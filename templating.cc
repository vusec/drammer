/*
 * Copyright 2016, Victor van der Veen
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */


#include <assert.h>
#include <stdlib.h>

#include "ion.h"
#include "rowsize.h"
#include "massage.h"
#include "templating.h"


#define RS_SINGLE_SIDED 0
#define RS_DOUBLE_SIDED 1
#define RS_AMPLIFIED 2
#define RS_ANY_SIDED 3

#define BS_BANK_SELECT_BITS 0
#define BS_TRESHOLD 1
#define BS_PERMUTATION 2
#define BS_PERMUTATION_STEP 4096


#define BANK_SELECTION BS_PERMUTATION
#define ROW_SELECTION RS_DOUBLE_SIDED


//#define DRY_HAMMER



extern struct model device;


time_t start_time;

bool times_up, oom;
void alarm_handler(int signal) {
    if (signal == SIGALRM) {
        lprint("[SIGALRM] Time is up\n");
        times_up = true;
    } else if (signal == SIGUSR1) {
        lprint("[SIGUSR1] OOM-killer\n");
        oom = true;
    } else if (signal == SIGTERM) {
        lprint("[SIGTERM]\n");
        times_up = true;
    }
}










/***************************************
 * CLASS Pattern
 */
Pattern::Pattern(int c) {
    this->c = c;

    cur_use = 0;
    max_use = 10;
    rerandomize();
};

void Pattern::fill(uintptr_t p, int len) {
    if (c != -1) {
        memset((void *)p, c, len);
        return;
    }

    int offset = 0;
    while (len > 0) {
        int bytes = (len < K(16) ? len : K(16));

        memcpy((void *) (p + offset), r, bytes);
    
        offset += bytes;
        len -= bytes;
    }

    cur_use++;
    if (cur_use == max_use) {
        rerandomize();
        cur_use = 0;
    }
}

void Pattern::rerandomize(void) {
    for (int i = 0; i < K(16); i++) 
        r[i] = rand() % 255;
}

/***************************************
 * CLASS PatternCollection
 */
PatternCollection::PatternCollection(const char *name, int ck, int a1, int a2) {
    p_name = name;
    p_ck = new Pattern(ck);
    p_a1 = new Pattern(a1);
    p_a2 = new Pattern(a2);
}
PatternCollection::~PatternCollection(void) {
    delete(p_ck);
    delete(p_a1);
    delete(p_a2);
}
void PatternCollection::fill(uintptr_t ck, int ck_len, 
                             uintptr_t a1, int a1_len, 
                             uintptr_t a2, int a2_len) {
            p_ck->fill(ck, ck_len);
            p_a1->fill(a1, a1_len);
            p_a2->fill(a2, a2_len);
}



/***************************************
 * CLASS Aggressor
 */
Aggressor::Aggressor(struct ion_data *ion_chunk, int row_in_chunk, int offset_in_row) {
    a_row_in_chunk  = row_in_chunk;
    a_offset_in_row = offset_in_row;

    a_row_virt        = ion_chunk->virt + (row_in_chunk * device.rowsize);
    a_virt            = a_row_virt + a_offset_in_row;
    a_offset_in_chunk = a_virt - ion_chunk->virt;
    a_phys            = ion_chunk->phys + a_offset_in_chunk;
        
    a_accessesM = 0;
}

int Aggressor::getBank(void) {
    uint32_t  ba2 = __builtin_popcount(a_offset_in_chunk & device.ba2) % 2;
    uint32_t  ba1 = __builtin_popcount(a_offset_in_chunk & device.ba1) % 2;
    uint32_t  ba0 = __builtin_popcount(a_offset_in_chunk & device.ba0) % 2;
    uint32_t rank = __builtin_popcount(a_offset_in_chunk & device.rank) % 2;
    return (1*rank) + (2*ba0) + (4*ba1) + (8*ba2);
}


/***************************************
 * CLASS Flip
 */
Flip::Flip(struct ion_data *ion_chunk, int index, uint8_t before, uint8_t after, Aggressor *a1, Aggressor *a2, bool cached) {
    f_a1 = a1;
    f_a2 = a2;
    f_bits = before ^ after;
    f_count = 1;
    f_before = before;
    f_after = after;
    
    f_virt = ion_chunk->virt + index;
    f_phys = ion_chunk->phys + index;
    f_cached = cached;

}

