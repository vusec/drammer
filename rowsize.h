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

#ifndef __ROWSIZE_H__
#define __ROWSIZE_H__

#include <set>

#include "helper.h"

const std::set<int> VALID_ROWSIZES = {K(16), K(32), K(64), K(128), K(256)};

#define PAGES_PER_ROW (rowsize / PAGESIZE)
#define MAX_ROWSIZE K(256)

int RS_autodetect(bool always);

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
