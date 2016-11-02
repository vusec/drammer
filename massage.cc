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

#include <algorithm>
#include <fstream>
#include <iostream>
#include <set>

#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "helper.h"
#include "ion.h"
#include "massage.h"
#include "rowsize.h"
#include "templating.h"

extern bool lowmem;

bool alloc_timeout;
void alloc_alarm(int signal) {
    printf("Allocation timeout\n");
    alloc_timeout = true;
}

std::ifstream meminfo("/proc/meminfo");
size_t read_meminfo(std::string type) {
    meminfo.clear();
    meminfo.seekg(0, std::ios::beg);
    for (std::string line; getline(meminfo, line); ) {
        if (line.find(type) != std::string::npos) {
            std::string kb = line.substr( line.find(':') + 1, line.length() - type.length() - 3 );
            return std::atoi(kb.c_str());
        }
    }
    return 0;
}
size_t get_LowFree(void) { return read_meminfo("LowFree"); }

int exhaust(std::vector<struct ion_data *> &chunks, int min_bytes, bool mmap) { 
    int total_kb;

    total_kb = 0;
    for (int order = MAX_ORDER; order >= B_TO_ORDER(min_bytes); order--) {
        int count = ION_bulk(ORDER_TO_B(order), chunks, 0, mmap);
        print("[EXHAUST] - order %2d (%4d KB) - got %3d chunks\n", 
                    order, ORDER_TO_KB(order), count);
        total_kb += ORDER_TO_KB(order) * count;

        if (lowmem) break;
    }
    print("[EXHAUST] allocated %d KB (%d MB)\n", total_kb, total_kb / 1024);

    return total_kb;
}




/* stop defrag when it has been working for more than ALLOC_TIMEOUT seconds */
#define ALLOC_TIMEOUT 10

/* stop defrag when the system has less than MIN_LOWFREE KB low memory left */
#define MIN_LOWFREE 4 * 1024

/* stop defrag when none of the last <INTERVAL> allocations yield more than MIN_COUNT blocks */
#define INTERVAL 10
#define MIN_COUNT 10 

/* The goal of defrag() is to trick the system into reserving more 'ION memory'
 * that we can allocate when we start templating. We do this by exhausting all
 * 4K ION chunks, resulting in the low memory killer killing background
 * processes and moving cached memory into a pool that can be used for ION
 * allocations.
 *
 * We first exhaust all contiguous chunks of size 64KB and up, to ensure that
 * background processes are already forced to use smaller contiguous memory
 * chunks (up to 32KB). Since we cannot simply exhaust *all* 4KB chunks (we
 * would go completely out of memory), we then allocate chunks until:
 * - a timeout occurs (after ALLOC_TIMEOUT seconds); or
 * - the system has little free low memory left (MIN_LOWFREE KB); or
 * - we did not get many new blocks during the last x seconds (INTERAL /
 *   MINCOUNT)
 */
void defrag(int alloc_timer) {
    std::vector<struct ion_data *> defrag_chunks;
    
    time_t start_time = 0;
    time_t  prev_time = 0;
    int      count = 0;
    int prev_count = 0;
    int    alloc_count[INTERVAL];
    for (int i = 0; i < INTERVAL; i++) alloc_count[i] = MIN_COUNT + 1;
    int    alloc_count_index = 0;
    int len = K(4);
    
    exhaust(defrag_chunks, K(64), false);

    if (lowmem) goto bail;

    alloc_timeout = false;
    signal(SIGALRM, alloc_alarm);
    alarm(alloc_timer);
    
    start_time = time(NULL);

    while (true) {
        struct ion_data *data = new ion_data;
        if (data == NULL) {
            perror("Could not allocate memory");
            exit(EXIT_FAILURE);
        }
        data->handle = ION_alloc(len);
        if (data->handle == 0) {
            printf("Exhausted *all* memory?\n");
            break;
//          exit(EXIT_FAILURE);
        }
        data->len = len;
        data->mapping = NULL;
        count++;

        time_t curr_time = time(NULL);
        if (curr_time != prev_time) {
            int lowfree = get_LowFree();
            int timerunning = (curr_time - start_time);
            int timeleft = alloc_timer - timerunning;

            alloc_count[alloc_count_index] = (count - prev_count);
            alloc_count_index = (alloc_count_index + 1) % 10;
            bool progress = false;
            print("[DEFRAG] Blocks allocated last %d intervals: ", 10);
            for (int i = 9; i >= 0; i--) {
                printf("%5d ", alloc_count[(alloc_count_index + i) % 10]);
                if (alloc_count[i] > MIN_COUNT) 
                    progress = true;
            }
            print(" | time left: %3d | low free: %8d KB | blocks: %8d\n", 
                        timeleft, lowfree, count);

            if (!progress) { 
                print("[DEFRAG] Not enough progress\n");
                break;
            }
           
            // some devices do not report LowFree in /proc/meminfo
            if (lowfree > 0 && lowfree < MIN_LOWFREE) {
                print("[DEFRAG] Not enough low memory\n");
                break;
            }

            if (alloc_timeout) {
                print("[DEFRAG] Timeout\n");
                break;
            }
            
            prev_count = count;
            prev_time = curr_time;
        }
        defrag_chunks.push_back(data);
    }
   
    print("[DEFRAG] Additionally got %d chunks of size %d KB (%d bytes in total = %d MB)\n", 
                 count,           len,   count * len,        count * len / 1024 / 1024);

bail:
    ION_clean_all(defrag_chunks);
    
    printf("[DEFRAG] Dumping /proc/pagetypeinfo\n");
    std::ifstream pagetypeinfo("/proc/pagetypeinfo");
    pagetypeinfo.clear();
    pagetypeinfo.seekg(0, std::ios::beg);
    for (std::string line; getline(pagetypeinfo, line); ) {
        if (!line.empty()) print("%s\n", line.c_str());
    }
}