void Flip::dump(struct ion_data *ion_chunk, uint64_t count) {
    printf("\n");
    lprint("[+%4lu] FLIP at v:%p p:%p %02x != %02x [count %llu] [ion v:%p p:%p + %d] [a1 v:%p p:%p c:%lluM] [a2 v:%p p:%p c:%lluM]\n", 
            time(NULL) - start_time, 
            f_virt, f_phys, f_before, f_after,
            count,
            ion_chunk->virt, ion_chunk->phys, ion_chunk->len,
            f_a1->getVirt(), f_a1->getPhys(), f_a1->getAccesses(),
            f_a2->getVirt(), f_a2->getPhys(), f_a1->getAccesses());
    if (f_cached) 
        lprint("      \\------- with DC CIVAC\n");
}

int Flip::compare(Flip *f) {
    return (f_virt == f->getVirt() &&
            f_phys == f->getPhys() &&
//          f_a1   == f->getA1()   &&
//          f_a2   == f->getA2()   &&
            f_bits == f->getBits());
}


/***************************************
 * CLASS Chunk
 */
Chunk::Chunk(struct ion_data *ion_chunk, int id) {
    c_ion_chunk = ion_chunk;
    c_len  = ion_chunk->len;
    c_virt = ion_chunk->virt;
    c_phys = ion_chunk->phys;
    c_rounds_completed = 0;
    c_id = id;
    c_pairs_hammered = 0;
    c_cached = false;

    selectAggressors();
    
    lprint("[Chunk %3d] %4dKB @ %10p (phys: %10p) | pairs: %5zu\n", id, c_len / 1024, 
            (void *) ion_chunk->virt, 
            (void *) ion_chunk->phys,
            getHammerPairs());
}

void Chunk::disable(void) {
    c_ion_chunk = NULL;
    c_len = 0;
    c_virt = 0;
    
    for (auto it: c_aggressors) {
        Aggressor *a1 = it.first;
        std::vector<Aggressor *> a2s = it.second;

        delete(a1);
        for (auto a2: a2s) {
            delete(a2);
        }
    }
}

bool Chunk::makeCached(void) {
    ION_clean(c_ion_chunk);
        
    c_ion_chunk->handle = ION_alloc(c_len, device.ion_heap, true);
    if (c_ion_chunk->handle == 0) {
        lprint("Unable to allocate ION chunk of size %d, despite having just released one. Disabling this chunk");
        disable();
        return false;
    }
    c_ion_chunk->len = c_len;
    if (ION_mmap(c_ion_chunk) != 0) {
        lprint("Unable to mmap. Disabling this chunk");
        disable();
        return false;
    }
    
    c_virt = c_ion_chunk->virt;
    c_phys = c_ion_chunk->phys;
    c_cached = true;

    selectAggressors();
    
    lprint("[Chunk %3d is now cached] %4dKB @ %10p (phys: %10p) | pairs: %5zu\n", c_id, c_len / 1024, 
            (void *) c_ion_chunk->virt, 
            (void *) c_ion_chunk->phys,
            getHammerPairs());

    return true;
}

size_t Chunk::getHammerPairs(void) {
    size_t combinations = 0;
    for (auto it: c_aggressors) 
        combinations += it.second.size();
    return combinations;
}
uint64_t Chunk::getAccesses(void) {
    uint64_t accesses = 0;
    for (auto it: c_aggressors) {
        accesses += it.first->getAccesses();
        for (auto a: it.second) {
            accesses += a->getAccesses();
        }
    }
    return accesses;
}

int Chunk::collectFlips(void *org, Aggressor *a1, Aggressor *a2) {
    uint8_t *before = (uint8_t *)org;
    uint8_t *after  = (uint8_t *)c_virt;

    int flips = 0;
    for (int i = 0; i < c_len; i++) {
        if (before[i] != after[i]) {
            Flip *flip = new Flip(c_ion_chunk, i, before[i], after[i], a1, a2, c_cached);
            
            int count = 1;    
            for (auto f: c_flips) {
                if (f->compare(flip)) {
                    count = f->hit(); // will be 2 at least
                    break;
                }
            }
            if (count == 1) 
                c_flips.push_back(flip);

            flip->dump(c_ion_chunk, count);
        }
    }
    return flips;
}

