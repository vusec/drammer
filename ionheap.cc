
#include "helper.h"


#include <algorithm>
#include <assert.h>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <linux/ion.h>
#include <map>
#include <numeric>
#include <set>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sstream>
#include <unistd.h>
#include <vector>

#include "ionheap.h"

int my_ion_fd;

int ION_detect_system_heap(void) {
    my_ion_fd = open("/dev/ion", O_RDONLY);
    if (!my_ion_fd) {
        perror("Could not open /dev/ion");
        exit(EXIT_FAILURE);
    }

    std::map<int, std::string> heaps;
    heaps = ION_detect_heaps();

    std::vector<int> ids;

    lprint("============================================\n");
    
    for (auto &it: heaps) {
        int id = it.first;
        std::string heap = it.second;
        
        /* we should be able to allocate 128MB on the system heap */
        struct ion_allocation_data allocation_data;
        allocation_data.heap_id_mask = 0x1 << id;
        allocation_data.len = M(128);
        allocation_data.flags = 0;
        allocation_data.align = 0;
        allocation_data.handle = 0;
        lprint("\n");
        lprint("[ION] allocation_data.heap_id_mask: %x\n", allocation_data.heap_id_mask);
        lprint("[ION] allocation_data.flags: %x\n", allocation_data.flags);
        lprint("[ION] allocation_data.align: %x\n", allocation_data.align);
        lprint("[ION] allocation_data.handle: %p\n", allocation_data.handle);
        lprint("%s\n", heap.c_str());
        lprint("[ION] ION_IOC_ALLOC for 128MB... ");
        int err = ioctl(my_ion_fd, ION_IOC_ALLOC, &allocation_data);
        if (err) {
            lprint("Failed: %s\n", strerror(errno));
            continue;
        } 
        lprint("Success\n");

        /* free */
        struct ion_handle_data handle_data;
        handle_data.handle = allocation_data.handle;
        lprint("\n");
        lprint("[ION] ION_IOC_FREE... ");
        err = ioctl(my_ion_fd, ION_IOC_FREE, &handle_data);
        if (err) {
            lprint("Failed: %s\n", strerror(errno));
            continue;
        }
        lprint("Success\n");
        lprint("\n");

        ids.push_back(id);
    }

    lprint("-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-\n");

    close(my_ion_fd);

    if (ids.size() == 0) {
        lprint("no suitable heap found (low on memory maybe?)\n");
        exit(EXIT_FAILURE);
    }
    if (ids.size() == 1) {
        lprint("found one candidate: %d --> %s\n", ids[0], heaps[ids[0]].c_str());
        return ids[0];
    }

    for (auto id: ids) {
        lprint("remaining id: %d --> %s\n", id, heaps[id].c_str());
    }

    for (auto id: ids) {
        if (id == 30) {
            lprint("assuming msm still uses heap id 30 for vmalloc\n");
            return 30;
        }
    }
    for (auto id: ids) {
        if (id == 25) {
            lprint("assuming msm still uses heap id 25 for system\n");
            return 25;
        }
    }

    return ids[0];

}

std::map<int, std::string> ION_detect_heaps(void) {
    std::map<int, std::string> heaps;

    std::vector<int> ids;
    ids = ION_detect_heap_ids();

	std::stringstream ss;
	ss << getpid();
	std::string pid = ss.str();


    lprint("========================================================\n");
    lprint("our pid: %s\n", pid.c_str());

#define ION_BUFFERS "/sys/kernel/debug/ion/buffer"
#define ION_DEBUG "/sys/kernel/debug/ion/"



	    for (auto id: ids) {
	        lprint("[ION] heap: %d\n", id);
        
    		struct ion_allocation_data allocation_data;
			allocation_data.heap_id_mask = 0x1 << id;
	        allocation_data.len = K(4);
            allocation_data.flags = 0;
            allocation_data.align = 0;
            allocation_data.handle = 0;
            lprint("[ION] allocation_data.heap_id_mask: %x\n", allocation_data.heap_id_mask);
            lprint("[ION] allocation_data.flags: %x\n", allocation_data.flags);
            lprint("[ION] allocation_data.align: %x\n", allocation_data.align);
            lprint("[ION] allocation_data.handle: %p\n", allocation_data.handle);
        	lprint("[ION] ION_IOC_ALLOC... ");
			int err = ioctl(my_ion_fd, ION_IOC_ALLOC, &allocation_data);
	        if (err) {
	            lprint("Failed: %s\n", strerror(errno));
	            continue;
	        } 
	        lprint("Success\n");

    		std::ifstream ionbuffers(ION_BUFFERS);
		    ionbuffers.clear();
            ionbuffers.seekg(0, std::ios::beg);
            for (std::string line; getline(ionbuffers, line); ) {
                if (line.find(pid) != std::string::npos) {
                    lprint("%s\n", line.c_str());

					std::transform(line.begin(), line.end(), line.begin(), ::tolower);
					heaps[id] = line;
                }
            }
			if (heaps.count(id) == 0) {
                std::cout << "opening " ION_DEBUG + pid << " now!\n";
                std::ifstream ionbuffers(ION_DEBUG + pid);
                ionbuffers.clear();
                ionbuffers.seekg(0, std::ios::beg);

                // skip first line
                std::string skip; getline(ionbuffers, skip);

                for (std::string line; getline(ionbuffers, line); ) {
                        lprint("%s\n", line.c_str());

                        std::transform(line.begin(), line.end(), line.begin(), ::tolower);
                        heaps[id] = line;
                }
            }
            if (heaps.count(id) == 0) {
				heaps[id] = "unknown";
            }	

        	/* free */
            struct ion_handle_data handle_data;
	        handle_data.handle = allocation_data.handle;
	        lprint("\n");
	        lprint("[ION] ION_IOC_FREE... ");
	        err = ioctl(my_ion_fd, ION_IOC_FREE, &handle_data);
	        if (err) {
    	        lprint("Failed: %s\n", strerror(errno));
	            continue;
	        }
	        lprint("Success\n");
	        lprint("\n");
        }
    return heaps;
}

