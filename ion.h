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


#ifndef __ION_H__
#define __ION_H__

#include <map>
#include <numeric>
#include <set>
#include <vector>

#include <linux/ion.h>
#include <strings.h>


struct ion_data {
    ion_user_handle_t handle;
    int fd, len;
    void *mapping = NULL;
    uintptr_t virt = 0;
    uintptr_t phys = 0;
};



ion_user_handle_t ION_alloc(int len, int heap_id = -1);
int  ION_share(ion_user_handle_t handle); 
int  ION_free (ion_user_handle_t handle);

int  ION_mmap (struct ion_data *data, int prot = -1, int flags = -1, void *addr = NULL);
int  ION_alloc_mmap(struct ion_data *data, int len, int id);
void ION_clean(struct ion_data *data);
int  ION_bulk(int len, std::vector<struct ion_data *> &chunks, int heap_id, int max = 0, bool mmap = true);
void ION_clean_all(    std::vector<struct ion_data *> &chunks, int max = 0);
void ION_get_hammerable_rows(struct ion_data *chunk);

void ION_detector(void);
void ION_init(void);
void ION_fini(void);

#endif
