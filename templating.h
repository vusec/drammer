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

#ifndef __TEMPLATING_H__
#define __TEMPLATING_H__

#include <vector>

#include "ion.h"

#define ONE_TO_ZERO 1
#define ZERO_TO_ONE 0

#define FLIP_DIRECTION_STR(x) (((x) == ONE_TO_ZERO) ? "1-to-0" : "0-to-1")

struct template_t {
    uintptr_t virt_page;      // virtual address of the vulnerable page 
    uintptr_t virt_addr;      // virutal address of the vulnerable byte
    uintptr_t virt_row;
    uintptr_t phys_addr;
    uintptr_t phys_page;
    int virt_index;  
    uint8_t org_byte;         // the original value of the vulnerable byte
    uint32_t org_word;
    uint8_t new_byte;         // the new value
    uint32_t new_word;
    struct ion_data *ion_chunk;
    int ion_len;
            
    uint8_t xorred_byte;
    uint32_t xorred_word;
    int bits_set;
    int bit_offset;
    int org_bit;
    int direction;
    bool maybe_exploitable;
    bool likely_exploitable;
    int rel_pfn;
    int rel_address;
    int rel_row_index;
    uint32_t source_pte;
    uint32_t target_pte;
    uint32_t target_16k_pfn;
    uint32_t source_16k_pfn;
    uint32_t source_pfn, target_pfn;
    uint32_t source_page_index_in_row, target_page_index_in_row;
    uint32_t source_pfn_row, target_pfn_row;
    int byte_index_in_row;
    int byte_index_in_page;
    int word_index_in_page;
    int word_index_in_pt;
    int bit_index_in_word;
    int bit_index_in_byte;
    uintptr_t virt_above;
    uintptr_t virt_below;
    bool confirmed;
    time_t found_at;
};

struct pattern_t {
    uint8_t *above;
    uint8_t *victim;
    uint8_t *below;
    int cur_use;
    int max_use;
    void (*reset_above) (uint8_t *);
    void (*reset_victim)(uint8_t *);
    void (*reset_below) (uint8_t *);
};



struct template_t *templating(void);
void TMPL_run(std::vector<struct ion_data *> &chunks, 
              std::vector<struct template_t *> &templates,
              std::vector<struct pattern_t *> &patterns, int timer, int hammer_readcount,
              bool do_conservative);
struct template_t *find_template_in_rows(std::vector<struct ion_data *> &chunks, struct template_t *needle);

#endif // __TEMPLATING_H__
