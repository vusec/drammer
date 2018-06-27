#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

int main(int argc, char *argv[]) {

    size_t len = 1024*1024*1024;

    printf("allocating %zu bytes with mmap\n", len);
    void *p1 = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_ANON|MAP_PRIVATE,-1, 0);
    if (p1 == MAP_FAILED) {
        perror("Could not mmap");
    } else {
        printf("p1: %p\n", p1);
        memset(p1, 0x42, len);
    }

    


/*
    len = 100*1024*1024;

    printf("allocating %zu bytes with mmap\n", len);
    void *p2 = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_ANON|MAP_PRIVATE,-1, 0);
    if (p2 == MAP_FAILED) {
        perror("Could not mmap");
    } else {
        printf("p2: %p\n", p2);
    }



    len = 10*1024*1024;

    printf("allocating %zu bytes with mmap\n", len);
    void *p3 = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_ANON|MAP_PRIVATE,-1, 0);
    if (p3 == MAP_FAILED) {
        perror("Could not mmap");
    } else {
        printf("p3: %p\n", p3);
    }

*/
    printf("kthxbye: %d\n",getpid());

}