uint64_t Chunk::getBitFlips(bool only_unique) {
    if (only_unique) {
        return c_flips.size();
    }

    uint64_t count = 0;
    for (auto flip: c_flips) {
        count += flip->getCount();
    }
    return count;
}

void Chunk::selectAggressors(void) {
    c_rows_in_chunk = c_ion_chunk->len / device.rowsize;
    for (int a1_row_in_chunk = 0; 
             a1_row_in_chunk < c_rows_in_chunk; 
             a1_row_in_chunk++) {

        std::set<int> a2_rows_in_chunk;
        if (ROW_SELECTION == RS_SINGLE_SIDED) {
            int a2_row_in_chunk = 0;
            if (a1_row_in_chunk < c_rows_in_chunk/2) 
                a2_row_in_chunk = c_rows_in_chunk - 1;
            a2_rows_in_chunk.insert(a2_row_in_chunk);
        } else if (ROW_SELECTION == RS_AMPLIFIED) {
            int a2_row_in_chunk = a1_row_in_chunk + 1;
            if (a2_row_in_chunk < c_rows_in_chunk)
                a2_rows_in_chunk.insert(a2_row_in_chunk);
        } else if (ROW_SELECTION == RS_DOUBLE_SIDED) {
            int a2_row_in_chunk = a1_row_in_chunk + 2;
            if (a2_row_in_chunk < c_rows_in_chunk) 
                a2_rows_in_chunk.insert(a2_row_in_chunk);
        } else if (ROW_SELECTION == RS_ANY_SIDED) {
            for (int i = 0; i < c_rows_in_chunk; i++) {
                int a2_row_in_chunk = i;
                if (a2_row_in_chunk == a1_row_in_chunk) continue;
                a2_rows_in_chunk.insert(a2_row_in_chunk);
            }
        }

        if (BANK_SELECTION == BS_BANK_SELECT_BITS) {
            /* Hammer according the bank select bits ba0, ba1, and ba2 (and
             * rank?)
             *
             * -----------------------------
             * | r1b1 | r1b2 | r1b3 | r1b4 | <-- a1_row_in_chunk
             * -----------------------------
             * |...                        |
             * ----------------------------|
             * | rXb3 | rXb1 | rXb4 | rXb2 | <-- a2_row_in_chunk
             * -----------------------------
             *
             * We will hammer:
             * - r1b1, rXb1
             * - r1b2, rXb2
             * - r1b3, rXb3
             * - r1b4, rXb4
             */

            if (device.ba0 == 0 || device.ba1 == 0 || device.ba2 == 0) {
                lprint("No bank select bits available\n");
                exit(0);
            }
            if (device.ba2 > c_ion_chunk->len) {
                lprint("Chunk not large enough to hold all bank-select bits\n"); 
                return;
            }


            std::set<int> banks;
            for (int a1_offset_in_row = 0;
                     a1_offset_in_row < device.rowsize;
                     a1_offset_in_row += CACHELINE_SIZE) {

                Aggressor *a1 = new Aggressor(c_ion_chunk, a1_row_in_chunk, a1_offset_in_row);
                int bank = a1->getBank();
                if (banks.count(bank)) {
                    delete(a1);
                    continue;
                }
                banks.insert(bank);
                
                for (auto a2_row_in_chunk: a2_rows_in_chunk) {

                    for (int a2_offset_in_row = 0;
                             a2_offset_in_row < device.rowsize;
                             a2_offset_in_row += CACHELINE_SIZE) {

                        Aggressor *a2 = new Aggressor(c_ion_chunk, a2_row_in_chunk, a2_offset_in_row);
                        if (a2->getBank() == bank) {
                            /* Something for a dry-run? */
                            /*
                            printf("%10p [row %3d off %5x] and %10p [row %3d off %5x] [bank %2d]\n", 
                                  (void *) a1->getVirt(), a1->getRowInChunk(), a1->getOffsetInRow(), 
                                  (void *) a2->getVirt(), a2->getRowInChunk(), a2->getOffsetInRow(), bank);
                                  */
                            c_aggressors[a1].push_back(a2);
                            break;
                        }
                        delete(a2);
                    }
                }
            }
        } else if (BANK_SELECTION == BS_PERMUTATION ||
                   BANK_SELECTION == BS_TRESHOLD) {
            /* Simply hammer all combinations for two rows.
             *
             * -------------------------
             * | a | b | c | d | e | f | <-- a1_row_in_chunk
             * -------------------------
             * |...                    |
             * -------------------------
             * | 1 | 2 | 3 | 4 | 5 | 6 | <-- a2_row_in_chunk
             * -------------------------
             *
             * We will hammer:
             * - a1, a2, a3, a4, a5, a6
             * - b1, b2, b3, b4, b5, b6
             * - c1, c2, c3, c4, c5, c6
             * - d1, d2, d3, d4, d5, d6
             * - e1, e2, e3, e4, e5, e6
             * - f1, f2, f3, f4, f5, f6
             *
             * When using TRESHOLD, we check whether these combinations actually
             * exceed the treshold as found by rowsize.cc
             */
            for (int a1_offset_in_row = 0;
                     a1_offset_in_row < device.rowsize;
                     a1_offset_in_row += BS_PERMUTATION_STEP) {

                Aggressor *a1 = new Aggressor(c_ion_chunk, a1_row_in_chunk, a1_offset_in_row);

                for (auto a2_row_in_chunk: a2_rows_in_chunk) {

                    for (int a2_offset_in_row = 0;
                             a2_offset_in_row < device.rowsize;
                             a2_offset_in_row += BS_PERMUTATION_STEP) {

                        Aggressor *a2 = new Aggressor(c_ion_chunk, a2_row_in_chunk, a2_offset_in_row);

                        if (BANK_SELECTION == BS_TRESHOLD) {
                            /* TODO treshold check */
                        }

                        /* Something for a dry-run? */
                        /*
                        printf("%10p [row %3d off %5x] and %10p [row %3d off %5x]\n", 
                              (void *) a1->getVirt(), a1->getRowInChunk(), a1->getOffsetInRow(), 
                              (void *) a2->getVirt(), a2->getRowInChunk(), a2->getOffsetInRow());
                              */
                        c_aggressors[a1].push_back(a2);
                    }
                }
            }
        }
    }
}