/***********************************************************
 * ION HEAP IDS DETECTOR 
 ***********************************************************/
std::vector<int> ION_detect_heap_ids(void) {
    std::vector<int> ids;

        
        struct ion_allocation_data allocation_data;
    struct ion_fd_data fd_data;
        struct ion_handle_data handle_data;

    for (int id = 0; id < 32; id++) {
        int err;


        /* try to allocate 4KB for heap id <id> to figure out if this heap even exists */ 
        allocation_data.heap_id_mask = 0x1 << id;
        allocation_data.len = K(4);
        allocation_data.flags = 0;
        allocation_data.align = 0;
        allocation_data.handle = 0;
        lprint("\n");
        lprint("[ION] allocation_data.heap_id_mask: %x\n", allocation_data.heap_id_mask);
        lprint("[ION] allocation_data.flags: %x\n", allocation_data.flags);
        lprint("[ION] allocation_data.align: %x\n", allocation_data.align);
        lprint("[ION] allocation_data.handle: %p\n", allocation_data.handle);
        lprint("[ION] ION_IOC_ALLOC... ");
        err = ioctl(my_ion_fd, ION_IOC_ALLOC, &allocation_data);
        if (err) {
            lprint("Failed: %s\n", strerror(errno));
            continue;
        } 
        lprint("Success\n");
        lprint("[ION] allocation_data.heap_id_mask: %x\n", allocation_data.heap_id_mask);
        lprint("[ION] allocation_data.flags: %x\n", allocation_data.flags);
        lprint("[ION] allocation_data.align: %x\n", allocation_data.align);
        lprint("[ION] allocation_data.handle: %p\n", allocation_data.handle);
        lprint("\n");

        /* try to share */
        fd_data.fd = 0;
        fd_data.handle = allocation_data.handle;
        lprint("\n");
        lprint("[ION] fd_data.handle: %p\n", fd_data.handle);
        lprint("[ION] fd_data.fd: %d\n", fd_data.fd);
        lprint("[ION] ION_IOC_SHARE... ");
        err = ioctl(my_ion_fd, ION_IOC_SHARE, &fd_data);
        if (err) {
            lprint("Failed: %s\n", strerror(errno));
            continue;
        }
        lprint("Success\n");
        lprint("[ION] fd_data.handle: %p\n", fd_data.handle);
        lprint("[ION] fd_data.fd: %d\n", fd_data.fd);
        lprint("\n");

        /* try to mmap */
        lprint("\n");
        lprint("[ION] mmap... ");
        void *p = mmap(NULL, allocation_data.len, PROT_READ | PROT_WRITE, MAP_SHARED, fd_data.fd, 0);
        if (p == MAP_FAILED) {
            lprint("Failed: %s\n", strerror(errno));
            continue;
        }
        lprint("Success\n");
        lprint("\n");


        /* unmap */
        munmap(p, allocation_data.len);
        
        /* close */
        close(fd_data.fd);

        /* free */
        handle_data.handle = allocation_data.handle;
        lprint("\n");
        lprint("[ION] ION_IOC_FREE... ");
        err = ioctl(my_ion_fd, ION_IOC_FREE, &handle_data);
        if (err) {
            lprint("Failed: %s\n", strerror(errno));
            continue;
        }
        lprint("Success\n");
        lprint("\n");




        ids.push_back(id);
    }

    return ids;
}
