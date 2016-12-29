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

#ifndef __ROWSIZE_H__
#define __ROWSIZE_H__

#include <set>

#include "helper.h"

const std::set<int> VALID_ROWSIZES = {K(16), K(32), K(64), K(128), K(256)};

#define PAGES_PER_ROW (rowsize / PAGESIZE)
#define MAX_ROWSIZE K(256)

int RS_autodetect(int force_autodetect, struct model *our_model); 

struct model {
    std::string generic_name;

    std::string model;    // ro.product.model
    std::string name;     // ro.product.name
    std::string board;    // ro.product.board
    std::string platform; // ro.board.platform

    int ion_heap;
    int rowsize;
    int ba2;
    int ba1;
    int ba0;
    int rank;
    
    
    int treshold;
    int measurements;
    int count;
    int fence;
    int cpu;

    int cpus;
    int slowest_cpu;
    int fastest_cpu;

    // 0x00 -> nope
    // 0x01 -> yes, normal addresses
    // 0x02 -> yes, ION chunks, start
    // 0x04 -> yes, ION chunks, middle
    int pagemap; 
#define PAGEMAP_UNAVAILABLE 0x00
#define PAGEMAP_NORMAL      0x01
#define PAGEMAP_ION_START   0x02
#define PAGEMAP_ION_MIDDLE  0x04
};

struct chipset {
    int ion_heap;
    std::string name;
};

class Histogram {
    public:
        Histogram(std::map<int, std::set<uint8_t *>> histogram);
    
        Histogram* getSubHistogram(std::set<uint8_t *> candidates); 

        int getMin();
        int getMax();
        int getMed();
        int getQ1();
        int getQ3();
        int getTreshold();
        int getVariation();
        bool isNormal();

        std::vector<int> deltas;
        std::vector<int> sorted_deltas();

        int countUp(int);
        int count();
        std::set<uint8_t *> getConflicts(int treshold);

        void print();
        std::map<int, std::set<uint8_t *>> histogram;

    private:
        int q1, median, q3;
        std::vector<int> variations;
};

class BankConflicts {
    public:
        BankConflicts(void);

        Histogram *verifyRowsize(uint8_t *base, int len, 
                            int   rowsize, 
                            int *_treshold);
        bool findRowsize(uint8_t *base, int len);
        bool findMask();
        bool verifyMask(uint8_t *base, int len, int rowsize, int treshold, uint32_t mask);
        Histogram *getAccessTimes(uint8_t *base, std::vector<uint8_t *> candidates, bool do_print, int timer = 0); 

        void getModel(int force_autodetect, struct model *m);

    private:

        void determineCount(uint8_t *base, int min_loop_count_us);

        struct model *our_model;
       
        int measurements;
        int count;
        int fence;

        std::vector<int> cpus;
};
   

#endif // __ROWSIZE_H__
