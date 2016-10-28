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


#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <numeric>
#include <string>
#include <iostream>

#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "helper.h"
#include "ion.h"
#include "rowsize.h"

#define ROWSIZE_READCOUNT 2500000 // 2.5 million reads
#define ROWSIZE_PAGES 128

#define DEFAULT_ROWSIZE K(64)

std::vector<struct model> models = { 
//  model         ro.product.name     board            platform  ion  row      generic name 

// Snapdragon 820
// {"SM-G935T",  "hero2qltetmo",     "msm8996",       "msm8996", 21,  0,      "Samsung Galaxy S7 Edge"},
// {"SAMSUNG-SM-G930A", "heroqlteuc","MSM8996",       "msm8996", 21,  0,      "Samsung Galaxy S7"},

// Snapdragon 810
// {"Nexus 6P",  "angler",           "angler",        "msm8994", 21,  0,      "Huawei Nexus 6P"},
   {"E6853",     "E6853",            "msm8994",       "msm8994", 21,  K(64), "Sony Xperia Z5"},

// Snapdragon 808
   {"Nexus 5X",  "bullhead",         "bullhead",      "msm8992", 21,  K(64),  "LG Nexus 5X"},
   {"LG-H960",   "pplus_global_com", "msm8992",       "msm8992", 21,  K(64),  "LG V10"},
   {"LG-H815",   "p1_global_com",    "msm8992",       "msm8992", 21,  K(64),  "LG G4"},

// Snapdragon 805
   {"SM-G901F",  "kccat6xx",         "APQ8084",       "apq8084", 21,  K(128), "Samsung Galaxy S5 Plus"}, 
// {"SM-N910V",  "trltevzw",         "APQ8084",       "apq8084", 21,  0,      "Samsung Galaxy Note 4"},

// Snapdragon 800
   {"Nexus 5",   "hammerhead",       "hammerhead",    "msm8974", 21,  K(64),  "LG Nexus 5"},
   {"A0001",     "bacon",            "MSM8974",       "msm8974", 21,  K(128), "OnePlus One"},
   {"SM-G870F",  "klteactivexx",     "MSM8974",       "msm8974", 21,  K(64), "Samsung Galaxy S5 Active"},
// {"SM-G900T",  "kltetmo",          "MSM8974",       "msm8974", 21,  0,      "Samsung Galaxy S5"},

// Snapdragon 410:
// {"SM-A500FU", "a5ultexx",         "MSM8916",       "msm8916", 21,  0,      "Samsung Galaxy A5"},
// {"MotoG3",    "osprey_retus",     "msm8916",       "msm8916", 21,  0,      "Motorola Moto G 3rd Gen"},
   {"GT-I9195I", "serranoveltexx",   "MSM8916",       "msm8916", 21,  K(32),  "Samsung Galaxy S4 Mini"},
   {"KIW-L21",   "KIW-L21",          "KIW-L21",       "msm8916", 21,  K(32),  "Huawei Honor 5X"},
   {"MotoE2(4G-LTE)", "surnia_reteu","msm8916",       "msm8916", 21,  K(32),  "Motorola Moto E 2nd Gen"},
   {"MotoG3",    "osprey_reteu",     "msm8916",       "msm8916", 21,  K(32),  "Motorola Moto G 3rd Gen"},
   {"HUAWEI RIO-L01", "RIO-L01",     "RIO-L01",       "msm8916", 21,  K(64),  "Huawei GX8/G8"},
   {"HTC One M8s","m8qlul_htc_europe","msm8939",      "msm8916", 21,  K(64),  "HTC One M8s"},

// Snapdragon 400:
   {"XT1064",    "titan_retuaws",    "MSM8226",       "msm8226", 21,  K(32),  "Motorola Moto G 2nd Gen"},
   {"XT1068",    "titan_retaildsds", "MSM8226",       "msm8226", 21,  K(32),  "Motorola Moto G 2nd Gen"},
// {"LG-V410",   "e7lte_att_us",     "MSM8226",       "msm8226", 21,  0,      "LG G Pad 7.0"},

   {"SM-J320FN", "j3xnltexx",        "SC9830I",       "sc8830",   2,  K(32),  "Samsung Galaxy J3 2016"},
   {"SM-A310F", "a3xeltexx",         "universal7580", "exynos5",  4,  K(64),  "Samsung Galaxy A3 2016"}, // not sure about the rowsize...
   {"SM-A700F", "a7altexx",          "universal5430", "exynos5",  4,  K(128), "Samsung Galaxy A7"},
   {"SM-G920F",  "zerofltexx",       "universal7420", "exynos5",  4,  K(128), "Samsung Galaxy S6"},
   {"SM-G935F",  "hero2ltexx",       "universal8890", "exynos5",  4,  K(256), "Samsung Galaxy S7 Edge"},
   {"SM-G930F",  "heroltexx",        "universal8890", "exynos5",  4,  K(256), "Samsung Galaxy S7"},
// {"SM-T710",   "gts28wifixx",      "universal5433", "exynos5",  4,  0,      "Samsung Galaxy Tab S2 8.0"},
   {"SM-G935F",  "hero2ltexx",       "universal8890", "exynos5",  4,  K(256), "Samsung Galaxy S7 Edge"},
   {"SM-G930F",  "heroltexx",        "universal8890", "exynos5",  4,  K(256), "Samsung Galaxy S7"},
// {"SM-T710",   "gts28wifixx",      "universal5433", "exynos5",  4,  0,      "Samsung Galaxy Tab S2 8.0"},
// {"SM-T810",   "gts210wifixx",     "universal5433", "exynos5",  4,  0,      "Samsung Galaxy Tab S2 9.7"},
   { "SM-N910C", "treltexx",         "universal5433", "exynos5",  4,  K(64),  "Samsung Galaxy Note 4"},

 // Snapdragon S4
// {"AOSP on Mako", "full_mako",     "MAKO",          "msm8960", 21,  0,      ""},

   {"ALE-L21",   "ALE-L21",          "BalongV8R1SFT", "hi6210sft",1,  K(32),   "Huawei P8 Lite"},

   {"HUAWEI VNS-L31", "VNS-L31",     "VNS-L31",       "hi6250",   1,  K(32),   "Huawei P9 Lite"},

   {"NEO6_LTE", "NEO6_LTE",          "",              "mt6735",   1,  K(32),   "Odys Neo 6"},

   {"HTC Desire 830 dual sim","a51cml_dtul_00401","", "mt6753",   1,  K(64),   "HTC Desire 830"},

   {"E5603",    "E5603",             "",              "mt6795",   1,  K(64),   "Sony Xperia M5"}

 
 // MT6572
// {"Goophone i5C", "mbk72_wet_jb3", "mbk72_wet_jb3", "",        21,  0,      "Goophone i5C"},

 // MT8735


};

