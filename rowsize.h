#ifndef __ROWSIZE_H__
#define __ROWSIZE_H__

#include <set>

#include "helper.h"

const std::set<int> VALID_ROWSIZES = {K(16), K(32), K(64), K(128), K(256)};

#define PAGES_PER_ROW (rowsize / PAGESIZE)
#define MAX_ROWSIZE K(256)

int RS_autodetect(void);

struct model {
    std::string model; // ro.product.model
    std::string name;  // ro.product.name
    std::string board; // ro.product.board
    std::string platform; // ro.board.platform
    int kmalloc_heap;
    int rowsize;
    std::string generic_name;
};


#endif // __ROWSIZE_H__
