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
#include <string>

#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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


#define VERSION "0.2"

#define HAMMER_READCOUNT 2000000

struct model device;

Logger *logger;



void usage(char *main_program) {
    fprintf(stderr,"Usage: %s [-a] [-A] [-c count] [-d seconds] [-f file] [-h] [-l seconds] [-r rounds] [-t timer]\n", main_program);
    fprintf(stderr,"   -a        : Run all pattern combinations\n");
    fprintf(stderr,"   -A        : Always autodetect\n");
    fprintf(stderr,"   -c count  : Number of memory accesses per hammer round (default is %d)\n",HAMMER_READCOUNT);
    fprintf(stderr,"   -d seconds: Number of seconds to run defrag (default is disabled)\n");
    fprintf(stderr,"   -f base   : Write output to this file (basename)\n"); 
    fprintf(stderr,"   -h        : This help\n");
    fprintf(stderr,"   -l seconds: Log rotation (new log file) after this many seconds (default is 0 = disabled)\n");
    fprintf(stderr,"   -r rounds : Number of rounds to hammer all chunks (default 1)\n");
    fprintf(stderr,"   -t seconds: Number of seconds to hammer (default is to hammer everything)\n");
}

void resetter(uint8_t *pattern) {
    for (int i = 0; i < MAX_ROWSIZE; i++) {
        pattern[i] = rand() % 255;
    }
}


int main(int argc, char *argv[]) {
    printf("______   ______ _______ _______ _______ _______  ______  \n");
    printf("|     \\ |_____/ |_____| |  |  | |  |  | |______ |_____/ \n");
    printf("|_____/ |    \\_ |     | |  |  | |  |  | |______ |    \\_\n");
    printf("Version: %s\n", VERSION);
    printf("\n");
    
/********** GETOPT **********/
    int timer = 0;
    int defrag_timer = 0;
    char *basename = NULL;
    int hammer_readcount = HAMMER_READCOUNT;
    int rounds = 1;
    bool all_patterns = false;
    bool always_autodetect = false;
    int log_rotate = 0;

    int c;
    opterr = 0;
    while ((c = getopt(argc, argv, "aAc:d:f:hl:r:t:")) != -1) {
        switch (c) {
            case 'a':
                all_patterns = true;
                break;
            case 'A':
                always_autodetect = true;
                break;
            case 'c':
                hammer_readcount = strtol(optarg, NULL, 10);
                break;
            case 'd':
                defrag_timer = strtol(optarg, NULL, 10);
                break;
            case 'f':
                basename = optarg;
                break;
            case 'h':
                usage(argv[0]);
                return 0;
            case 'l':
                log_rotate = strtol(optarg, NULL, 10);
                break;
            case 'r':
                rounds = strtol(optarg, NULL, 10);
                break;
            case 't':
                timer = strtol(optarg, NULL, 10);
                break;
            case '?':
                if (optopt == 'c' || optopt == 'd' || optopt == 'f' || 
                    optopt == 'l' || optopt == 'r' || optopt == 't') 
                    fprintf(stderr, "Option -%c requires an argument.\n", optopt);
                else if (isprint(optopt))
                    fprintf(stderr,"Unknown option `-%c'.\n", optopt);
                else
                    fprintf(stderr,"Unknown option character `\\x%x'.\n", optopt);
                usage(argv[0]);
                return 1;
            default:
                abort();
        }
    }

/********** MAIN **********/

    /*** OUTPUTFILE */
#define SECONDS_IN_DAY 86400
#define SECONDS_IN_HOUR 3600
    logger = new Logger(basename, log_rotate);


    /*** UNBLOCK SIGNALS */
    unblock_signals();
    
    /*** MODEL DETECTION (rowsize, bank selectors, treshold, ion heap, ...) */
    RS_autodetect(always_autodetect, &device);
    
    lprint("\n");
    lprint("=============================================================\n");
    lprint("        MAIN\n");
    lprint("=============================================================\n");

    /*** PIN CPU */
    pincpu(device.fastest_cpu);

    /*** DEFRAG MEMORY */
    if (defrag_timer) {
        printf("[MAIN] Defragment memory\n");
        defrag(defrag_timer, device.ion_heap); 
    }

    
    printf("[MAIN] Initializing patterns\n");
    /*                                               
     *                                                Chunk
     *                              Pattern name \      |     /---------- Aggressor 1 (-1 --> random)
     *                                           |      |     |     /---- Aggressor 2 (-1 --> random) */
    PatternCollection p000 = PatternCollection("000", 0x00, 0x00, 0x00); // AGGRESSIVE
    PatternCollection p001 = PatternCollection("001", 0x00, 0x00, 0xff);
    PatternCollection p010 = PatternCollection("010", 0x00, 0xff, 0x00);
    PatternCollection p011 = PatternCollection("011", 0x00, 0xff, 0xff); // default, AGGRESSIVE
    PatternCollection p100 = PatternCollection("100", 0xff, 0x00, 0x00); // default, AGGRESSIVE
    PatternCollection p101 = PatternCollection("101", 0xff, 0x00, 0xff);
    PatternCollection p110 = PatternCollection("101", 0xff, 0xff, 0x00);
    PatternCollection p111 = PatternCollection("111", 0xff, 0xff, 0xff); // AGGRESSIVE

    PatternCollection p00r = PatternCollection("00r", 0x00, 0x00,   -1);
    PatternCollection p0r0 = PatternCollection("0r0", 0x00,   -1, 0x00);
    PatternCollection p0rr = PatternCollection("0rr", 0x00,   -1,   -1);
    PatternCollection pr00 = PatternCollection("r00",   -1, 0x00, 0x00);
    PatternCollection pr0r = PatternCollection("r0r",   -1, 0x00,   -1);
    PatternCollection prr0 = PatternCollection("r0r",   -1,   -1, 0x00);
    PatternCollection prrr = PatternCollection("rrr",   -1,   -1,   -1); // RANDOM, AGGRESSIVE

    PatternCollection p11r = PatternCollection("11r", 0xff, 0xff,   -1);
    PatternCollection p1r1 = PatternCollection("1r1", 0xff,   -1, 0xff);
    PatternCollection p1rr = PatternCollection("1rr", 0xff,   -1,   -1);
    PatternCollection pr11 = PatternCollection("r11",   -1, 0xff, 0xff);
    PatternCollection pr1r = PatternCollection("r1r",   -1, 0xff,   -1);
    PatternCollection prr1 = PatternCollection("r1r",   -1,   -1, 0xff);

    std::vector<PatternCollection *> patterns;

//#define AGGRESSIVE_PATTERNS
//#define RANDOM_PATTERNS

    if (all_patterns) 
        patterns = {&p000, &p001, &p010, &p011, &p100, &p101, &p110, &p111, 
                           &p00r, &p0r0, &p0rr, &pr00, &pr0r, &prr0, &prrr,
                           &p11r, &p1r1, &p1rr, &pr11, &pr1r, &prr1};
    else
        patterns = {&p100};


#ifdef AGGRESSIVE_PATTERNS
    patterns = {&p000, &p011, &p100, &p111, &prrr};
#endif
#ifdef RANDOM_PATTERNS
    patterns = {&prrr};
#endif

    /*** TEMPLATE */
    printf("[MAIN] Start templating\n");
    TMPL_run(patterns, timer, hammer_readcount, rounds);
    printf("ok bye\n");
}