int rowsize;
    
uint64_t compute_mad(std::vector<uint64_t> &v) {
    uint64_t median = compute_median(v); 
    
    std::vector<uint64_t> absolute_deviations;
    for (auto it : v) {
        if (it < median) absolute_deviations.push_back( median - it );
        else             absolute_deviations.push_back (it - median );
    }
    sort(absolute_deviations.begin(), absolute_deviations.end());
    return compute_median(absolute_deviations);
}

uint64_t compute_iqr(std::vector<uint64_t> &v, uint64_t *q1, uint64_t *q2, uint64_t *q3) {
    std::vector<uint64_t> tmp = v;
    sort(tmp.begin(), tmp.end());
    auto const i1 = tmp.size() / 4;
    auto const i2 = tmp.size() / 2;
    auto const i3 = i1 + i2;
    std::nth_element(tmp.begin(),          tmp.begin() + i1, tmp.end());
    std::nth_element(tmp.begin() + i1 + 1, tmp.begin() + i2, tmp.end());
    std::nth_element(tmp.begin() + i2 + 1, tmp.begin() + i3, tmp.end());
    *q1 = tmp[i1];
    *q2 = tmp[i2];
    *q3 = tmp[i3];
    return (tmp[i3] - tmp[i1]);
}

std::string getprop(std::string property) {
    std::string cmd = "/system/bin/getprop ";
    cmd += property;

    char buffer[128];
    std::string value = "";
    std::shared_ptr<FILE> pipe(popen(cmd.c_str(), "r"), pclose);
    if (!pipe) {
        perror("popen failed");
        return value;
    }
    while (!feof(pipe.get())) {
        if (fgets(buffer, 128, pipe.get()) != NULL)
            value += buffer;
    }
    value.erase(std::remove(value.begin(), value.end(), '\n'), value.end());
    return value;
}

#define KNOWN_MODEL 2
#define FAMILIAR_MODEL 1
#define UNKNOWN_MODEL 0
struct model *get_model(int *familiarity) {
    std::string model = getprop("ro.product.model");
    print("[RS] ro.product.model: %s\n", model.c_str());

    std::string name = getprop("ro.product.name");
    print("[RS] ro.product.name: %s\n",name.c_str());

    std::string board = getprop("ro.product.board");
    print("[RS] ro.product.board: %s\n", board.c_str());

    std::string platform = getprop("ro.board.platform");
    print("[RS] ro.board.platform: %s\n", platform.c_str());

