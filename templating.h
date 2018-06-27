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
#include "rowsize.h"

extern struct model device;

class Pattern {
    public:
        Pattern(int c);
        void fill(uintptr_t p, int len);

    private:
        void rerandomize(void);

        int c;
        int cur_use;
        int max_use;
        uint8_t r[K(16)];
};

class PatternCollection {
    public:
         PatternCollection(const char *name, int ck, int a1, int a2); 
        ~PatternCollection(void);
        
        void fill(uintptr_t ck, int ck_len, 
                  uintptr_t a1, int a1_len,
                  uintptr_t a2, int a2_len); 
    private:
        const char *p_name;
        Pattern *p_ck;
        Pattern *p_a1;
        Pattern *p_a2;
};



class Aggressor {
    public:
        Aggressor(struct ion_data *ion_chunk, int offset, int offset_to_start_row, int rowsize);
        
        void incrementAccesses(int accesses) { a_accessesM += accesses / MILLION; };

        uintptr_t getVirt(void)     { return a_virt;            };
        uintptr_t getPhys(void)     { return a_phys;            };
        int getRowsize(void) { return a_rowsize; };
        uintptr_t getRowVirt(void) { return a_row; };
        int getOffsetInChunk(void)  { return a_offset_in_chunk; };
        uint64_t getAccesses(void) { return a_accessesM; };

            bool operator<(Aggressor& other) const {
                if (a_virt != other.getVirt())
                    return false;
                if (a_rowsize != other.getRowsize())
                    return false;
                return true;
            };

    private:
        uintptr_t a_virt;
        uintptr_t a_phys;
        uintptr_t a_row;
        int a_rowsize;
        int a_offset_in_chunk;
        uint64_t a_accessesM; // in million
};


class Flip {
    public:
        Flip(struct ion_data *chunk, int index, uint8_t before, 
                                                uint8_t after, Aggressor *a1, 
                                                               Aggressor *a2, bool cached);
        uintptr_t getVirt(void) { return f_virt; };
        uintptr_t getPhys(void) { return f_phys; };
        Aggressor *getA1(void) { return f_a1; };
        Aggressor *getA2(void) { return f_a2; };
        uint8_t getBits(void) { return f_bits; };
        uint64_t hit(void) { f_count++; return f_count; };
        uint64_t getCount(void) { return f_count; };
        int compare(Flip *f);
        void dump(struct ion_data *ion_chunk, uint64_t count);

    private:
        uintptr_t f_virt;
        uintptr_t f_phys;
        Aggressor *f_a1;
        Aggressor *f_a2;
        uint8_t f_before;
        uint8_t f_after;
        uint8_t f_bits; // before ^ after
        uint64_t f_count; 
        bool f_cached;
};


class Chunk {
    public:
         Chunk(struct ion_data *ion_chunk, int id);
        ~Chunk();

        void doHammer(std::vector<PatternCollection *> &patterns, int acceses);
        size_t getHammerPairs(void);
        int getRows(void) { return c_rows_in_chunk; };
        int getId(void) { return c_id; };
        uint64_t getBitFlips(bool only_unique); 
        size_t getPairsHammered(void) { return c_pairs_hammered; };
        uint64_t getAccesses(void);
        int getSize(void) { return c_len; };
        uintptr_t getVirt(void) { return c_virt; };
        uintptr_t getPhys(void) { return c_phys; };

        bool makeCached(void);

        void disable(void) { c_disabled = true; }
        bool isDisabled(void) { return c_disabled; };

    private:
        void selectAggressors(void);
        int collectFlips(void *org, Aggressor *a1, Aggressor *a2, uintptr_t watch_region_start, int watch_region_size); 




        std::map<Aggressor *, std::vector<Aggressor *>> c_aggressors;
        std::vector<Aggressor *> c_a1s;
        std::vector<Flip *> c_flips;
        uintptr_t c_virt;
        uintptr_t c_phys;
        int c_len;
        struct ion_data *c_ion_chunk;
        uint64_t c_rounds_completed;
        int c_rows_in_chunk;
        int c_id;
        size_t c_pairs_hammered;
        bool c_cached;
        bool c_disabled;
};


class Memory {
    public:
        Memory();
        void doHammer(std::vector<PatternCollection *> &patterns, int timer, int accesses, int rounds);
        bool exhaust(void);
        void cleanup(void);
        void disableChunks(void);

        uint64_t getBitFlips(void);
        uint64_t getUniqueBitFlips(void);
        size_t getPairsHammered(void); 
        uint64_t getAccesses(void); 
        void releaseLargestChunk(void);

    private:
        std::vector<Chunk *> m_chunks;
        std::vector<struct ion_data *> m_ion_chunks;
        int m_kb;
        int m_rounds_completed;
        size_t m_pairs;
};




void TMPL_run(std::vector<PatternCollection *> &patterns, int timer, int accesses, int rounds);
              

#endif // __TEMPLATING_H__
