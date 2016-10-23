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

    std::vector<uintptr_t> hammerable_rows;
};



ion_user_handle_t ION_alloc(int len, int heap_id = -1);
int  ION_share(ion_user_handle_t handle); 
int  ION_free (ion_user_handle_t handle);

int  ION_mmap (struct ion_data *data, int prot = -1, int flags = -1, void *addr = NULL);
void ION_clean(struct ion_data *data);
int  ION_bulk(int len, std::vector<struct ion_data *> &chunks, int max = 0, bool mmap = true);
void ION_clean_all(    std::vector<struct ion_data *> &chunks, int max = 0);
void ION_get_hammerable_rows(struct ion_data *chunk);

void ION_detector(void);
void ION_init(void);
void ION_fini(void);

#endif