    for (std::vector<struct model>::iterator it  = models.begin();
                                             it != models.end();
                                           ++it) {
        struct model *m = &(*it);
        if (m->model == model || m->name == name) {
            print("[RS] known model: %s\n", m->generic_name.c_str());
            *familiarity = KNOWN_MODEL;
            return m;
        }
    }

    for (std::vector<struct model>::iterator it  = models.begin();
                                             it != models.end();
                                           ++it) {
        struct model *m = &(*it);
        if (m->board == board || m->platform == platform) {
            printf("[RS] familiar model: %s\n", m->generic_name.c_str());
            *familiarity = FAMILIAR_MODEL;
            return m;
        }
    }

    *familiarity = UNKNOWN_MODEL;
    return NULL;
}


/* auto detect row size */
int RS_autodetect(void) {

    print("[RS] Trying getprop\n");
    int familiarity;
    struct model *m = get_model(&familiarity);
    if (familiarity == KNOWN_MODEL) {
        rowsize = m->rowsize;
        return rowsize;
    }


    print("[RS] Allocating 256 ion chunk\n");
    struct ion_data data;
    data.handle = ION_alloc(K(256));
    if (data.handle == 0) {
        perror("Could not allocate 256K chunk for row size detection");
        exit(EXIT_FAILURE);
    }
    data.len = K(256);
    ION_mmap(&data);
   
    print("[RS] Reading from page 0 and page x (x = 0..%d)\n",ROWSIZE_PAGES);
    std::vector<uint64_t> deltas;
    int page1 = 0;
    volatile uintptr_t *virt1 = (volatile uintptr_t *) ((uint64_t) data.mapping + (page1 * PAGESIZE));
    for (int page2 = 0; page2 < ROWSIZE_PAGES; page2++) {
        volatile uintptr_t *virt2 = (volatile uintptr_t *) ((uint64_t) data.mapping + (page2 * PAGESIZE));

        uint64_t t1 = get_ns();
        for (int i = 0; i < ROWSIZE_READCOUNT; i++) {
            *virt1;
            *virt2;
        }
        uint64_t t2 = get_ns();
        deltas.push_back((t2 - t1) / ROWSIZE_READCOUNT);

        print("%llu ", deltas.back());
    }
    print("\n");

    if (munmap(data.mapping, data.len)) {
        perror("Could not munmap");
        exit(EXIT_FAILURE);
    }
    if (close(data.fd)) {
        perror("Could not close");
        exit(EXIT_FAILURE);
    }
    if (ION_free(data.handle)) {
        perror("Could not free");
        exit(EXIT_FAILURE);
    }

    uint64_t q1, q2, q3;
    uint64_t    iqr = compute_iqr   (deltas, &q1, &q2, &q3);
    uint64_t median = compute_median(deltas);
    uint64_t    mad = compute_mad   (deltas);

    print("[RS] Median: %llu\n", median);
    print("[RS] MAD: %llu\n", mad);
    print("[RS] IQR: %llu\n", iqr);

    // MAD, IQR and standard deviation all need some form of correction... :(
    iqr += 5;
    print("[RS] Corrected IQR: %llu\n", iqr);


    /* try simple algorithm first */
    int count = 0;
    for (auto it: deltas) {
        if (it < 2*median) {
            count++;
        } else {
            break;
        }
    }
    printf("count: %d\n", count);
    rowsize = count * 4096;

    /* do more advanced stuff if the rowsize is absurd */
    if (rowsize >= K(128)) {

        print("[RS] Sequences: ");
        std::vector<uint64_t> seq_normal;
        std::vector<uint64_t> seq_outlier;
        int sn = 0; int so = 0;
        for (auto it : deltas) {
            if (it < q1 - 1.5*iqr || it > q3 + 1.5*iqr) {
                if (so != 0) {
                    seq_normal.push_back(so);
                    print("%d ", so);
                    so = 0;
                }
                sn++;
            } else {
                if (sn != 0) {
                    seq_outlier.push_back(sn);
                    print("%d ", sn);
                    sn = 0;
                }
                so++;
            }
        }
        printf("\n");
    
        rowsize = (compute_median(seq_normal) + compute_median(seq_outlier)) * 4096;
    }


    print("[RS] Detected row size: %d\n", rowsize);
    if (!VALID_ROWSIZES.count(rowsize)) {
        if (familiarity == FAMILIAR_MODEL) {
            print("[RS] WARNING! Weird row size detected, assuming familiar model's rowsize %d\n", m->rowsize);
            rowsize = m->rowsize;
        } else {
            print("[RS] WARNING! Weird row size detected, assuming %d\n", DEFAULT_ROWSIZE);
            rowsize = DEFAULT_ROWSIZE;
        }
    }

    return rowsize;
}
