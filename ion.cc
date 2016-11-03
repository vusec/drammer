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
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <map>
#include <numeric>
#include <sstream>
#include <vector>

#include <linux/ion.h>

#include "helper.h"
#include "ion.h"

int chipset;
#define CHIPSET_MSM         21
#define CHIPSET_MEDIATEK    1
#define CHIPSET_EXYNOS      4
#define CHIPSET_MAKO        25
#define CHIPSET_TEGRA       2
#define CHIPSET_UNIVERSAL   1
#define CHIPSET_KIRIN       1 
#define CHIPSET_SPREADTRUM  2
#define CHIPSET_QCT         22

int ion_fd;
extern int rowsize;

/**********************************************
 * Core ION wrappers
 **********************************************/
ion_user_handle_t ION_alloc(int len, int heap_id) {
    if (heap_id == -1 && len > M(4)) return 0;
    struct ion_allocation_data allocation_data;

    if (heap_id == -1) {
        allocation_data.heap_id_mask = (0x1 << chipset);
    } else {
        allocation_data.heap_id_mask = (0x1 << heap_id);
    }
    allocation_data.flags = 0;
    allocation_data.align = 0;
    allocation_data.len = len;
    int err = ioctl(ion_fd, ION_IOC_ALLOC, &allocation_data);
    if (err) return 0;
    return allocation_data.handle;
}
int ION_share(ion_user_handle_t handle) {
    struct ion_fd_data fd_data;
    fd_data.handle = handle;
    int err = ioctl(ion_fd, ION_IOC_SHARE, &fd_data);
    if (err) return -1;
    return fd_data.fd;
}
int ION_free(ion_user_handle_t handle) {
    struct ion_handle_data handle_data;
    handle_data.handle = handle;
    int err = ioctl(ion_fd, ION_IOC_FREE, &handle_data);
    if (err) return -1;
    return 0;
}

/**********************************************
 * Mmap a struct ion_data
 **********************************************/
int ION_mmap(struct ion_data *data, int prot, int flags, void *addr) {
    data->fd = ION_share(data->handle);
    if (data->fd < 0) {
        perror("Could not share");
        return -1;
        //exit(EXIT_FAILURE);
    }
    
    if ( prot == -1)  prot =  PROT_READ | PROT_WRITE;
    if (flags == -1) flags = MAP_SHARED | MAP_POPULATE;

    data->mapping = mmap(addr, data->len, prot, flags, data->fd, 0);
    if (data->mapping == MAP_FAILED) {
        perror("Could not mmap");
        exit(EXIT_FAILURE);
    }

    return 0;
}

/**********************************************
 * Free a struct ion_data 
 **********************************************/
void ION_clean(struct ion_data *data) {
    if (data->mapping) {
        if (munmap(data->mapping, data->len)) {
            perror("Could not munmap");
            exit(EXIT_FAILURE);
        }
        data->mapping = NULL;

        if (close(data->fd)) {
            perror("Could not close");
            exit(EXIT_FAILURE);
        }
    }

    if (data->handle) {
        if (ION_free(data->handle)) {
            perror("Could not free");
            exit(EXIT_FAILURE);
        }
        data->handle = 0;
    }
}

/**********************************************
 * Allocate ION chunks in bulk 
 **********************************************/

/* Our java app will send a SIGUSR1 signal if the system is low on memory. This
 * probably requires a bit more debugging... */

bool lowmem;
void lowmem_handler(int signal) {
    print("LOW MEMORY!\n");
    lowmem = true;
}

int ION_bulk(int len, std::vector<struct ion_data *> &chunks, int max, bool mmap) {
    lowmem = false;
    signal(SIGUSR1, lowmem_handler);

    int count = 0;
    while (true) {
        struct ion_data *data = new ion_data;
        if (data == NULL) {
            perror("Could not malloc");
            exit(EXIT_FAILURE);
        }

        data->handle = ION_alloc(len);
        if (data->handle == 0) {
            /* Could not allocate, probably exhausted the ion chunks */
            free(data);
            break;
        }
        data->len = len;

        if (mmap) {
            int ret = ION_mmap(data);
            if (ret < 0) {
                break;
            }
        } else {
            data->mapping = NULL;
        }
    
        data->hammerable_rows.clear();
        
        chunks.push_back(data);
        count++;
        if (max > 0 && count >= max) break;

        if (lowmem) break;
    }
    return count;
}

/**********************************************
 * Clean a vector of struct ion_data
 **********************************************/
void ION_clean_all(std::vector<struct ion_data *> &chunks, int max) {
    if (!max) max = chunks.size();
    for (int i = 0; i < max; i++) {
        ION_clean(chunks[i]);
        delete(chunks[i]);
    }
    chunks.erase(chunks.begin(), chunks.begin() + max); // remove first <max> elements
}

/**********************************************
 * Populate a vector of virtual address that we can hammer
 **********************************************/
void ION_get_hammerable_rows(struct ion_data * chunk) {
    if (chunk->len < (3*rowsize)) return;
    if (chunk->mapping == NULL) return;
    for (int offset = rowsize; 
             offset < chunk->len - rowsize; 
             offset += rowsize) {
        uintptr_t virt_row = (uintptr_t) chunk->mapping + offset;
        chunk->hammerable_rows.push_back(virt_row);
    }
}