uint8_t org[M(4)];

void Chunk::doHammer(std::vector<PatternCollection *> &patterns, int accesses) {
    int last_row = -1;
    bool need_newline = false;
    for (auto combo: c_aggressors) {
                    Aggressor *  a1  = combo.first;
        std::vector<Aggressor *> a2s = combo.second;

        if (last_row != a1->getRowInChunk()) {
            last_row = a1->getRowInChunk();
           
            if (need_newline) 
            printf("\n");
            lprint("[+%4lu] - a1 row %2d/%2d (phys %d): ", 
                    time(NULL) - start_time, a1->getRowInChunk()+1, c_rows_in_chunk, a1->getPhysRow());
            logger->fprint("\n");

        }

        for (auto a2: a2s) {

#ifdef DEBUG_PRINT
            int bank = -1;
            if (BANK_SELECTION == BS_BANK_SELECT_BITS) {
                bank = a2->getBank();
            }
            printf("bank %2d | a1: %10p (row: %2d) | a2: %10p (row: %2d) -- delta: ", 
                    bank, (void *) a1->getVirt(), a1->getRowInChunk(), (void *) a2->getVirt(), a2->getRowInChunk());
#endif

            std::vector<uint64_t> deltas;
            for (auto pattern: patterns) {
                pattern->fill(c_virt, c_len,
                              a1->getRowVirt(), device.rowsize,
                              a2->getRowVirt(), device.rowsize);

                memcpy(org, (void *)c_virt, c_len);
#ifdef DRY_HAMMER
                deltas.push_back(49);
#else
                int delta = hammer((volatile uint8_t *)a1->getVirt(),
                                   (volatile uint8_t *)a2->getVirt(), accesses, false, c_cached);
                deltas.push_back(delta);
#endif
                c_pairs_hammered++;

                a1->incrementAccesses(accesses);
                a2->incrementAccesses(accesses);

                if (memcmp(org, (void *) c_virt, c_len)) 
                    collectFlips(org, a1, a2);

            }
            lprint("%llu ", compute_median(deltas));
            need_newline = true;

            if (times_up || oom) break;
        }

        if (times_up || oom) break;
    }
    printf("\n");

    c_rounds_completed++;
}

Chunk::~Chunk(void) {
    for (auto it: c_aggressors) {
        Aggressor *a1 = it.first;
        std::vector<Aggressor *> a2s = it.second;

        delete(a1);
        for (auto a2: a2s) {
            delete(a2);
        }
    }
}


