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


bool stop_defrag;

void signal_handler(int signal) {
    if (signal == SIGALRM) {
        printf("[SIGNALRM] Time is up\n");
    } else if (signal == SIGUSR1) {
        printf("[SIGUSR1] OOM-killer\n");
    }
    stop_defrag = true;
}




int ionExhaust(std::vector<struct ion_data *> &chunks, int min_bytes, int heap_id, bool mmap) { 
    int total_kb;

    total_kb = 0;
    for (int order = MAX_ORDER; order >= B_TO_ORDER(min_bytes); order--) {
        int count = ION_bulk(ORDER_TO_B(order), chunks, heap_id, 0, mmap);
        lprint("[EXHAUST] - order %2d (%4d KB) - got %3d chunks\n", 
                    order, ORDER_TO_KB(order), count);
        total_kb += ORDER_TO_KB(order) * count;
    }
    lprint("[EXHAUST] allocated %d KB (%d MB)\n", total_kb, total_kb / 1024);

    return total_kb;
}




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
 * - a timeout occurs (after n seconds); or
 * - we did not get many new blocks during the last x seconds (INTERAL /
 *   MINCOUNT)
 */
int defrag(int timer, int heap_id) {
    std::vector<struct ion_data *> defrag_chunks;
    time_t start_time = 0;
    time_t  prev_time = 0;
    int      count = 0;
    int prev_count = 0;
    int    alloc_count[INTERVAL];
    for (int i = 0; i < INTERVAL; i++) alloc_count[i] = MIN_COUNT + 1;
    int    alloc_count_index = 0;
    int len = K(4);
    stop_defrag = false;


    /* Exhaust */
    ionExhaust(defrag_chunks, K(64), heap_id, false);

    int fixed_size_ion = 0;

    /* Install one signal handler for:
     * - SIGALRM: if our timer runs out
     * - SIGUSR1: if the system is on low-memory (detected and sent by our app) */
    struct sigaction new_action, old_ALARM, old_USR1;
    new_action.sa_handler = signal_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction(SIGALRM, &new_action, &old_ALARM);
    sigaction(SIGUSR1, &new_action, &old_USR1);
    alarm(timer);

    /* Let's go! */ 
    start_time = time(NULL);
    while (true) {
        struct ion_data *data = new ion_data;
        if (data == NULL) {
            perror("Could not allocate memory");
            exit(EXIT_FAILURE);
        }
        data->handle = ION_alloc(len, heap_id);
        if (data->handle == 0) {
            printf("Could not allocate 4KB. ION heap may be of a fixed-size\n");
            fixed_size_ion = 1;
            break;
        }
        data->len = len;
        data->mapping = NULL;
        defrag_chunks.push_back(data);
        count++;

        time_t curr_time = time(NULL);
        if (curr_time != prev_time) {
            int timerunning = (curr_time - start_time);
            int timeleft = timer - timerunning;

            alloc_count[alloc_count_index] = (count - prev_count);
            alloc_count_index = (alloc_count_index + 1) % INTERVAL;
            bool progress = false;
            lprint("[DEFRAG] Blocks allocated last %d intervals: ", INTERVAL);
            for (int i = INTERVAL-1; i >= 0; i--) {
                printf("%5d ", alloc_count[(alloc_count_index + i) % INTERVAL]);
                if (alloc_count[i] > MIN_COUNT) 
                    progress = true;
            }
            lprint(" | time left: %3d | blocks: %8d\n", 
                        timeleft, count);

            if (!progress) { 
                lprint("[DEFRAG] Not enough progress\n");
                break;
            }
           
            prev_count = count;
            prev_time = curr_time;
        }

        if (stop_defrag) {
            lprint("[DEFRAG] Signal received\n");
            break;
        }
    }
   
    lprint("[DEFRAG] Additionally got %d chunks of size %d KB (%d bytes in total = %d MB)\n", 
                 count,           len,   count * len,        count * len / 1024 / 1024);

    ION_clean_all(defrag_chunks);
   
    alarm(0);
    sigaction(SIGALRM, &old_ALARM, NULL);
    sigaction(SIGUSR1, &old_USR1, NULL);
    
    printf("[DEFRAG] Dumping /proc/pagetypeinfo\n");
    std::ifstream pagetypeinfo("/proc/pagetypeinfo");
    pagetypeinfo.clear();
    pagetypeinfo.seekg(0, std::ios::beg);
    for (std::string line; getline(pagetypeinfo, line); ) {
        if (!line.empty()) lprint("%s\n", line.c_str());
    }

    return fixed_size_ion;
}
