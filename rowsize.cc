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
#include <bitset>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>

#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "helper.h"
#include "ion.h"
#include "rowsize.h"
#include "ionheap.h"


#define CACHELINE_SIZE 64

#define MAX_TRIES         1


#define MEASUREMENTS      100
#define DEFAULT_LOOPCOUNT 10000 
#define DEFAULT_FENCE     FENCING_NONE
#define DEFAULT_CPU       0

#define RS_CHUNKSIZE K(256)

struct model unknown_model = {"Unknown model", "unknown", "unknown", "unknown", "unknown", -1, K( 64), 0x0000, 0x0000, 0x0000, 0x000};





void dump_hardware(struct model *m) {
    lprint("\n");
    lprint("=============================================================\n");
    lprint("        HARDWARE\n");
    lprint("=============================================================\n");
    lprint("[RS] Model:\n");
    lprint("[RS] - ro.product.model:  %s\n", m->model.c_str());
    lprint("[RS] - ro.product.name:   %s\n", m->name.c_str());
    lprint("[RS] - ro.product.board:  %s\n", m->board.c_str());
    lprint("[RS] - ro.board.platform: %s\n", m->platform.c_str());
    lprint("[RS] CPU:\n");
    lprint("[RS] - count:   %d\n", m->cpus);
    lprint("[RS] - fastest: %d\n", m->fastest_cpu);
    lprint("[RS] - slowest: %d\n", m->slowest_cpu);

    lprint("[RS] Contents of /proc/cpuinfo:\n");
    dumpfile("/proc/cpuinfo");

    lprint("[RS] Contents of /proc/version:\n");
    dumpfile("/proc/version");

    lprint("[RS] Content of /proc/sys/vm/overcommit_memory:\n");
    dumpfile("/proc/sys/vm/overcommit_memory");

    lprint("[RS] Content of /proc/meminfo:\n");
    dumpfile("/proc/meminfo");

    lprint("[RS] Content of /proc/pagetypeinfo:\n");
    dumpfile("/proc/pagetypeinfo");
    
    lprint("[RS] Content of /proc/zoneinfo:\n");
    dumpfile("/proc/zoneinfo");
    
    lprint("[RS] Content of /proc/buddyinfo:\n");
    dumpfile("/proc/buddyinfo");

    lprint("[RS] Output of ls -l /sys/kernel/mm:\n");
    lprint("%s",run("/system/bin/ls -l /sys/kernel/mm/").c_str());

    lprint("[RS] Output of ls -l /proc/self/pagemap:\n");
    lprint("%s",run("/system/bin/ls -l /proc/self/pagemap").c_str());

    lprint("[RS] Output of ls -l /sys/kernel/debug/ion:\n");
    lprint("%s",run("/system/bin/ls -l /sys/kernel/debug/ion").c_str());

    lprint("[RS] Output of ls -l /sys/kernel/debug/ion/heaps:\n");
    lprint("%s",run("/system/bin/ls -l /sys/kernel/debug/ion/heaps").c_str());

    lprint("[RS] Output of ls -l /proc/device-tree/soc/qcom,ion/:\n");
    lprint("%s",run("/system/bin/ls -l /proc/device-tree/soc/qcom,ion/").c_str());
//    find /proc/device-tree/soc/qcom,ion/ -name *heap-type* -exec echo {} \; -exec cat {} \;

    lprint("[RS] Output of ls -l /proc/dvice-tree/hisi,ion/:\n");
    lprint("%s",run("/system/bin/ls -l /proc/device-tree/hisi,ion/").c_str());

    lprint("\n");
}


void dump_settings(struct model *m) {
    lprint("\n");
    lprint("=============================================================\n");
    lprint("        HAMMER SETTINGS\n");
    lprint("=============================================================\n");
    lprint("[RS] ION heap:     %d\n", m->ion_heap);
    lprint("[RS] Rowsize:      %d\n", m->rowsize);
    lprint("[RS] ba2:          %x\n", m->ba2);
    lprint("[RS] ba1:          %x\n", m->ba1);
    lprint("[RS] ba0:          %x\n", m->ba0);
    lprint("[RS] rank:         %x\n", m->rank);

    lprint("\n");
}




void get_model(struct model *m) {
    lprint("\n");
    lprint("=============================================================\n");
    lprint("        SEARCHING FOR MODEL\n");
    lprint("=============================================================\n");
     
    memcpy(m, &unknown_model, sizeof(unknown_model));

    lprint("[BC] Collecting basic hardware info\n");
    m->model    = getprop("ro.product.model");
    m->name     = getprop("ro.product.name");
    m->board    = getprop("ro.product.board");
    m->platform = getprop("ro.board.platform");
    
    m->measurements = MEASUREMENTS;
    m->count        = DEFAULT_LOOPCOUNT;
    m->fence        = DEFAULT_FENCE;

    m->ba2 = 0x0000;
    m->ba1 = 0x0000;
    m->ba0 = 0x0000;
    m->rank = 0x0000;

    m->cpus = -1;
    m->slowest_cpu = -1;
    m->fastest_cpu = -1;

    m->ion_heap = ION_detect_system_heap();
    m->use_contig_heap = false;

    m->pagemap = 0x0;

    lprint("[BC] Collecting CPU info\n");
    m->cpus = getcpus(&m->slowest_cpu, &m->fastest_cpu);
    
    return;
}




/***********************************************************
 * MAIN ENTRY POINT FOR AUTO DETECT 
 ***********************************************************/
int RS_autodetect(struct model *our_model) {
    get_model(our_model);
    dump_hardware(our_model);
    dump_settings(our_model);

    return 0;
}