/***************************************
 * CLASS Memory
 */
Memory::Memory() {
}

void Memory::exhaust(void) {
    m_kb = ionExhaust(m_ion_chunks, device.rowsize * 4, device.ion_heap);
    m_pairs = 0;
    int id = 1;
    for (auto ion_chunk: m_ion_chunks) {
        Chunk *c = new Chunk(ion_chunk, id);
        m_chunks.push_back(c);
        m_pairs += c->getHammerPairs();
        id++;
    }
    lprint("[Memory] Allocated %dKB (%d MB) in %zu ION chunks resulting in %zu aggressor pairs\n", 
            m_kb, m_kb/1024, m_ion_chunks.size(), m_pairs);
}

void Memory::releaseLargestChunk(void) {
    for (auto ion_chunk: m_ion_chunks) {
        if (ion_chunk->virt) {
            lprint("[Memory] Releasing chunk at %p with size %d\n", ion_chunk->virt, ion_chunk->len);

            for (auto c: m_chunks) {
                if (c->getVirt() == ion_chunk->virt) {
                    lprint("[Memory] Disabling chunk %d\n", c->getId());
                    c->disable();
                    break;
                }
            }

            ION_clean(ion_chunk);
            break;
        }
    }
}

uint64_t Memory::getBitFlips(void) {
    uint64_t flips = 0;
    for (auto chunk: m_chunks) {
        flips += chunk->getBitFlips(false);
    }
    return flips;
}

uint64_t Memory::getUniqueBitFlips(void) {
    uint64_t flips = 0;
    for (auto chunk: m_chunks) {
        flips += chunk->getBitFlips(true);
    }
    return flips;
}

size_t Memory::getPairsHammered(void) {
    size_t pairs = 0;
    for (auto chunk: m_chunks) {
        pairs += chunk->getPairsHammered();
    }
    return pairs;
}

uint64_t Memory::getAccesses(void) {
    uint64_t accesses = 0;
    for (auto chunk: m_chunks) {
        accesses += chunk->getAccesses();
    }
    return accesses;
}



