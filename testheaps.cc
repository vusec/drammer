#include "ionheap.h"
#include "logger.h"

Logger *logger;

int main(int argc, char *argv[]) {
    logger = new Logger("heaps.log", 0);
   
    int id = ION_detect_system_heap(); 
    printf("id: %d\n", id);
}
