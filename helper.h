#ifndef __HELPER_H__
#define __HELPER_H__

#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <algorithm>
#include <cmath>
#include <numeric>

#define G(x) (x << 30)
#define M(x) (x << 20)
#define K(x) (x << 10)

#define  B_TO_ORDER(x) (ffs(x / 4096)-1)
#define KB_TO_ORDER(x) (ffs(x / 4)-1)
#define MB_TO_ORDER(x) (ffs(x * 256)-1)

#define ORDER_TO_B(x)  ((1 << x) * 4096)
#define ORDER_TO_KB(x) ((1 << x) * 4)
#define ORDER_TO_MB(x) ((1 << x) / 256)

#define MAX_ORDER 10

#define BILLION 1000000000L
#define MILLION 1000000L

extern FILE *global_of;

static inline uint64_t get_ns(void) {
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return BILLION * (uint64_t) t.tv_sec + (uint64_t) t.tv_nsec;
}

static inline uint64_t get_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return MILLION * (uint64_t) tv.tv_sec + tv.tv_usec;
}

static int pagemap_fd = 0;
static bool got_pagemap = true;

static inline uintptr_t get_phys_addr(uintptr_t virtual_addr) {
    if (!got_pagemap) return 0;
    if (pagemap_fd == 0) {
        pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
        if (pagemap_fd < 0) {
            got_pagemap = false;
            return 0;
        }
    }
    uint64_t value;
    off_t offset = (virtual_addr / PAGESIZE) * sizeof(value);
    int got = pread(pagemap_fd, &value, sizeof(value), offset);
    assert(got == 8);
  
    // Check the "page present" flag.
    if ((value & (1ULL << 63)) == 0) {
        printf("page not present? virtual address: %p | value: %p\n", virtual_addr, value);
        return 0;
    }

    uint64_t frame_num = (value & ((1ULL << 54) - 1));
    return (frame_num * PAGESIZE) | (virtual_addr & (PAGESIZE-1));
}

static inline uint64_t compute_median(std::vector<uint64_t> &v) {
    if (v.size() == 0) return 0;
    std::vector<uint64_t> tmp = v;
    size_t n = tmp.size() / 2;
    std::nth_element(tmp.begin(), tmp.begin()+n, tmp.end());
    return tmp[n];
}

static inline void print(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stdout, format, args);
    if (global_of != NULL) vfprintf(global_of, format, args);
    va_end(args);
}

#endif // __HELPER_H__