void Memory::doHammer(std::vector<PatternCollection *> &patterns, int timer, int accesses, int rounds) {

/* Questions:
 *  - What patterns to use?
 *      - Chunk: 0x00 / 0xff / rand()
 *      - Aggressor1: 0x00 / 0xff / rand() 
 *      - Aggressor2: 0x00 / 0xff / rand()
 *  - How to select the banks?
 *      - Permutation
 *      - Treshold
 *      - Bank select bits
 *  - How to select the rows?
 *      - Single Sided
 *      - Double Sided
 *      - Amplified
 *      - Any Sided
 */



    start_time = time(NULL);


        
    times_up = false;
    oom = false;
    struct sigaction new_action, old_TERM, old_ALARM, old_USR1;
    
    new_action.sa_handler = alarm_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction(SIGTERM, &new_action, &old_TERM);
    sigaction(SIGUSR1, &new_action, &old_USR1);
    if (timer) {
        sigaction(SIGALRM, &new_action, &old_ALARM);
        alarm(timer);
    }


    uint64_t  flips_total = 0;
    uint64_t uflips_total = 0;
    uint64_t pairs_hammered_total = 0;


    for (m_rounds_completed = 0; 
         m_rounds_completed < rounds; 
         m_rounds_completed++) {
        lprint("[Memory] Starting round %d/%d\n", m_rounds_completed+1, rounds);

        uint64_t  flips_round = 0;
        uint64_t uflips_round = 0;
        uint64_t pairs_hammered_round = 0;

        time_t start_round = time(NULL);
        for (auto chunk: m_chunks) {
            lprint("[+%4lu] Hammering chunk %d/%d round %d/%d virt %p phys %p len %d\n", 
                    time(NULL) - start_time, 
                    chunk->getId(), m_chunks.size(), m_rounds_completed+1, rounds, 
                    chunk->getVirt(), chunk->getPhys(), chunk->getSize());

            uint64_t  flips_chunk_before = chunk->getBitFlips(false);
            uint64_t uflips_chunk_before = chunk->getBitFlips(true);
            uint64_t pairs_hammered_chunk_before = chunk->getPairsHammered();

            time_t start_chunk = time(NULL);
            chunk->doHammer(patterns, accesses);
            lprint("[+%4lu] Hammering chunk %d/%d round %d/%d virt %p phys %p len %d took %lus\n", 
                    time(NULL) - start_time, 
                    chunk->getId(), m_chunks.size(), m_rounds_completed+1, rounds, 
                    chunk->getVirt(), chunk->getPhys(), chunk->getSize(), 
                    time(NULL) - start_chunk);

            uint64_t  flips_chunk_total = chunk->getBitFlips(false);
            uint64_t uflips_chunk_total = chunk->getBitFlips(true);
            uint64_t  flips_chunk_round =  flips_chunk_total -  flips_chunk_before;
            uint64_t uflips_chunk_round = uflips_chunk_total - uflips_chunk_before;
             flips_round +=  flips_chunk_round;
            uflips_round += uflips_chunk_round;
             flips_total +=  flips_chunk_round;
            uflips_total += uflips_chunk_round;
            uint64_t pairs_hammered_chunk_total = chunk->getPairsHammered();
            uint64_t pairs_hammered_chunk_round = pairs_hammered_chunk_total - pairs_hammered_chunk_before;
            pairs_hammered_round += pairs_hammered_chunk_round;
            pairs_hammered_total += pairs_hammered_chunk_round;
            lprint("[+%4lu] [c_last p:%llu f:%llu u:%llu] [c_all p:%llu f:%llu u:%llu] [r_cur p:%llu f:%llu u:%llu] [r_all p:%llu f:%llu u:%llu]\n",
                    time(NULL) - start_time,
                    pairs_hammered_chunk_round, flips_chunk_round, uflips_chunk_round,
                    pairs_hammered_chunk_total, flips_chunk_total, uflips_chunk_total,
                    pairs_hammered_round, flips_round, uflips_round,
                    pairs_hammered_total, flips_total, uflips_total);
            lprint("\n");

#ifdef ARMV8
            if (flips_chunk_round > 0) {
                lprint("[%4lu] Found bit flips on a ARMv8 device using DMA.\n",
                        time(NULL) - start_time);
                bool cached = chunk->makeCached();
                if (cached) {
                    lprint("[%4lu] Hammering chunk %d/%d virt %p phys %p len %d with explicit cache flush\n",
                            time(NULL) - start_time,
                            chunk->getId(), m_chunks.size(),
                            chunk->getVirt(), chunk->getPhys(), chunk->getSize());
                    chunk->doHammer(patterns, accesses);
                }
            }
#endif


            // TODO check for OOM-killer and if so, remove one of the largest chunks
            if (oom) {
                releaseLargestChunk();
                oom = false;
            }

            if (times_up) 
                break;
        }

        double pairs_per_bitflip = 0.0;
        if (flips_total > 0)
            pairs_per_bitflip = pairs_hammered_total / (double) flips_total;

        lprint("[Memory] Round %d (%d chunks) took %lus\n", 
                m_rounds_completed+1, m_chunks.size(), time(NULL) - start_round);
        lprint("[Memory] - total seconds passed: %lu\n", time(NULL) - start_time);
        lprint("[Memory] -       pairs hammered: + %llu = %llu\n", pairs_hammered_round, pairs_hammered_total);
        lprint("[Memory] -                flips: + %llu = %llu\n",  flips_round,  flips_total);
        lprint("[Memory] -         unique flips: + %llu = %llu\n", uflips_round, uflips_total);
        lprint("[Memory] -       pairs per flip: %1.2f (average)\n", pairs_per_bitflip);
        lprint("[Memory] -        DRAM accesses: %lluM\n", getAccesses());
        lprint("\n");


        if (times_up)
            break;
    }        

    /* Restore the signal handler for SIGALRM */
    if (timer) {
        alarm(0);
        sigaction(SIGALRM, &old_ALARM, NULL);
    }
    sigaction(SIGTERM, &old_TERM, NULL);
    sigaction(SIGUSR1, &old_USR1, NULL);
}

void Memory::cleanup(void) {
    for (auto chunk: m_chunks) {
        delete(chunk);
    }
    ION_clean_all(m_ion_chunks);
}





void TMPL_run(std::vector<PatternCollection *> &patterns, int timer, int accesses, int rounds) {
    Memory memory;
    memory.exhaust();
    memory.doHammer(patterns, timer, accesses, rounds);
    memory.cleanup();
}





