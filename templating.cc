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
int BS_PERMUTATION_STEP = K(4);

#include <sys/mman.h>

//#define BANK_SELECTION BS_PERMUTATION
#define BANK_SELECTION BS_BANK_SELECT_BITS
#define ROW_SELECTION RS_DOUBLE_SIDED

#define HAMMER_LEN M(4)


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
Aggressor::Aggressor(struct ion_data *ion_chunk, int offset, int offset_to_start_row, int rowsize) {
    a_virt            = ion_chunk->virt + offset;
    a_row             = ion_chunk->virt + offset_to_start_row;
    a_rowsize         = rowsize;
    a_offset_in_chunk = offset;
    a_phys            = 0;
    a_accessesM = 0;
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
    c_disabled = false;

    selectAggressors();
    
    lprint("[Chunk %3d] %4dKB @ %10p (phys: %10p) | pairs: %5zu\n", id, c_len / 1024, 
            (void *) ion_chunk->virt, 
            (void *) ion_chunk->phys,
            getHammerPairs());
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

int Chunk::collectFlips(void *org, Aggressor *a1, Aggressor *a2, uintptr_t watch_region_start, int watch_region_size) {
    int offset = watch_region_start - c_virt;

    uint8_t *before = (uint8_t *)org;
    uint8_t *after  = (uint8_t *)watch_region_start;

    int flips = 0;
    for (int i = 0; i < watch_region_size; i++) {
        if (before[i] != after[i]) {
            Flip *flip = new Flip(c_ion_chunk, i+offset, before[i], after[i], a1, a2, c_cached);
            
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
/*
 * P.000 | P.001 | P.002 | P.003 | P.004 | P.005 | P.006 | P.007 <32 KB>
 * P.008 | P.009 | P.010 | P.011 | P.012 | P.013 | P.014 | P.015 <64 KB>
 * P.016 | P.017 | P.018 | P.019 | P.020 | P.021 | P.022 | P.023 <96 KB>
 * P.024 | P.025 | P.026 | P.027 | P.028 | P.029 | P.030 | P.031 <128 KB>
 * P.032 | P.033 | P.034 | P.035 | P.036 | P.037 | P.038 | P.039 <160 KB>
 * P.040 | P.041 | P.042 | P.043 | P.044 | P.045 | P.046 | P.047 <192 KB>
 * P.048 | P.049 | P.050 | P.051 | P.052 | P.053 | P.054 | P.055 <224 KB>
 * P.056 | P.057 | P.058 | P.059 | P.060 | P.061 | P.062 | P.063 <256 KB>
 * P.064 | P.065 | P.066 | P.067 | P.068 | P.069 | P.070 | P.071 <288 KB>
 * P.072 | P.073 | P.074 | P.075 | P.076 | P.077 | P.078 | P.079 <320 KB>
 * P.080 | P.081 | P.082 | P.083 | P.084 | P.085 | P.086 | P.087 <352 KB>
 * P.088 | P.089 | P.090 | P.091 | P.092 | P.093 | P.094 | P.095 <384 KB>
 * P.096 | P.097 | P.098 | P.099 | P.100 | P.101 | P.102 | P.103 <416 KB>
 * P.104 | P.105 | P.106 | P.107 | P.108 | P.109 | P.110 | P.111 <448 KB>
 * P.112 | P.113 | P.114 | P.115 | P.116 | P.117 | P.118 | P.119 <480 KB>
 * P.120 | P.121 | P.122 | P.123 | P.124 | P.125 | P.126 | P.127 <512 KB>
 
 
 * P.000 | P.001 | P.002 | P.003 | P.004 | P.005 | P.006 | P.007 | P.008 | P.009 | P.010 | P.011 | P.012 | P.013 | P.014 | P.015 <64 KB>
 * P.016 | P.017 | P.018 | P.019 | P.020 | P.021 | P.022 | P.023 | P.024 | P.025 | P.026 | P.027 | P.028 | P.029 | P.030 | P.031 <128 KB>
 * P.032 | P.033 | P.034 | P.035 | P.036 | P.037 | P.038 | P.039 | P.040 | P.041 | P.042 | P.043 | P.044 | P.045 | P.046 | P.047 <192 KB>
 * P.048 | P.049 | P.050 | P.051 | P.052 | P.053 | P.054 | P.055 | P.056 | P.057 | P.058 | P.059 | P.060 | P.061 | P.062 | P.063 <256 KB>
 * P.064 | P.065 | P.066 | P.067 | P.068 | P.069 | P.070 | P.071 | P.072 | P.073 | P.074 | P.075 | P.076 | P.077 | P.078 | P.079 <320 KB>
 * P.080 | P.081 | P.082 | P.083 | P.084 | P.085 | P.086 | P.087 | P.088 | P.089 | P.090 | P.091 | P.092 | P.093 | P.094 | P.095 <384 KB>
 * P.096 | P.097 | P.098 | P.099 | P.100 | P.101 | P.102 | P.103 | P.104 | P.105 | P.106 | P.107 | P.108 | P.109 | P.110 | P.111 <448 KB>
 * P.112 | P.113 | P.114 | P.115 | P.116 | P.117 | P.118 | P.119 | P.120 | P.121 | P.122 | P.123 | P.124 | P.125 | P.126 | P.127 <512 KB>
 *
 * P.000 | P.001 | P.002 | P.003 | P.004 | P.005 | P.006 | P.007 | P.008 | P.009 | P.010 | P.011 | P.012 | P.013 | P.014 | P.015 | P.016 | P.017 | P.018 | P.019 | P.020 | P.021 | P.022 | P.023 | P.024 | P.025 | P.026 | P.027 | P.028 | P.029 | P.030 | P.031 <128 KB>
 * P.032 | P.033 | P.034 | P.035 | P.036 | P.037 | P.038 | P.039 | P.040 | P.041 | P.042 | P.043 | P.044 | P.045 | P.046 | P.047 | P.048 | P.049 | P.050 | P.051 | P.052 | P.053 | P.054 | P.055 | P.056 | P.057 | P.058 | P.059 | P.060 | P.061 | P.062 | P.063 <256 KB>
 * P.064 | P.065 | P.066 | P.067 | P.068 | P.069 | P.070 | P.071 | P.072 | P.073 | P.074 | P.075 | P.076 | P.077 | P.078 | P.079 | P.080 | P.081 | P.082 | P.083 | P.084 | P.085 | P.086 | P.087 | P.088 | P.089 | P.090 | P.091 | P.092 | P.093 | P.094 | P.095 <384 KB>
 * P.096 | P.097 | P.098 | P.099 | P.100 | P.101 | P.102 | P.103 | P.104 | P.105 | P.106 | P.107 | P.108 | P.109 | P.110 | P.111 | P.112 | P.113 | P.114 | P.115 | P.116 | P.117 | P.118 | P.119 | P.120 | P.121 | P.122 | P.123 | P.124 | P.125 | P.126 | P.127 <512 KB>
 *
 * We do not know whether this is contiguous. We also do not know the rowsize.
 * We will hammer:
 *
 * <for 32KB rowsizes>
 * P.000 - P.16,17,18,19,20,21,22,23 A           < 8 combinations>
 * P.008 - P.24,25,26,27,28,29,30,31
 * P.016 - P.32,33,34,35,36,37,38,39
 * P.024 - P.40
 * P.032 - P.48
 * P.040 - P 56
 * P.048 - P.64
 * P.056 - P.72
 * P.064 - P.80
 * P.072 - P.88
 * P.080 - P.96
 * P.088 - P.104
 * P.096 - P.112
 * P.104 - P.120
 *                                           -----------------------
 *                                                  112 combinations
 *
 *
 * <for 64KB rowsizes>
 * P.00 - P.32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47     < 16 combinations >
 * P.16 - P.48                                                  < 16 combinations >
 * P.32 - P.64
 * P.48 - P.80
 * P.64 - P.96
 * P.80 - P.112
 *                                                             ---------------------
 *                                                                96 combinations
 *  
 * <for 128KB rowsizes> 
 * P.00 - P.64,65,66,67,68,69,70,...    < 32 combinations >
 * P.64 - P.96,97,98, ...               < 32 combinations>
 *                                      ---------------------
 *                                        64 combinations
 *
 * 112 + 96 + 64 = 272 combinations in 512KB
 * 256 * 512KB = 128MB --> 69632 pairs.
 */

    for (int rowsize = K(32); rowsize <= K(128); rowsize = rowsize * 2) {
//        printf("\nrowsize: %d\n", rowsize);
//        printf(  "-------\n");

        for (int row_in_chunk = 0;
                 row_in_chunk < (c_ion_chunk->len / rowsize);
                 row_in_chunk++) {
       
            /* relative addresses inside the chunk */ 
            int a1_row = row_in_chunk * rowsize;
            Aggressor *a1 = new Aggressor(c_ion_chunk, a1_row, a1_row, rowsize);

            int a2_row = a1_row + rowsize + rowsize;
            if (a2_row >= c_ion_chunk->len)
                continue;

            for (int page_in_row = 0; 
                     page_in_row < (rowsize / PAGESIZE);
                     page_in_row++) {

                int a2_offset = a2_row + (page_in_row * PAGESIZE);
                Aggressor *a2 = new Aggressor(c_ion_chunk, a2_offset, a2_row, rowsize);

    //            printf("(row in chunk: %d) A1: %p | A2: %p (page in row: %d) | distance: %d\n", row_in_chunk, (void *) a1->getVirt(), (void *) a2->getVirt(), page_in_row, a2->getVirt() - a1->getVirt());

                c_aggressors[a1].push_back(a2);
            }

            c_a1s.push_back(a1);

 //           printf("\n");
        }
    }
}

uint8_t org[M(4)];

void Chunk::doHammer(std::vector<PatternCollection *> &patterns, int accesses) {
    int a1_index = 1;
    int a1_count = c_aggressors.size();

    bool need_newline = false;




	/* loop over the aggressors randomly since we don't really know what rowsize we need */
    std::random_shuffle(c_a1s.begin(), c_a1s.end());

    for (auto a1: c_a1s) {
        std::vector<Aggressor *> a2s = c_aggressors[a1];

        if (need_newline) 
            lprint("\n");
        
        lprint("[+%4lu] - a1 %p (%2d/%2d rowsize: %d): ", time(NULL) - start_time, (void *)a1->getVirt(), a1_index, a1_count, a1->getRowsize());

        for (auto a2: a2s) {

//#define DEBUG_PRINT
#ifdef DEBUG_PRINT
            printf("\n\t\ta2: %10p -- delta: ", 
                    (void *) a2->getVirt()); fflush(stdout);
#endif

            std::vector<uint64_t> deltas;
            for (auto pattern: patterns) {

                /* we only check for bit flips in a -1MB and +1MB window.
                 * ideally, the aggressors are somewhat in the middle of this */
                uintptr_t chunk_start = c_virt;
                uintptr_t chunk_end   = c_virt + c_len;
                uintptr_t critical_region_start = a1->getRowVirt();
                uintptr_t critical_region_end   = a2->getRowVirt() + a2->getRowsize();
                uintptr_t watch_region_start = critical_region_start - M(1);
                uintptr_t watch_region_end   = critical_region_end   + M(1);
                if (watch_region_start < chunk_start) {
                    watch_region_start = chunk_start;
                }
                if (watch_region_end > chunk_end) {
                    watch_region_end = chunk_end;
                }
                int watch_region_size = watch_region_end - watch_region_start;

//              printf("Filling pattern: %p .. %d | %p .. %d | %p .. %d\n", c_virt, c_len, a1->getRowVirt(), a1->getRowsize(), a2->getRowVirt(), a2->getRowsize());
                pattern->fill(watch_region_start, watch_region_size,
                              a1->getRowVirt(), a1->getRowsize(),
                              a2->getRowVirt(), a2->getRowsize());


                memcpy(org, (void *)watch_region_start, watch_region_size);
//                printf("."); fflush(stdout);
#ifdef DRY_HAMMER
                deltas.push_back(49);
#else
                int delta = hammer((volatile uint8_t *)a1->getVirt(),
                                   (volatile uint8_t *)a2->getVirt(), accesses, false, false);
                deltas.push_back(delta);
#endif
                c_pairs_hammered++;

                a1->incrementAccesses(accesses);
                a2->incrementAccesses(accesses);

                if (memcmp(org, (void *) watch_region_start, watch_region_size)) 
                    collectFlips(org, a1, a2, watch_region_start, watch_region_size);

            }
            lprint("%llu ", compute_median(deltas));
            need_newline = true;

            if (times_up || oom) break;
        }
        a1_index++;

        if (times_up || oom) break;
    }
    lprint("\n");

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



bool Memory::exhaust(void) {
    size_t avail = get_FreeContigMem(K(256));
    if (avail == 0) {
        lprint("[TMPL] no higher order free chunks\n");
        lprint("[TMPL] it is unlikely that we get more contiguous memory\n"); 
        lprint("[TMPL] try closing some apps and/or reboot your device\n");
        return false;
    }

    /* what if there is less than 64MB available? */
    /* what if no large contiguous chunks at all? */
    size_t hammer_len = HAMMER_LEN;

    std::vector<struct ion_data *> noncontig_chunks;

    // do a second allocation, if possible and use the best one 
    struct ion_data *first_option = NULL;
    size_t first_option_delta = 0;

    while (true) {
        size_t before = get_FreeContigMem(K(256));
//      lprint("[TEMPL] before: %zu\n", before);

        // allocate 64 MB
//      lprint("[TEMPL] alloc %d bytes on heap %d... ", hammer_len, device.ion_heap);
        struct ion_data *chunk =  new ion_data;
        if (chunk == NULL) {
            perror("Could not allocate memory");
            exit(EXIT_FAILURE);
        }
        chunk->handle = ION_alloc(hammer_len, device.ion_heap);
        if (chunk->handle == 0) {
            lprint("Failed to allocate 64MB: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
//      lprint("Success\n");
        chunk->len = hammer_len;
        chunk->mapping = NULL;


        size_t after = get_FreeContigMem(K(256));
 //     lprint("[TEMPL] after: %zu\n", after);

        if (after < before) {
            size_t delta = before - after;
            lprint("[TEMPL] delta: %zu\n", delta);

            if (first_option == NULL && delta < hammer_len && (after > hammer_len || after > delta)) {
                lprint("this is our first try\n");
                lprint("- number of bytes that are probably contiguous: %d out of %d\n", delta, hammer_len);
                lprint("- number of bytes contiguous bytes that are still available: %d\n", after);
                // try it once more and use the biggest one
                first_option = chunk;
                first_option_delta = delta;

                // note thate we do not add this chunk to the noncontig list 
                continue;
            }


            // check if we have a first option that we can use (only if larger)
            struct ion_data *chunk_to_use = chunk;
            size_t best_delta = delta;
            if (first_option != NULL) {
                lprint("this is our second try\n");
                lprint("- number of bytes that are probably contiguous: %d out of %d (this try)\n", delta, hammer_len);
                lprint("- number of bytes that are probably contiguous: %d out of %d (first try)\n", first_option_delta, hammer_len);

                if (delta < first_option_delta) {
                    lprint("we will use the first try\n");
                    best_delta = first_option_delta;
                    chunk_to_use = first_option;
                    noncontig_chunks.push_back(chunk);
                } else {
                    lprint("we will use the second try\n");
                    noncontig_chunks.push_back(first_option);
                }
            }

            lprint("We should have some contiguous chunks now. best delta: %zu\n", best_delta);

            // free all chunks that were allocated before this one
            ION_clean_all(noncontig_chunks);

            // prepare the chunk that we will hammer
            ION_mmap(chunk_to_use);
           
            Chunk *c = new Chunk(chunk_to_use, 1);
            m_chunks.push_back(c);
            m_pairs = c->getHammerPairs();
            lprint("[Memory] %zu aggressor pairs\n", m_pairs);
            break;
        }

        //add chunk to list that we should free
        noncontig_chunks.push_back(chunk);
    }

    return true;
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
            if (chunk->isDisabled()) 
                continue;
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



            if (oom) {
                lprint("OOM, but nothing we can do\n");
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

void Memory::disableChunks(void) {
    for (auto chunk: m_chunks) {
        chunk->disable();
    }
}



void TMPL_run(std::vector<PatternCollection *> &patterns, int timer, int accesses, int rounds) {
    int passes = 0;

    Memory memory;
    while (true) {
        bool contig = memory.exhaust();
        if (!contig)
            break;
        memory.doHammer(patterns, timer, accesses, rounds);
         
        // disable all previous chunks before exhausting again
        memory.disableChunks();
        passes++;

        if (passes == 100) {
            lprint("That should be enough. giving up.\n");
            break;
        }
    }
    memory.cleanup();
}





