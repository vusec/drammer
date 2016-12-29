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
#include "massage.h"
#include "rowsize.h"

int ion_fd;
extern struct model device;

/**********************************************
 * Core ION wrappers
 **********************************************/
ion_user_handle_t ION_alloc(int len, int heap_id) {
    struct ion_allocation_data allocation_data;

    if (heap_id == -1) 
        allocation_data.heap_id_mask = (0x1 << device.ion_heap);
    else 
        allocation_data.heap_id_mask = (0x1 << heap_id);
    
    allocation_data.flags = 0;
    allocation_data.align = 0;
    allocation_data.len = len;
    
    if (!ion_fd) 
        ION_init();

    if (ioctl(ion_fd, ION_IOC_ALLOC, &allocation_data))
        return 0;

    return allocation_data.handle;
}
int ION_share(ion_user_handle_t handle) {
    struct ion_fd_data fd_data;
    fd_data.handle = handle;
    
    if (ioctl(ion_fd, ION_IOC_SHARE, &fd_data))
        return -1;

    return fd_data.fd;
}
int ION_free(ion_user_handle_t handle) {
    struct ion_handle_data handle_data;
    handle_data.handle = handle;

    if (ioctl(ion_fd, ION_IOC_FREE, &handle_data))
        return -1;

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
    if (flags == -1) flags = MAP_SHARED;

    data->mapping = mmap(addr, data->len, prot, flags, data->fd, 0);
    if (data->mapping == MAP_FAILED) {
        perror("Could not mmap");
        exit(EXIT_FAILURE);
    }
    data->virt = (uintptr_t) data->mapping;

    if (device.pagemap & PAGEMAP_ION_START) 
        data->phys = get_phys_addr(data->virt);

    return 0;
}

int ION_alloc_mmap(struct ion_data *data, int len, int id) {
    data->handle = 0;

    int MAX_TRIES = 10;
    
    for (int tries = 0; tries < MAX_TRIES; tries++) {
        lprint("[ION] Trying to allocate %d bytes (try %d/%d) with id %d\n", len, tries, MAX_TRIES, id);
        data->handle = ION_alloc(len, id);
        if (data->handle != 0) {
            data->len = len;
            return ION_mmap(data);
        }
        lprint("[ION] Could not allocate chunk: %s\n",strerror(errno));
        lprint("[ION] Running defrag(%d)\n", tries);
        if (defrag(tries+1, id)) 
            break;
    }
    return -1;
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
int ION_bulk(int len, std::vector<struct ion_data *> &chunks, int heap_id, int max, bool mmap) {

    int count = 0;
    while (true) {
        struct ion_data *data = new ion_data;
        if (data == NULL) {
            perror("Could not malloc");
            exit(EXIT_FAILURE);
        }

        data->handle = ION_alloc(len, heap_id);
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
    
        
        chunks.push_back(data);
        count++;
        if (max > 0 && count >= max) break;
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
 * Initialize and finalize /dev/ion
 **********************************************/
void ION_init(void) {
    ion_fd = open("/dev/ion", O_RDONLY);
    if (!ion_fd) {
        perror("Could not open /dev/ion");
        exit(EXIT_FAILURE);
    }
}