/**********************************************
 * Initialize and finalize /dev/ion
 **********************************************/
void ION_init(void) {
    // get chipset
    chipset = CHIPSET_MSM;
    std::ifstream cpuinfo("/proc/cpuinfo");
    for (std::string line; getline(cpuinfo, line); ) {
        if (line.find("Qualcomm") != std::string::npos) {
            print("Detected chipset: Qualcomm\n");
            chipset = CHIPSET_MSM;
            break;
        }   
        if (line.find("Exynos") != std::string::npos) {
            print("Detected chipset: Exynos\n");
            chipset = CHIPSET_EXYNOS;
            break;
        }
        if (line.find(": 0x53") != std::string::npos) {
            print("Detected chipset: Exynos\n"); // S7, S7 Edge, but probably more :(
            chipset = CHIPSET_EXYNOS;
            break;
        }
        if (line.find(": sc") != std::string::npos) {
            // Hardware : sc8830
            print("Detected chipset: Spreadtrum\n");
            chipset = CHIPSET_SPREADTRUM;
            break;
        }
        if (line.find("EXYNOS") != std::string::npos) {
            // Samsung EXYNOS5433
            print("Detected chipset: Exynos\n");
            chipset = CHIPSET_EXYNOS;
            break;
        }
        if (line.find("UNIVERSAL") != std::string::npos) {
            print("Detected chipset: UNIVERSAL\n");
            chipset = CHIPSET_UNIVERSAL;
            break;
        }
        if (line.find("MAKO") != std::string::npos) {
            print("Detected chipset: Mako\n");
            chipset = CHIPSET_MAKO;
            break;
        }
        if (line.find("Flounder") != std::string::npos) {
            print("Detected chipset: Tegra\n");
            chipset = CHIPSET_TEGRA;
            break;
        }
        if (line.find(": MT") != std::string::npos) {
            print("Detected chipset: Mediatek\n");
            chipset = CHIPSET_MEDIATEK;
            break;
        }
        if (line.find(": hi") != std::string::npos) {
            print("Detected chipset Kirin\n");
            chipset = CHIPSET_KIRIN;
            break;
        }
        if (line.find("Kirin") != std::string::npos) {
            print("Detected chipset Kirin\n");
            chipset = CHIPSET_KIRIN;
            break;
        }
        if (line.find("MSM8627") != std::string::npos) {
            print("Detected cihpset MSM8627\n");
            chipset = CHIPSET_QCT;
        }
    }
    
    ion_fd = open("/dev/ion", O_RDONLY);
    if (!ion_fd) {
        perror("Could not open ion");
        exit(EXIT_FAILURE);
    }
    
    int err;
    sigset_t sigset;
    
    err = sigfillset(&sigset);
    if (err != 0) perror("sigfillset");
    
    err = sigprocmask(SIG_UNBLOCK, &sigset, NULL); 
    if (err != 0) perror("sigprocmask");
    
    setvbuf(stdout, NULL, _IONBF, 0);
}
void ION_fini(void) {
    close(ion_fd);
}




void ION_detector(void) {
    for (int i = 0; i < 32; i++) {
        uint32_t mask = 0x1 << i;
        printf("Trying to allocate  4KB with heap id: %2d | mask: %8x ", i, mask);

        struct ion_handle_data handle_data;
        struct ion_allocation_data allocation_data;
        allocation_data.flags = 0;
        allocation_data.align = 0;
        allocation_data.len = K(4);
        allocation_data.heap_id_mask = mask;
        int err = ioctl(ion_fd, ION_IOC_ALLOC, &allocation_data);
        if (err) {
            printf(" -> nope (%s)\n", strerror(errno));
            continue;
        } 
        printf(" -> ok!\n");
        handle_data.handle = allocation_data.handle;
        err = ioctl(ion_fd, ION_IOC_FREE, &handle_data);
        if (err) {
            printf(" -> could not free (%s)\n", strerror(errno));
            continue;
        }

        printf("...... to allocate  4MB with heap id: %2d | mask: %8x ", i, mask);
        allocation_data.len = M(4);
        err = ioctl(ion_fd, ION_IOC_ALLOC, &allocation_data);
        if (err) {
            printf(" -> nope (%s)\n", strerror(errno));
            continue;
        } 
        printf(" -> ok!\n");
        handle_data.handle = allocation_data.handle;
        err = ioctl(ion_fd, ION_IOC_FREE, &handle_data);
        if (err) {
            printf(" -> could not free (%s)\n", strerror(errno));
            continue;
        }
        
        printf("...... to allocate 16MB with heap id: %2d | mask: %8x ", i, mask);
        allocation_data.len = M(16);
        err = ioctl(ion_fd, ION_IOC_ALLOC, &allocation_data);
        if (err) {
            printf(" -> nope (%s)\n", strerror(errno));
            continue;
        } 
        printf(" -> ok!\n");
        handle_data.handle = allocation_data.handle;
        err = ioctl(ion_fd, ION_IOC_FREE, &handle_data);
        if (err) {
            printf(" -> could not free (%s)\n", strerror(errno));
            continue;
        }
    }
}
