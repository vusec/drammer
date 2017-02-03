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

/* December 29. The comments below might be a bit out-dated already. Leaving
 * them here for now.
 *
 * This autodetect module tries to detect the following device specific
 * features:
 *      1) Rowsize
 *      2) ION heap for contiguous memory
 *      3) Treshold for bank conflicts
 *    [ 4) DRAM addressing functions    ] --> disabled for now
 * Of this list, features 1) and 2) are crucial for correct operation of
 * drammer, while 3) and 4) are nice to have. Since autodetect may fail, several
 * fallback options are in place. For this, we first search this device in our
 * list of known devices to construct a 'database' model. Depending on the
 * matched criteria, we identify four different types of database models:
 *
 *** EXACT_MODEL
 * If autodetect is succesful, results are written to a file for later use. Our
 * first search for a database model is thus to look for this file and read its
 * contents. If we find it, we have an EXACT_MODEL.
 *
 *** KNOWN_MODEL
 * We may have analyzed a device before, in which case we added results to the
 * top of this file, in our 'database'. Whenever a device's product model or
 * product name properties match one in our database, we identify this device as
 * KNOWN_MODEL. Note that a custom ROM may cause false positives.
 *
 *** FAMILIAR_MODEL
 * If we do not match the model or name, but do match the product board or
 * platform, we identify this device as a FAMILIAR_MODEL.
 *
 *** UNKNOWN_MODEL
 * There may be no match with our database, in which case this device is an
 * UNKNOWN_MODEL.
 *
 *
 * 1) Detecting the rowsize and treshold
 * The control-flow for detecting the rowsize and treshold depends on the
 * database model:
 * 
 * o EXACT_MODEL 
 *   ret EXACT_MODEL; // includes rowsize, ion-heap, treshold, and bank-select mask
 *
 * o KNOWN_MODEL 
 *   rowsize, treshold = verify_rowsize(KNOWN_MODEL.rowsize)
 *   if !rowsize: // verification failed -> fallback to autodetection
 *      rowsize, treshold = detect_rowsize() 
 *      if (!rowsize): // detection failed -> fallback to known-model
 *          rowsize = KNOWN_MODEL.rowsize
 *          treshold = 0
 * 
 * o FAMILIAR_MODEL
 *   rowsize, treshold = detect_rowsize()
 *   if (!rowsize): // detection failed -> fallback to familiar-model
 *      rowsize = FAMILIAR_MODEL.rowsize
 *      treshold = 0
 *
 * o UNKNOWN_MODEL
 *   rowsize, treshold = detect_rowsize()
 *   if (!rowsize): // detection failed -> fallback to default
 *      rowsize = DEFAULT_ROWSIZE
 *      treshold = 0
 *
 * 
 * 2) Detecting the ION heap
 * In the case that no EXACT_MODEL was found, ION heap detection is done during
 * rowsize verification and/or detection. For this, we construct a list of
 * possible ION heap ids and iterate over them while trying verification or
 * detection. The idea is that if the wrong heap is used (not giving us
 * contiguous memory), verification or detection will fail, and we can remove
 * the heap from possible candidates.
 *
 * The list of possible ION heap ids is constructed as follows:
 * - Brute-force all 32 candidates and try to allocate 4KB (which should
 *   succeed) and 2^(max_order+1)*4KB (which should fail with an out-of-memory
 *   error message).
 * - Lookup the device's platform in our database of known platforms. If this
 *   gives an ION-heap id that was already in the list (by our brute-force try),
 *   we move this item to the head of the list to make sure it is tried first.
 *   If it was not on the list, it is appended to the very end.
 *
 *
 * 3) DRAM addressing functions
 * Finally, if the rowsize was correctly detected or verified (and we thus have
 * a working treshold value to identify bank conflicts), we try to find the
 * addressing functions (how physical addresses map to a bank). Our approach is
 * based on DRAMA [1] and only works is the addressing is 'simple' enough. If
 * reversing the bank select bits fails, drammer should rely on the treshold to
 * identify address that fall in the same bank.
 *
 * The control-flow for detecting the bank-select bits is similar to the rowsize
 * detection and depends on the database model:
 * 
 * o KNOWN_MODEL 
 *   mask = verify_mask(KNOWN_MODEL.mask)
 *   if !mask: // verification failed -> fallback to autodetection
 *      mask = detect_mask() 
 *      if (!mask): // detection failed -> fallback to 0
 *          mask = 0
 * 
 * o FAMILIAR_MODEL|UNKNOWN_MODEL
 *   mask = detect_mask()
 *   if (!mask): // detection failed -> fallback to 0
 *      mask = 0
 *
 *
 * [1]
 * https://www.usenix.org/system/files/conference/usenixsecurity16/sec16_paper_pessl.pdf
 */

#include <algorithm>
#include <bitset>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>

#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "helper.h"
#include "ion.h"
#include "rowsize.h"

#define CACHELINE_SIZE 64

#define MAX_TRIES         1


#define MEASUREMENTS      100
#define DEFAULT_LOOPCOUNT 10000 
#define DEFAULT_FENCE     FENCING_NONE
#define DEFAULT_CPU       0





#define EXACT_MODEL     3
#define KNOWN_MODEL     2
#define FAMILIAR_MODEL  1
#define UNKNOWN_MODEL   0

#define RS_CHUNKSIZE K(256)


extern int ion_fd;

std::vector<struct model> models = { 

// last: 909208b7e7b217160066351b95a72232

    
/**** QUALCOMM ****/
//  generic name                ro.product.model    ro.product.name     board           platform   ion rowsize    ba2     ba1     ba0   rank
// Snadragon 210 - MSM8909:
   {"HTC Desire 530",           "HTC Desire 530",   "a16ul_00401",      "msm8909",      "msm8909", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // @nfi
   {"LG Treasure LTE",          "LGL52VL",          "m1_trf_us_vzw",    "msm8909",      "msm8909", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // 54cc73c06e1be0d13d544d6f2f17efcb
   {"LG Optimus Zone 3",        "VS425PP",          "e1q_vzw",          "msm8909",      "msm8909", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // 4f3426e7b286d13e9b2598b34fcbc43b
   {"LG X Power",               "LG-K210",          "k6p_global_ca",    "msm8909",      "msm8909", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // eb1c37e6ac2d0554518a2ee65b576318
   {"LG Tribute 5",             "LGLS675",          "m1_spr_us",        "msm8909",      "msm8909", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // e861e390c8e8f607c4784d3a0954b113
   {"LG Tribute HD",            "LGLS676",          "k6b_spr_us",       "msm8909",      "msm8909", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // 4b421cfcb5801a73e3cd166a38935bbd
   {"LG K10",                   "LGMS428",          "m209n_mpcs_us",    "msm8909",      "msm8909", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // e304b1c770aa688b253c33e4798bb2b4
   {"ZTE Z815",                 "Z815",             "Z815",             "sheen",        "msm8909", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // 3d27c89335bd5a133b6916a5ccdaa4f5
   {"ZTE Blade A310",           "Blade A310",       "P809A50_CO_CLA",   "msm8909",      "msm8909", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // 0f5fb3fac6e602e92c7f9c207b4e9f62
   {"Alcatel Pixi Avion",       "A571VL",           "A571VL",           "msm8909",      "msm8909", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // b0b04075873ef6cc9ceba4bb41d33875
   {"KYOCERA-C6742",            "KYOCERA-C6742",    "C6742",            "C6742",        "msm8909", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // 32ca7a3034f12bb895cfdbfb10d194a2 - failed but close
   {"S40",                      "S40",              "CatS40",           "msm8909",      "msm8909", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // 6a333c8a5672524ea3d65e3ff40831c9


// Snapdragon 400 - MSM8226:                                                                                    
   {"Motorola Moto G 1st Gen",  "XT1028",           "falcon_verizon",   "MSM8226",      "msm8226", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // 428b0da2cf26567c2b28154c63a1e250
   {"Motorola Moto G 1st Gen",  "XT1032",           "falcon_reteu",     "MSM8226",      "msm8226", 21, K( 32), 0x4000, 0x2000, 0x1000, 0x000}, // @home
   {"Motorola Moto G 2nd Gen",  "XT1064",           "titan_retuaws",    "MSM8226",      "msm8226", 21, K( 32), 0x4000, 0x2000, 0x1000, 0x000}, // @sb
   {"Samsung Galaxy S3 Neo",    "GT-I9301I",        "s3ve3gxx",         "MSM8226",      "msm8226", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // 9aa8b754889c6c3d78eba2114b272046 - FLIPS
   {"LGLS740",                  "LGLS740",          "x5_spr_us",        "MSM8226",      "msm8226", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // 396b97408c3d13febc2c4fbe0499ca87
// {"Motorola Moto G 2nd Gen",  "XT1068",           "titan_retaildsds", "MSM8226",      "msm8226", 21, K( 32), 0x4000, 0x2000, 0x1000, 0x000},
// {"LG G Pad 7.0",             "LG-V410",          "e7lte_att_us",     "MSM8226",      "msm8226", 21, K(  0), 0x0000},
//                  MSM8228:
   {"HTC Desire 816",     "HTC Desire 816 dual sim","htc_asia_india",   "MSM8226",      "msm8226", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // da75ed4017d9fe0d0f03d66ac655d2fc

   
//                  MSM8926                    
   {"XT1077",                   "XT1077",           "thea_retcn_ctds",  "MSM8226",      "msm8226", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // f71073274d551a3f98083da6e37755e3

//                  MSM8928:
   {"HTC Desire 10 Lifestyle","HTC Desire 10 lifestyle",
                                                    "a56djul_00600",    "MSM8226",      "msm8226", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // 7172bb86fdd7ee7389029fc429a0b444
   {"HTC Desire 630",     "HTC Desire 630 dual sim","a16dwgl_00401",    "MSM8226",      "msm8226", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // e92e02117a865f48a4e77971b5b087fc

// Snapdragon 410 - MSM8916:                                                        
   {"HTC Desire 510",           "HTC Desire 510",   "htc_europe",       "msm8916",      "msm8916", 21, K( 32), 0x4000, 0x2000, 0x1000, 0x000}, // @home
   {"Motorola Moto E 2nd Gen",  "MotoE2(4G-LTE)",   "surnia_reteu",     "msm8916",      "msm8916", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // < 0.2
   {"Motorola Moto G 3rd Gen",  "MotoG3",           "osprey_reteu",     "msm8916",      "msm8916", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // < 0.2
   {"Samsung Galaxy S4 Mini",   "GT-I9195I",        "serranoveltexx",   "MSM8916",      "msm8916", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // < 0.2
   {"Samsung Galaxy A5",        "SM-A500FU",        "a5ultexx",         "MSM8916",      "msm8916", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // < 0.2
   {"Motorola Moto G 3rd Gen",  "MotoG3",           "osprey_retus",     "msm8916",      "msm8916", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // < 0.2
   {"Motorola Moto G 3rd Gen",  "MotoG3",           "osprey_reteu_2gb", "msm8916",      "msm8916", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // @nfi
   {"LG Premier LTE",           "LGL62VL",          "m209_trf_us_vzw",  "msm8916",      "msm8916", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // da371d919e85c375a3ff2acd00a53a25
   {"LG Stylo 2",               "LGL81AL",          "ph1_trf_us",       "msm8916",      "msm8916", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // f170ab317e7b1096cf8450c1baa84e8a
   {"LG Style 2",               "LGL82VL",          "ph1_trf_us_vzw",   "msm8916",      "msm8916", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // 898143d5c1a42996b1b5f7498f4e0f8f
   {"LG G4c" ,                  "LG-H525n",         "c90n_global_com",  "msm8916",      "msm8916", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // 5841086560dede329537623a3d07e027
   {"ZTE Warp Elite",           "N9518",            "zte_warp6",        "warp6",        "msm8916", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // aa5578234f99b2986bae80e1204a453d
   {"ZTE Boost Max+",           "N9521",            "zte_max",          "max",          "msm8916", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // 24ded3caa90257c952f7eb5708bdae67
   {"XT1528",                   "XT1528",           "surnia_verizon",   "msm8916",      "msm8916", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // 4b191d43f34b4692b01083093bbae5b7
   {"MotoG3",                   "MotoG3",           "osprey_retus_2gb", "msm8916",      "msm8916", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // bf89c1c7ec8b3cc72201e5edc95004c6

// Snapdragon 425 - MSM8917:
   {"ZTE Avid Trio",            "Z833",             "P817T06",          "CAMELLIA",     "msm8937", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // 933771242b444995f3ec9e17a111a11f

// Snapdragon 615 - MSM8939
   {"Xiaomi Mi 4i",             "Mi 4i",            "ferrari",          "msm8916",      "msm8916", 21, K( 64), 0x8000, 0x4000, 0x2000, 0x000}, // @home
// {"HTC One M8s",              "HTC One M8s",      "m8qlul_htc_europe","msm8939",      "msm8916", 21, K( 64), 0x0000},
   {"Huawei GX8/G8",            "HUAWEI RIO-L01",   "RIO-L01",          "RIO-L01",      "msm8916", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // @nfi
   {"Motorola Moto X Play",     "XT1563",           "lux_retca",        "msm8916",      "msm8916", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // 7b1c1b688e7ce0ac4dd154cc49613adf
   {"ZTE G720T",                "ZTE G720T",        "P839T30",          "msm8916",      "msm8916",2 1, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // 7427ec38c358d63c35eadd6cce2260b5

// Snapdragon 616 - MSM8939v2
// {"Huawei Honor 5X",          "KIW-L21",          "KIW-L21",          "KIW-L21",      "msm8916", 21, K( 32), 0x0000},

// Snapdragon 617 - MSM8952
   {"Moto G (4)",               "Moto G (4)",       "athene",           "msm8952",      "msm8952", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // @nfi
   {"LG G Vista 2",             "LG-H740",          "p1v_att_us",       "msm8952",      "msm8952", 21, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // 74ce1633692226a1eb02cf52f0537a48

// MSM8956
   {"Redmi Note 3",             "Redmi Note 3",     "kenzo",            "msm8952",      "msm8952", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // 7bac314dfde794264bd447002ed8a7e1

// Snapdragon 652 - MSM8976
   {"LG-H840",                  "LG-H840",          "alicee_global_com","msm8952",      "msm8952", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // 55cfd7a4b551bef72c86707dfac17143


// Snapdragon 800 - MSM8974
   {"LG Nexus 5",               "Nexus 5",          "hammerhead",       "hammerhead",   "msm8974", 21, K( 64), 0x8000, 0x4000, 0x2000, 0x400}, // @home
   {"Fairphone 2",              "FP2",              "FP2",              "FP2",          "msm8974", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // @nfi
   {"C6903",                    "C6903",            "C6903",            "MSM8974",      "msm8974", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // e551436309b8e9e6b3ffc6300476d5ff

// Snapdragon 801 - MSM8974AB
   {"HTC One M8",               "831C",             "sprint_wwe_harman","MSM8974",      "msm8974", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // 1ba3c23a8b2180fefa915fc4d6937a59
   {"HTC6525LVW",               "HTC6525LVW",       "HTCOneM8vzw",      "MSM8974",      "msm8974", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // a4c47ce3bbd28de06a3253510cbf1a50

// Snapdragon 801 - MSM8974AC
   {"OnePlus One",              "A0001",            "bacon",            "MSM8974",      "msm8974", 21, K( 64), 0x1000, 0x8000, 0x4000, 0x400}, // 638a7e35067de5d00855f214a2791118
   {"Samsung Galaxy S5 Active", "SM-G870F",         "klteactivexx",     "MSM8974",      "msm8974", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // < 0.2
   {"Samsung Galaxy S5",        "SM-G900T",         "kltetmo",          "MSM8974",      "msm8974", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // < 0.2
   {"Samsung Galaxy S5",        "SM-G900V",         "kltevzw",          "MSM8974",      "msm8974", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // 5e241103203859aefe4b4f0709f50501
   {"Samsung Galaxy S5",        "SM-S903VL",        "kltetfnmm",        "MSM8974",      "msm8974", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // 179ef7953009e70ac47154ec22a3058e
   {"LG-D855",                  "LG-D855",          "g3_global_com",    "MSM8974",      "msm8974", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // 6960cc492edf822f833211043ffe1819
   {"SAMSUNG-SM-G900A",         "SAMSUNG-SM-G900A", "klteuc",           "MSM8974",      "msm8974", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // 99299170b0ed083900ac94c58222903c
   {"ZUK Z1",                   "ZUK Z1",           "ham",              "MSM8974",      "msm8974", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // 44b463d051c388390c3b9e7d1803b638





// Snapdragon 805 - APQ8084
   {"Samsung Galaxy S5",        "SM-G901F",         "kccat6xx",         "APQ8084",      "apq8084", 21, K(128), 0x0000, 0x0000, 0x000, 0x000}, // @nfi
// {"Samsung Galaxy Note 4",    "SM-N910V",         "trltevzw",         "APQ8084",      "apq8084", 21, K(  0), 000000},


// Snapdragon 808 - MSM8992
   {"LG Nexus 5X",              "Nexus 5X",         "bullhead",         "bullhead",     "msm8992", 21, K( 64), 0x8000, 0x4000, 0x2000, 0x400}, // @home
   {"LG G4",                    "LG-H815",          "p1_global_com",    "msm8992",      "msm8992", 21, K( 64), 0x8000, 0x4000, 0x2000, 0x400}, // @home
   {"LG G4",                    "LG-H810",          "p1_att_us",        "msm8992",      "msm8992", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // c6a3dcf2f512856ac6544122c9bad906
   {"LG G4 Dual",               "LG-H818",          "p1_global_com",    "msm8992",      "msm8992", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // 72ab1933b12e57bcb618345297f86dbc
   {"LG V10",                   "LG-H960",          "pplus_global_com", "msm8992",      "msm8992", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // < 0.2
   {"LG V10",                   "LG-H900",          "pplus_att_us",     "msm8992",      "msm8992", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // 2d27a8a56200a4cfccddbd81263afe6d
   {"LGLS991",                  "LGLS991",          "p1_spr_us",        "msm8992",      "msm8992", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // 1e0fdd82d5b020eb2228b7d3e96566a2
   {"LGUS991",                  "LGUS991",          "p1_usc_us",        "msm8992",      "msm8992", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // b36c377b1df8aee89b9d0ee3b1ee0b8d
   {"STV100-2",                 "STV100-2",         "venicevzwvzw",     "venice",       "msm8992", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // 31d5680d18250388fb7eb241cbd0e3bb
   {"VS990",                    "VS990",            "pplus_vzw",        "msm8992",      "msm8992", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // eaad7de918ff14131edf11fb299a2d71




// Snapdragon 810 - MSM8994
// {"Sony Xperia Z5",           "E6853",            "E6853",            "msm8994",      "msm8994", 21, K( 64), 0x0000},
// {"Huawei Nexus 6P",          "Nexus 6P",         "angler",           "angler",       "msm8994", 21, K(  0), 0x0000},

// Snapdragon 820 - MSM8996
   {"LG G5",                    "LG-H850",          "h1_global_com",    "msm8996",      "msm8996", 21, K( 64), 0x8000, 0x4000, 0x2000, 0x400}, // @home
   {"Samsung Galaxy S7 Edge",   "SM-G935T",         "hero2qltetmo",     "msm8996",      "msm8996", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // 189303d8523f1c1a15af0327e2cd7e9a
// {"Samsung Galaxy S7",        "SAMSUNG-SM-G930A", "heroqlteuc",       "MSM8996",      "msm8996", 21, K(  0), 0x0000},
   {"Samsung Galaxy S7",        "SM-G930V",         "heroqltevzw",      "msm8996",      "msm8996", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // 20e27a705a09025f7924559fa02d5011
   {"Sharp Aquos Xx3",          "506SH",            "SG506SH",          "SG506SH",      "msm8996", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // 1c8e79b8f29e52b089b2714f84db100a
   {"Samsung Galaxy Note 7",    "SM-N930T",         "graceqltetmo",     "msm8996",      "msm8996", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // f193124f13962db11aa01c8181d0d39d
   {"OnePlus 3",                "ONEPLUS A3003",    "OnePlus3",    "QC_Reference_Phone","msm8996", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // f285ee909fa8bfde90ac430aa1796b5c
   {"OnePlus 3",                "ONEPLUS A3000",    "OnePlus3",    "QC_Reference_Phone","msm8996", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // a5c032f2ec91ce3b634ca2f5e638206a
   {"XT1650",                   "XT1650",           "griffin_verizon",  "msm8996",      "msm8996", 21, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // a57a4a67221822dd55aa800b87eda54d



// Snapdragon S4
// {"LG Nexus 4",               "AOSP on Mako",     "full_mako",        "MAKO",         "msm8960", 21, K( 64), 0x0000},


/**** HISILICON ****/

//  generic name                ro.product.model    ro.product.name     board           platform   ion rowsize    ba2     ba1     ba0   rank
// Kirin 620
   {"Huawei P8 Lite",           "ALE-L21",          "ALE-L21",          "BalongV8R1SFT","hi6210sft",1, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // @nfi
   {"Huawei G Play mini",       "CHC-U01",          "CHC-U01",          "BalongV8R1SFT","hi6210sft",1, K( 32), 0x0000, 0x0000, 0x0000, 0x000}, // @nfi

// Kirin 650
// {"Huawei P9 Lite",           "HUAWEI VNS-L31",   "VNS-L31",          "VNS-L31",      "hi6250",   1, K( 32), 0x0000},

// Kirin 950
   {"Huawei Honor 8",           "FRD-L09",          "FRD-L09",          "FRD-L09",      "hi3650",   2, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // 78d3e11cdf4c304b56ae05cb22dc9921
   {"Huawei Honor V8",          "KNT-AL10",         "KNT-AL10",         "KNT-AL10",     "hi3650",   1, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // c637fda8be6ed1f5e12ab55154d976e4

// Kirin 955
   {"Huawei P9",                "EVA-L09",          "EVA-L09",          "EVA-L09",      "hi3650",   1, K( 64), 0x8000, 0x4000, 0x2000, 0x080}, // @home




/**** EXYNOS ****/

//  generic name                ro.product.model    ro.product.name     board           platform   ion rowsize    ba2     ba1     ba0   rank
//  Exynos 5410
   {"GT-I9500",                 "GT-I9500",         "ja3gxx",           "universal5410","exynos5",  4, K( 64), 0x0000, 0x0000, 0x0000, 0x000}, // 4bdd1bad302e17292fc263a22ed469fc

// Exynos 5430
   {"Samsung Galaxy A7",        "SM-A700F",         "a7altexx",         "universal5430","exynos5",  4, K(128), 0x0000, 0x0000, 0x0000, 0x000}, //@nfi

// Exynos 5433
// {"Samsung Galaxy Tab S2 8.0","SM-T710",          "gts28wifixx",      "universal5433","exynos5",  4, K(  0), 0x0000},
// {"Samsung Galaxy Tab S2 9.7","SM-T810",          "gts210wifixx",     "universal5433","exynos5",  4, K(  0), 0x0000},
   {"Samsung Galaxy Note 4",    "SM-N910C",         "treltexx",         "universal5433","exynos5",  4, K(128), 0x0000, 0x0000, 0x0000, 0x000}, // @nfi
   {"SM-N910U",                 "SM-N910U",         "trhpltexx",        "universal5433","exynos5",  4, K(128), 0x0000, 0x0000, 0x0000, 0x000}, // bd44ee9db66a7663420a7c7c8e1fbd39 - FLIPS

// Exynos 7420
   {"Samsung Galaxy S6",        "SM-G920F",         "zerofltexx",       "universal7420","exynos5", -1, K(  0), 0x0000, 0x0000, 0x0000, 0x000}, // @nfi - no system contiguous heap

// Exynos 7580
   {"Samsung Galaxy A3 2016",   "SM-A310F",         "a3xeltexx",        "universal7580","exynos5", -1, K(  0), 0x0000, 0x0000, 0x0000, 0x000}, // @nfi - no system contiguous heap

// Exynos 8890
   {"Samsung Galaxy S7 Edge",   "SM-G935F",         "hero2ltexx",       "universal8890","exynos5", -1, K(  0), 0x0000, 0x0000, 0x0000, 0x000}, // @nfi - no system contiguous heap
// {"Samsung Galaxy S7",        "SM-G930F",         "heroltexx",        "universal8890","exynos5",  4, K(256), 0x0000},


/**** MEDIATEK ****/

//  generic name                ro.product.model    ro.product.name     board           platform  ion rowsize     ba2      ba1      ba0   rank
// MT6572
   {"Huawei Ascend Y540",       "HUAWEI Y540-U01",  "Y540-U01",         "Y540-U01",     "",       -1, K(  0), 0x00000, 0x00000, 0x00000, 0x000}, // @nfi - no system contiguous heap
   {"ZTE T520",                 "ZTE T520",         "P172A40_RU_CHA",   "techain6572_wet_l",
                                                                                        "mt6572",  1, K( 32), 0x00000, 0x00000, 0x00000, 0x000}, // b79709c1ebdaa2778393910a8b08e38b

// MT6592
   {"HTC Desire 526G",   "HTC Desire 526G dual sim","v02_htc_europe",  "v02_htc_europe","",       -1, K(  0), 0x00000, 0x00000, 0x00000, 0x000}, // @nfi - no system contiguous heap

// MT6580
   {"Alcatel PIXI 4(4)",        "4034D",            "4034D",            "",             "mt6580",  1, K( 32), 0x44000, 0x22000, 0x11000, 0x000}, // @home
   {"Alcatel One Touch Popstar","5022D",            "5022D",            "",             "mt6580",  1, K( 32), 0x00000, 0x00000, 0x00000, 0x000}, // @nfi
   {"Wiko Lenny3",              "LENNY3",           "V3702AN",          "",             "mt6580",  1, K( 32), 0x00000, 0x00000, 0x00000, 0x000}, // 5eee6f7cb541c8a3f8b2f857efc23ffd - xorred?

// MT6580M
   {"Wiki K-Kool",              "K-KOOL",           "V2800AN",          "",             "mt6580",  1, K( 32), 0x00000, 0x00000, 0x00000, 0x000}, // f2ade8cc739125cfcfae9ad28a982974 - xorred?
   {"FS509",                    "FS509",            "FS509",            "Fly",          "mt6580",  1, K( 64), 0x00000, 0x00000, 0x00000, 0x000}, // d70621207cb7bec2031a8e8b074dec42 - xorred?



// MT6582
   {"TANGO A5",                 "TANGO A5",         "J608_PUBLIC",      "J608_PUBLIC",  "",       -1, K(  0), 0x00000, 0x00000, 0x00000, 0x000}, // @nfi - no system contiguous heap
   {"HTC Desire 320",           "HTC Desire 320",   "v01_htc_europe",   "uc81",         "",       -1, K(  0), 0x00000, 0x00000, 0x00000, 0x000}, // @nfi - no system contiguous heap
   {"LG-H500",                  "LG-H500",          "my90_global_com",  "",             "mt6582",  1, K( 32), 0x00000, 0x00000, 0x00000, 0x000}, // @nfi - looks like xorred bits for ba0|1|2
   {"HUAWEI Y360-U61",          "HUAWEI Y360-U61",  "Y360-U61",         "Y360-U61",     "",       -1, K(  0), 0x00000, 0x00000, 0x00000, 0x000}, // @nfi - no system contiguous heap
   {"HTC Desire 526G dual sim","HTC Desire 526G dual sim","v02_htc_europe","v02_htc_europe", "",  -1, K(  0), 0x00000, 0x00000, 0x00000, 0x000}, // @nfi - no system contiguous heap

// MT6735
   {"Odys Neo 6",               "NEO6_LTE",         "NEO6_LTE",         "",             "mt6735",  1, K( 32), 0x00000, 0x00000, 0x00000, 0x000}, // @nfi - looks like xorred bits for ba0|1|2
   {"Acer Liquid Z530",         "T02",              "T02_ww",           "MT6735",       "mt6735",  1, K( 32), 0x00000, 0x00000, 0x00000, 0x000}, // @nfi - looks like xorred bits for ba0|1|2
   {"BV6000S",                  "BV6000S",          "Blackview",        "Blackview",    "mt6735",  1, K( 32), 0x00000, 0x00000, 0x00000, 0x000}, // 70f95d0b266cd7e52c8f6b9092d2a930
   {"HUAWEI LYO-L02",           "HUAWEI LYO-L02",   "LYO-L02",          "LYO-L02",      "mt6735",  1, K( 32), 0x00000, 0x00000, 0x00000, 0x000}, // fcace253b7a70fd410f9507382272ac6


// MT6735M
   {"LG K4 4G",                 "LG-K120",          "me1_global_com",   "",             "mt6735m", 1, K( 32), 0x00000, 0x00000, 0x00000, 0x000}, // @nfi - looks like xorred bits for ba0|1|2

// MT6735P
   {"ZTE Blade V7 Lite",        "ZTE BLADE V0720",  "P635A32",          "",             "mt6735m", 1,K(  32), 0x00000, 0x00000, 0x00000, 0x000}, // 0dfde0183d0d5f09c80e4ec15160064f - xorred - could be heap 11 also, but that would be weird

// MT6737M
   {"LG K3 K100",               "LG-K100",          "mme0_global_com",  "mt6735",       "mt6737m", 1, K( 32), 0x00000, 0x00000, 0x00000, 0x000}, // 7d73b08b8bddb4743baad9d00772f685 - xorred?

// MT6737T
   {"R6",                       "R6",               "full_h910be_v11_p_yx_a10b_r6","",  "mt6737t",11, K( 64), 0x00000, 0x00000, 0x00000, 0x000}, // 0017400a2375f710e4e91ddbb80a9c91 - heap 11

// MT6752
   {"Sony Xperia C4",           "E5303",            "E5303",            "",             "mt6752",  1, K( 32), 0x00000, 0x00000, 0x00000, 0x000}, // @nfi
   {"Wikio Highway Star",       "HIGHWAY STAR",     "l5560ae",          "l5560ae",      "",        1, K( 32), 0x00000, 0x00000, 0x00000, 0x000}, // @nfi

// MT6753
   {"Huawei GR3",               "HUAWEI TAG-L21",   "TAG-L21",          "TAG-L21",      "mt6753",  1, K( 32), 0x00000, 0x00000, 0x00000, 0x000}, // @nfi - looks like xorred bits for ba0|1|2
   {"HTC Desire 830",   "HTC Desire 830 dual sim",  "a51cml_dtul_00401","",             "mt6753",  1, K( 64), 0x00000, 0x00000, 0x00000, 0x000}, // @nfi - looks like xorred bits for ba0|1|2

// MT6755M
   {"ZTE Nubia N1",             "NX541J",           "NX541J",           "mt6755",       "mt6755",  1, K( 64), 0x00000, 0x00000, 0x00000, 0x000}, // f07831be5d61298aa0c67e5c39cbdc4b - xorred?
   {"Lenovo A7020a48",          "Lenovo A7020a48",  "k52_a48",          "",             "mt6755",  1, K( 64), 0x00000, 0x00000, 0x00000, 0x000}, // b29602a4a311077454c4c66c8f128193


// MT6795
// {"Sony Xperia M5",           "E5603",            "E5603",            "",             "mt6795",  1, K( 64), 0x00000},
   {"HTC One M9",    "HTC One M9_Prime Camera Edition","himaruhl_00401","",             "mt6795",  1, K( 64), 0x00000, 0x00000, 0x00000, 0x000}, // @nfi - looks like xorred bits for ba0|1|2

// MT8127
   {"Amazon Fire 7",            "KFFOWI",           "full_ford",        "ford",         "mt8127",  1, K( 32), 0x00000, 0x00000, 0x00000, 0x000}, // b0292167514398a8b8642622fa47159d - xorred?

// MT8321
   {"Alcatel PIXI 4(6)",        "8050D",            "8050D",            "8050D",        "mt6580",  1, K( 32), 0x00000, 0x00000, 0x00000, 0x000}, // 80d8e659c31079506e4d08c3dcfd073d - xorred?
   {"E691X",                    "E691X",            "E691X",            "",             "mt6580",  1, K( 32), 0x00000, 0x00000, 0x00000, 0x000}, // bd543f649421f24fcbfbb2bf1f2cc0bd

// MT8163
  {"KFGIWI",                    "KFGIWI",           "full_giza",        "giza",         "mt8163",  1, K( 32), 0x00000, 0x00000, 0x00000, 0x000}, // 37b2b8c23089eef9d96a0ed7577982d7 - xorred?

// MT8735P
   {"Q27 4G",                   "Q27 4G",           "iBall Slide",      "",             "mt6735m", 1, K( 64), 0x00000, 0x00000, 0x00000, 0x000}, // d2bda05837aa9ff95074959efebb1bc7 - heap 11 also available

/**** SPREADTRUM ****/

//  generic name                ro.product.model    ro.product.name     board           platform   ion rowsize     ba2     ba1     ba0   rank
// SC9830A
   {"Archos 40 Helium",         "Archos 40 Helium", "SCAC40HE",     "sp9830aec_4m_h100","sc8830",  3, K( 32), 0x04000, 0x2000, 0x1000, 0x000}, // @home
   {"VOTO GT11 Pro",            "VOTO GT11 Pro",    "l305a_yusun_a8",  "l305a_yusun_a8","sc8830",  3, K( 32), 0x00000, 0x0000, 0x0000, 0x000}, // b2d69465420aa89cfc36439e9e13c203
   {"Samsung Galaxy J3 2016",   "SM-J320FN",        "j3xnltexx",        "SC9830I",      "sc8830",  2, K( 32), 0x00000, 0x0000, 0x0000, 0x000}, // @nfi


};

struct model unknown_model = 
   {"Unknown model",           "unknown",           "unknown",         "unknown",       "unknown", -1, K( 64), 0x0000, 0x0000, 0x0000, 0x000};


bool sigusr1;
void usr1_handler(int signal) {
    if (signal == SIGUSR1) {
        lprint("[SIGUSR1] OOM-killer\n");
        sigusr1 = true;
    }
}



/***********************************************************
 * ION DETECTOR 
 ***********************************************************/
std::vector<int> ION_autodetect_bruteforce(int max_order) {
    std::vector<int> ids;

    struct ion_handle_data handle_data;
    struct ion_allocation_data allocation_data;
    allocation_data.flags = 0;
    allocation_data.align = 0;

    if (!ion_fd) 
        ION_init();

    for (int id = 0; id < 32; id++) {
        allocation_data.heap_id_mask = 0x1 << id;
        
        
        allocation_data.len = K(4);
        lprint("[ION] id: %2u - 1. Alloc(4KB)... ", id);
        int err = ioctl(ion_fd, ION_IOC_ALLOC, &allocation_data);
        if (err) {
            lprint("-> Failed: %s\n", strerror(errno));
            continue;
        } 
        lprint("-> Success");
        handle_data.handle = allocation_data.handle;
        err = ioctl(ion_fd, ION_IOC_FREE, &handle_data);
        if (err) {
            lprint(" -> Could not free: %s", strerror(errno));
        }
        lprint("\n");


        allocation_data.len = ORDER_TO_B((max_order+1));
        lprint("[ION] id: %2u - 2. Alloc(%dMB)... ", id, allocation_data.len/1024/1024);
        err = ioctl(ion_fd, ION_IOC_ALLOC, &allocation_data);
        if (err) {
            lprint("-> Failed: %s ", strerror(errno));
        
            if (errno == ENOMEM) {
                lprint("<-- Candidate id: %u\n", id);
                ids.push_back(id);
            } else {
                lprint("<-- No candidate (weird error-code)\n");
            }
            continue;
        } 
        lprint("-> Success");
        handle_data.handle = allocation_data.handle;
        err = ioctl(ion_fd, ION_IOC_FREE, &handle_data);
        if (err) {
            lprint(" -> Could not free: %s", strerror(errno));
        }
        lprint("\n");
    }

    return ids;
}

/* Given a model (from which we are currently only interested in its platform),
 * this function determines possible ION heap ids that gives us physically
 * contiguous memory. This heap we are looking for is usually called the "system
 * contiguous heap".
 *
 * We perform auto-detection by trying all 32 possibilities. The contiguous
 * heap should allow us to allocate memory of order 1 (4KB), but should fail
 * with a ENOMEM error when trying to allocate memory of the maximum order
 * (usually 10 -> 4MB) + 1. 
 *
 * In addition, we search our 'database' for this device's platform: if we have
 * seen this platform before, we can probably use the same heap-id as was used
 * for that platform.
 *
 * We return a vector of ION heap ids that might give us physically contiguous
 * memory, ordered from most-likely to less-likely (i.e., if the database result
 * is among the possible ids provided by the auto-detect method, this id will be
 * the first item in the vector. The caller may then invoke the rowsize/bank
 * conflict detection code in a loop, iterating over the possible heap ids,
 * until rowsize and bank conflict detection is successful.
 *
 * Ideally, we use another timing side-channel to detect physically contiguous
 * memory (TODO).
 */
std::vector<int> ION_autodetect(std::string platform) {
    
    lprint("[ION] Looking for max block order in /proc/pagetypeinfo\n");
    int max_order = MAX_ORDER;
    std::ifstream pagetypeinfo("/proc/pagetypeinfo");
    pagetypeinfo.clear();
    pagetypeinfo.seekg(0, std::ios::beg);
    for (std::string line; getline(pagetypeinfo, line); ) {
        if (line.find("Page block order") != std::string::npos) {
            std::string max_order_str = line.substr( line.find(':') + 1, line.length());
            max_order = std::atoi(max_order_str.c_str());
            break;
        }
    }
    lprint("[ION] Assuming max order of %d == %dMB\n", max_order, ORDER_TO_MB(max_order));
    lprint("[ION] Running brute-force autodetect for contiguous system heap\n");
    std::vector<int> possible_heaps = ION_autodetect_bruteforce(max_order);
    lprint("[ION] List of possible ids: ");
    for (auto i: possible_heaps) lprint("%d ", i);
    lprint("\n");
   
    lprint("[ION] Searching list of known platforms for %s\n", platform.c_str());
/* These are ION heap ids for the system contiguous heap on devices that we had
 * physical access to. */ 
#define CHIPSET_MSM         21 // confirmed on a Nexus 5, Moto G (2013), Xiaomi Mi 4i, OnePlus One
#define CHIPSET_SPREADTRUM   3 // confirmed on a Archos 40 Helium
#define CHIPSET_KIRIN        1 // confirmed on a Huawei P9
#define CHIPSET_MEDIATEK     1 // confirmed on a Alcatel PIXI 4(4)

#define CHIPSET_APQ         21
#define CHIPSET_EXYNOS3      4
#define CHIPSET_EXYNOS4      4
#define CHIPSET_EXYNOS5      4
#define CHIPSET_MAKO        25
#define CHIPSET_TEGRA        2
#define CHIPSET_UNIVERSAL    1

/* For some chipsets that we have seen (results obtained from app-submissions),
 * we don't know the ION heap id gives us contiguous memory. */
#define CHIPSET_SEEN        -2

/* Some other chipsets will be completely unknown to us. */
#define CHIPSET_UNKNOWN     -1
    int id = CHIPSET_UNKNOWN;
         if (platform == "android-x86" ) id = CHIPSET_SEEN;
    else if (platform == "apq8084"     ) id = CHIPSET_APQ;
    else if (platform == "astar"       ) id = CHIPSET_SEEN;
    else if (platform == "baytrail"    ) id = CHIPSET_SEEN;
    else if (platform == "capri"       ) id = CHIPSET_SEEN;
    else if (platform == "clovertrail" ) id = CHIPSET_SEEN;

    else if (platform == "exynos3"     ) id = CHIPSET_EXYNOS3;
    else if (platform == "exynos4"     ) id = CHIPSET_EXYNOS4;
    else if (platform == "exynos5"     ) id = CHIPSET_EXYNOS5;

    else if (platform == "gmin"        ) id = CHIPSET_SEEN;
    else if (platform == "gxm"         ) id = CHIPSET_SEEN;
    else if (platform == "hawaii"      ) id = CHIPSET_SEEN;

    else if (platform == "hi3630"      ) id = CHIPSET_KIRIN;
    else if (platform == "hi3635"      ) id = CHIPSET_KIRIN;
    else if (platform == "hi3650"      ) id = CHIPSET_KIRIN;
    else if (platform == "hi6210sft"   ) id = CHIPSET_KIRIN;
    else if (platform == "hi6250"      ) id = CHIPSET_KIRIN;
    else if (platform == "hi6620oem"   ) id = CHIPSET_KIRIN;

    else if (platform == "java"        ) id = CHIPSET_SEEN;
    else if (platform == "jaws"        ) id = CHIPSET_SEEN;
    else if (platform == "k3v2oem1"    ) id = CHIPSET_SEEN;
    else if (platform == "kylin"       ) id = CHIPSET_SEEN;
    else if (platform == "meson6"      ) id = CHIPSET_SEEN;
    else if (platform == "meson8"      ) id = CHIPSET_SEEN;
    else if (platform == "montblanc"   ) id = CHIPSET_SEEN;
    else if (platform == "moorefield"  ) id = CHIPSET_SEEN;
    else if (platform == "mrvl"        ) id = CHIPSET_SEEN;

    else if (platform == "msm7k"       ) id = CHIPSET_MSM;
    else if (platform == "msm7x27a"    ) id = CHIPSET_MSM;
    else if (platform == "msm7x30"     ) id = CHIPSET_MSM;
    else if (platform == "msm7626a"    ) id = CHIPSET_MSM;
    else if (platform == "msm7630_surf") id = CHIPSET_MSM;
    else if (platform == "msm8084"     ) id = CHIPSET_MSM;
    else if (platform == "msm8226"     ) id = CHIPSET_MSM;
    else if (platform == "msm8610"     ) id = CHIPSET_MSM;
    else if (platform == "msm8660"     ) id = CHIPSET_MSM;
    else if (platform == "msm8909"     ) id = CHIPSET_MSM;
    else if (platform == "msm8916"     ) id = CHIPSET_MSM;
    else if (platform == "msm8937"     ) id = CHIPSET_MSM;
    else if (platform == "msm8952"     ) id = CHIPSET_MSM;
    else if (platform == "msm8953"     ) id = CHIPSET_MSM;
    else if (platform == "msm8960"     ) id = CHIPSET_MSM;
    else if (platform == "msm8974"     ) id = CHIPSET_MSM;
    else if (platform == "msm8992"     ) id = CHIPSET_MSM;
    else if (platform == "msm8994"     ) id = CHIPSET_MSM;
    else if (platform == "msm8996"     ) id = CHIPSET_MSM;

    else if (platform == "MT89_V7"     ) id = CHIPSET_MEDIATEK;
    else if (platform == "MT92_S1"     ) id = CHIPSET_MEDIATEK;
    else if (platform == "MT92_S7"     ) id = CHIPSET_MEDIATEK;
    else if (platform == "MT95_L905"   ) id = CHIPSET_MEDIATEK;
    else if (platform == "mt5890"      ) id = CHIPSET_MEDIATEK;
    else if (platform == "mt6572"      ) id = CHIPSET_MEDIATEK;
    else if (platform == "mt6580"      ) id = CHIPSET_MEDIATEK;
    else if (platform == "mt6582"      ) id = CHIPSET_MEDIATEK;
    else if (platform == "MT6589"      ) id = CHIPSET_MEDIATEK;
    else if (platform == "mt6592"      ) id = CHIPSET_MEDIATEK;
    else if (platform == "mt6595"      ) id = CHIPSET_MEDIATEK;
    else if (platform == "mt6735"      ) id = CHIPSET_MEDIATEK;
    else if (platform == "mt6735m"     ) id = CHIPSET_MEDIATEK;
    else if (platform == "mt6737m"     ) id = CHIPSET_MEDIATEK;
    else if (platform == "mt6737t"     ) id = CHIPSET_MEDIATEK;
    else if (platform == "mt6750"      ) id = CHIPSET_MEDIATEK;
    else if (platform == "mt6752"      ) id = CHIPSET_MEDIATEK;
    else if (platform == "mt6753"      ) id = CHIPSET_MEDIATEK;
    else if (platform == "mt6755"      ) id = CHIPSET_MEDIATEK;
    else if (platform == "mt6757"      ) id = CHIPSET_MEDIATEK;
    else if (platform == "mt6795"      ) id = CHIPSET_MEDIATEK;
    else if (platform == "mt6797"      ) id = CHIPSET_MEDIATEK;
    else if (platform == "mt8127"      ) id = CHIPSET_MEDIATEK;
    else if (platform == "mt8135"      ) id = CHIPSET_MEDIATEK;
    else if (platform == "mt8163"      ) id = CHIPSET_MEDIATEK;
    else if (platform == "mtk"         ) id = CHIPSET_MEDIATEK;
    else if (platform == "MTK6592T"    ) id = CHIPSET_MEDIATEK;

    else if (platform == "octopus"     ) id = CHIPSET_SEEN;

    else if (platform == "omap3"       ) id = CHIPSET_SEEN;
    else if (platform == "omap4"       ) id = CHIPSET_SEEN;

    else if (platform == "polaris"     ) id = CHIPSET_SEEN;
    else if (platform == "rhea"        ) id = CHIPSET_SEEN;

    else if (platform == "rk29xx"      ) id = CHIPSET_SEEN;
    else if (platform == "rk30xx"      ) id = CHIPSET_SEEN;
    else if (platform == "rk312x"      ) id = CHIPSET_SEEN;
    else if (platform == "rk2928"      ) id = CHIPSET_SEEN;
    else if (platform == "rk3188"      ) id = CHIPSET_SEEN;

    else if (platform == "s5pc110"     ) id = CHIPSET_SEEN;
    else if (platform == "sc6820i"     ) id = CHIPSET_SEEN;
    else if (platform == "sc6830"      ) id = CHIPSET_SEEN;
    else if (platform == "sc8830"      ) id = CHIPSET_SPREADTRUM;
    else if (platform == "scx15"       ) id = CHIPSET_SEEN;
    else if (platform == "sofia"       ) id = CHIPSET_SEEN;
    else if (platform == "sofia3g"     ) id = CHIPSET_SEEN;
    else if (platform == "sofia3gr"    ) id = CHIPSET_SEEN;

    else if (platform == "tegra"       ) id = CHIPSET_TEGRA;
    else if (platform == "tegra3"      ) id = CHIPSET_TEGRA;
    else if (platform == "tegra132"    ) id = CHIPSET_TEGRA;
    else if (platform == "tegra210_dragon") id = CHIPSET_TEGRA;

    else if (platform == "u2"          ) id = CHIPSET_SEEN;

    else if (platform == ""            ) id = CHIPSET_SEEN;

    if (id == CHIPSET_UNKNOWN) {
        lprint("[ION] I have never seen this platform before\n");
    } else if (id == CHIPSET_SEEN) {
        lprint("[ION] I have seen this platform before, but do not know the system-contiguous heap id\n");
    } else {
        lprint("[ION] I have seen this platform before, with system-contiguous heap id: %d\n", id);

        bool found = false;
        for (auto it  = possible_heaps.begin(); 
                  it != possible_heaps.end();
                ++it) {
            if (*it == id) {
                lprint("[ION] Moving this id to the front of the list\n");
                std::rotate(possible_heaps.begin(), it, it + 1);
                found = true;
                break;
            }
        }
        if (!found) {
            lprint("[ION] This id likely won't work, pushing it to the end of the list\n");
            possible_heaps.push_back(id);
        }
    }

    lprint("[ION] List of possible ids: ");
    for (auto i: possible_heaps) lprint("%d ", i);
    lprint("\n");

    return possible_heaps;
} 

    




/***********************************************************
 * CLASS HISTOGRAM 
 ***********************************************************/

Histogram::Histogram(std::map<int, std::set<uint8_t *>> histogram) {
    this->histogram = histogram;

    this->deltas.clear();
    for (auto it: histogram) {
        int delta = it.first;
        int count = it.second.size();
    
        for (int i = 0; i < count; i++) {
            this->deltas.push_back(delta);
        }
    }

    if (this->deltas.size() == 0) 
        this->deltas.push_back(0);

    auto const i1 = this->deltas.size() / 4;
    auto const i2 = this->deltas.size() / 2;
    auto const i3 = i1 + i2;

    if (this->deltas.size() % 2 == 1) {
        this->q1 = this->deltas[i1];
        this->median = this->deltas[i2];
        this->q3 = this->deltas[i3];
    } else {
        this->q1 =     (this->deltas[i1] + this->deltas[i1-1]) / 2;
        this->median = (this->deltas[i2] + this->deltas[i2-1]) / 2;
        this->q3 =     (this->deltas[i3] + this->deltas[i3-1]) / 2;
    }
    
    this->variations.clear();
    for (auto delta: this->deltas) {
        int delta_min_two_percent = delta - (0.02 * delta);
        int delta_plus_two_percent = delta + (0.02 * delta);
        
        bool delta_seen = false;
        for (auto variation: this->variations) {
            if (variation >= delta_min_two_percent &&
                variation <= delta_plus_two_percent) {
                delta_seen = true;
            }
        }
        if (!delta_seen) {
            this->variations.push_back(delta);
        }
    }
}

Histogram* Histogram::getSubHistogram(std::set<uint8_t *> candidates) {
    std::map<int, std::set<uint8_t *>> sub;
    for (auto it: this->histogram) {
        int delta = it.first;
        std::set<uint8_t *> addresses = it.second;

        for (auto addr: addresses) {
            if (candidates.count(addr)) {
                sub[delta].insert(addr);
            }
        }
    }
    return new Histogram(sub);
}

int Histogram::getMin() {
    return histogram.begin()->first;
}
int Histogram::getMax() {
    return histogram.rbegin()->first;
}
int Histogram::getMed() {
    return this->median;
}
std::vector<int> Histogram::sorted_deltas() {
    std::map<uint8_t *, int> rev_map;

    for (auto it: this->histogram) {
        int delta = it.first;
        std::set<uint8_t *>addresses = it.second;
        for (auto addr: addresses) {
            rev_map[addr] = delta;
        }
    }

    std::vector<int> result;
    for (auto it: rev_map) {
        result.push_back(it.second);
    }
    return result;
}
int Histogram::getQ1() {
    return this->q1;
}
int Histogram::getQ3() {
    return this->q3;
}

bool Histogram::isNormal() {
    double sum = std::accumulate(this->deltas.begin(), this->deltas.end(), 0.0);
    double mean = sum / this->deltas.size();

    std::vector<double> diff(this->deltas.size());
    std::transform(this->deltas.begin(), this->deltas.end(), diff.begin(), [mean](double x) { return x - mean; });
    double sq_sum = std::inner_product(diff.begin(), diff.end(), diff.begin(), 0.0);
    double stdev = std::sqrt(sq_sum / this->deltas.size());

    int one_dev   = 0;
    int two_dev   = 0;
    int three_dev = 0;
    for (auto it: this->deltas) {
             if (it > (mean - 1*stdev) && it < (mean + 1*stdev)) one_dev++;
        else if (it > (mean - 2*stdev) && it < (mean + 2*stdev)) two_dev++;
        else if (it > (mean - 3*stdev) && it < (mean + 3*stdev)) three_dev++;
    }
    double one_dev_p   = (double) (one_dev                  ) / deltas.size();
    double two_dev_p   = (double) (one_dev+two_dev          ) / deltas.size();
    double three_dev_p = (double) (one_dev+two_dev+three_dev) / deltas.size();
    if (one_dev_p < 0.68 || two_dev_p < 0.95 || three_dev_p < 0.99) 
        return false;
    return true;
}

int Histogram::getVariation() {
    return this->variations.size();
}

int Histogram::getTreshold() {
    std::map<int, int> distances;

    int  max_distance = 0;
    int  max_delta = 0;
    int last_delta = 0;

    for (auto delta: this->deltas) {
        int distance = delta - last_delta;
        if (distance > max_distance && last_delta >= this->q3) {
            max_distance = distance;
            max_delta = last_delta;
        }
        last_delta = delta;
    }

    return (max_delta + max_distance/2);
}

int Histogram::count() {
    return countUp(-1);
}
int Histogram::countUp(int base) {
    int sum = 0;
    for (auto it: histogram) {
        int delta = it.first;
        int count = it.second.size();
        if (delta >= base) sum += count;
    }
    return sum;
}
void Histogram::print() {
    for (auto it: histogram) {
        int delta = it.first;
        int count = it.second.size();
        lprint("%3d, %4d\n", delta, count);
    }
      lprint("[BC] min: %d | q1: %d | med: %d | q3: %d | max: %d\n", getMin(), q1, median, q3, getMax());
}

std::set<uint8_t *> Histogram::getConflicts(int treshold) {
    std::set<uint8_t *> conflicts;
    for (auto it: histogram) {
        int delta = it.first;
        std::set<uint8_t *> addresses = it.second;
        if (delta >= treshold) {
            conflicts.insert(addresses.begin(), addresses.end());
        }
    }
    return conflicts;

}

bool getAccessTimerExpired;

void getAccessTimerSignal(int signal) {
    if (signal == SIGALRM) {
        lprint("\n[SIGALRM]\n");
        getAccessTimerExpired = true;
        alarm(0);
    }
}


/***********************************************************
 * CLASS BANKCONFLICTS
 ***********************************************************/
Histogram *BankConflicts::getAccessTimes(uint8_t *base, std::vector<uint8_t *> candidates, 
                                            bool do_print, int timer) {
    std::map<int, std::set<uint8_t *>> histogram;


    for (auto candidate: candidates) {

        /* Install a timer, if provided */
        getAccessTimerExpired = false;
        struct sigaction new_action, old_action;
        if (timer) {
            new_action.sa_handler = getAccessTimerSignal;
            sigemptyset(&new_action.sa_mask);
            new_action.sa_flags = 0;
            sigaction(SIGALRM, &new_action, &old_action);
            alarm(timer);
        }

        std::vector<uint64_t> deltas;
        for (int m = 0; m < this->measurements; m++) {
            int delta = hammer((volatile uint8_t *)base, 
                               (volatile uint8_t *)candidate, 
                               this->count,
                               this->fence);
            deltas.push_back(delta);

            if (getAccessTimerExpired) {
                alarm(0);
                sigaction(SIGALRM, &old_action, NULL);
                return NULL;
            }
        }
        int median = compute_median(deltas);
        histogram[median].insert(candidate);
        if (do_print) 
            lprint("%3d ", median);

        /* Restore the signal handler for SIGALRM */
        if (timer) {
            alarm(0);
            sigaction(SIGALRM, &old_action, NULL);
        }
    }

    if (do_print) 
        lprint("\n");

    Histogram *h = new Histogram(histogram);
    return h;
}





void BankConflicts::determineCount(uint8_t *base, int min_loop_time_us) {
    lprint("[Count] Determining loop count so that accessing two addresses takes at least %dus\n", min_loop_time_us);
    this->count = 100;

    std::vector<uint8_t *>candidates;
    candidates.push_back(base);

    /* Total time for accessing the same address twice, in a loop of <count>
     * should exceed <min_loop_time_us> us */
    int loop_time = 0;

    while (loop_time < min_loop_time_us) {
        uint64_t t1 = get_us();
        getAccessTimes(base, candidates, false);
        uint64_t t2 = get_us();
        loop_time = t2 - t1;

        lprint("[Count] #measurements: %d | #count: %7d | delta: %7dus\n", this->measurements, this->count, loop_time);

        double multiplyer = (double)min_loop_time_us / (double)loop_time;
        if (this->count * multiplyer > this->count + 100) 
            this->count = this->count * multiplyer;
        else 
            this->count += 100;
    }
}

bool BankConflicts::findRowsize(uint8_t *base, int len) {

    int min_rowsize = K(16);
    int max_rowsize = hibit(len/2);
    if (max_rowsize < min_rowsize) {
        lprint("Not enough contiguous memory for rowsize detection\n");
        return false;
    }
      
    std::vector<uint8_t *> candidates;

    for (int tries = 0; tries < MAX_TRIES; tries++) {

      /***** Method A *****
       * This method collects access times for <base> and pages <base+rowsize> where
       * rowsize varies from min_rowsize to max_rowsize while doubling each
       * iteration. We then accept only two access times: low and high ones.
       *
       * Tested on:
       *                     +16K +32K +64K +128K treshold  #count fence cpu
       * - Huawei P9          155  155  183   183      169   38947     0   0
       * - LG Nexus 5X        426  426  469   469      447   17288     0   0
       * - LG G5              218  218  232   232      225   24901     0   0
       * - LG Nexus 5           8    8   70    70       39  597616     0   0
       * - LG G4              404  404  446   446      425   33533     0   0
       * - Moto G 2013         74  127  127   127      100   67200     0   0
       * - Alcatel PIXI 4(4)    -    -    -     -        -       -     -   -
       * - Archos 40 Helium    98  156  156   156      127   50637     0   0
       * - Xiaomi Mi 4i       131  131  158   158      144   47409     0   0
       * - HTC Desire 510      83  124  124   124      104   59362     0   0 
       */

//#define SKIP_A
//#define SKIP_B


#ifdef SKIP_A
      goto method_b;
#endif
      lprint("\n");
      lprint("------------------------------------+\n");
      lprint("[BC] Determining rowsize - Method A |\n");
      lprint("------------------------------------+\n");
      candidates.clear();
      for (int rowsize = min_rowsize; rowsize <= max_rowsize; rowsize = rowsize * 2) {
          candidates.push_back(base + rowsize);
      }

      for (auto cpu: this->cpus) {
        pincpu(cpu);
        for (int fencing = 0; fencing < FENCING_OPTIONS; fencing++) {
            lprint("_______________________________________\n");
            lprint("[BC] Try %d/%d - cpu %d - fencing option %d\n", tries+1, MAX_TRIES, cpu, fencing);
            this->fence = fencing;

            int min_loop_time_us = 1000000; // 1 second
            this->determineCount(base, min_loop_time_us); 

            /* Generate access times for <base, base+16K>, <base, base+32K>, <base, base+64K>, ... */
            Histogram *h = getAccessTimes(base, candidates, true, 10); // 10 second timeout
            if (sigusr1) return false;
            if (!h) continue;
//          h->print();


            /* We expect exactly two access times: 
             * -  <low>: no bank conflict
             * - <high>:    bank conflict */
            int variation = h->getVariation();
            lprint("[BC] Variation: %d\n", variation);
            if (variation < 2) {
                lprint("[BC] -> not enough\n");
                continue;
            } else if (variation > 2) {
                lprint("[BC] -> too much\n");
                continue;
            }

            int  low = h->getMin();
            int high = h->getMax();
            int treshold = low + ((high - low) / 2);

            /* Loop over the access times to confirm the pattern: 
             *   <low>, <low>, <high>, <high>, <high>
             * Here, each access time indicates the next possible rowsize
             * boundary. As soon as we see a <high> access time, we know the
             * rowsize. */
            int rowsize = min_rowsize;
            bool treshold_reached = false;
            for (auto delta: h->sorted_deltas())  {
                if (!treshold_reached) {
                    /* We have not seen <high> yet */
                    if (delta < treshold) {
                        /* <low>, <low>, ... */
                        rowsize = rowsize * 2;
                    } else {
                        /* <low>, <low>, <high>, ... */
                        treshold_reached = true;
                    } 
                } else {
                    /* We have seen <high> */
                    if (delta < treshold) {
                        /* <low>, <low>, <high>, <low>, ... */
                        treshold_reached = false;
                        break;
                    } 
                }
            }

            if (!treshold_reached) 
                continue;
            
            lprint("[BC] --> Detected  rowsize: %3dKB\n", rowsize);
            lprint("[BC] --> Detected treshold: %3d\n", treshold);

            this->our_model->rowsize      = rowsize;
            this->our_model->ba2          = 0;
            this->our_model->ba1          = 0;
            this->our_model->ba0          = 0;
            this->our_model->rank         = 0;
            this->our_model->treshold     = treshold;
            this->our_model->measurements = this->measurements;
            this->our_model->count        = this->count;
            this->our_model->fence        = this->fence;
            this->our_model->cpu          = cpu;

            return true;
        } // FENCING LOOP
      } // CPUSPEED LOOP
#ifdef SKIP_A
method_b:   
#endif
#ifdef SKIP_B
      continue;
#endif
      /***** Method B *****
       * This method collects access times for <base> and pages <base+x> where
       * min_rowsize < x < max_rowsize*2 and computes a treshold. We then look
       * at subsets of the access times and check whether the computed treshold
       * results in a sane amount of bank conflicts.
       
       * Tested on:
       *                     pages conflicts banks treshold  count fence
       * - Huawei P9            16         2     8      175   9568     2
       * - LG Nexus 5X          16         2     8      459   3280     2
       * - LG G5                16         2     8      330   2580     2
       * - LG Nexus 5           16         2     8       89  20427     2
       * - LG G4                16         2     8      435   3383     2
       * - Moto G 2013           8         1     8      112  14842     2
       * - Alcatel PIXI 4(4)     8         1     8      159   9610     2
       * - Archos 40 Helium      8         1     8      144  11518     2
       * - Xiaomi Mi 4i         16         2     8      153  10629     2
       * - HTC Desire 510        8         1     8      108  14584     2
       */
      lprint("\n");
      lprint("------------------------------------+\n");
      lprint("[BC] Determining rowsize - Method B |\n");
      lprint("------------------------------------+\n");

      for (auto cpu: this->cpus) {
        pincpu(cpu);
        std::set<int> rowsizes;
        for (int fencing = 0; fencing < FENCING_OPTIONS; fencing++) {
            lprint("_______________________________________\n");
            lprint("[BC] Try %d/%d - CPU %d - fencing option %d\n", tries+1, MAX_TRIES, cpu, fencing);
            this->fence = fencing;
            this->determineCount(base, 250000); // minimum loop time: .25 seconds
    
            lprint("[BC] Collecting access times\n");
            candidates.clear();
            for (int offset = min_rowsize; offset < 2*max_rowsize; offset += PAGESIZE) {
                candidates.push_back(base + offset);
            }
            Histogram *h = getAccessTimes(base, candidates, true, 3); // 3 second timeout
            if (sigusr1) return false;
            if (!h) continue;
//          h->print();

            int treshold = h->getTreshold();
            lprint("[BC] Treshold: %d\n", treshold);;
            if (treshold == 0) continue;

            for (int rowsize = min_rowsize; rowsize <= max_rowsize; rowsize = rowsize * 2) {
                int pages = rowsize / PAGESIZE;

                std::set<uint8_t *>candidate_set;
                for (int offset = 0; offset < rowsize; offset += PAGESIZE) {
                    candidate_set.insert(base + rowsize + offset);
                }
                Histogram *subh = h->getSubHistogram(candidate_set);

                int banks = -1;

                int conflicts      = subh->countUp(treshold);
                if (conflicts) 
                    banks          = pages / conflicts;
                int conflicts_bits = std::bitset<32>(conflicts).count();
                int banks_bits     = std::bitset<32>(banks).count();

                lprint("[BC] rowsize: %6d (pages: %2d) | conflicts: %d (bits: %d) | banks: %2d (bits: %2d)\n",
                             rowsize,      pages,        conflicts, conflicts_bits, banks, banks_bits);

                if (conflicts &&
                    conflicts_bits == 1 &&
                        banks_bits == 1 &&
                        banks      >= 8 &&
                        banks      <= 16 &&
                    conflicts      < pages) {
                    
                    lprint("[BC] --> Detected  rowsize: %3dKB\n", rowsize/1024);
                    lprint("[BC] --> Detected treshold: %3d\n", treshold);

                    this->our_model->rowsize      = rowsize;
                    this->our_model->ba2          = 0;
                    this->our_model->ba1          = 0;
                    this->our_model->ba0          = 0;
                    this->our_model->rank         = 0;
                    this->our_model->treshold     = treshold;
                    this->our_model->measurements = this->measurements;
                    this->our_model->count        = this->count;
                    this->our_model->fence        = this->fence;
                    this->our_model->cpu          = cpu;

                    rowsizes.insert(rowsize);
                    break;
                }
            }
            if (rowsizes.size() > 1) break; // out of FENCING LOOP
        } // FENCING LOOP
          
        if (rowsizes.size() == 1)
            return true;
        if (rowsizes.size() > 1) {
            lprint("[BC] Detected different rowsizes.\n");
            continue;
        }

      } // CPUSPEED LOOP
    
    } // TRIES LOOP

    lprint("Failed to detect the rowsize\n");
    return false;
}


/* findMask() only works for addressing functions that use single bits for BA0,
 * BA1, BA2, ... We currently cannot detect whether a chipset xors two bits,
 * which is why this function should not be used.
 * 
 * Possible TODOs:
 *
 * 1) Only run findMask() when we are sure that single bits are used as
 * selector. Since ION gives us at most 4MB physically contiguous memory that is
 * also aligned to a 4MB boundary, we cannot determine whether high bits (>21)
 * are used without access to the pagemap. This should be fine since we won't
 * hammer accross 4MB pages. A simple check would be to ensure that reading from
 * <base>,<base+n*rowsize> always results in a bank conflict.
 *
 * 2) Modify findMaks() to also support two bit selectors for BA0, BA1, ... To
 * do this, we should read from <base>,<base+n*rowsize> until we see an exact
 * one to one mapping between the two rows: <base+x>,<base+n*rowsize+x> should
 * always hit a bank conflict. Note that this may happen for n=0, which is why
 * we should exhaust an entire 4MB chunk doing this.
 *
 * For now, it seems that finding the bank mask was merely a nice brain
 * excersise.
 */
bool BankConflicts::findMask() {
       
    struct ion_data data1;
    if (ION_alloc_mmap(&data1, M(1), this->our_model->ion_heap) < 0) {
        lprint("[BC] Could not allocate 1MB\n");
        return false;
    }
    

    int min_conflicts = 8;
    int max_conflicts = this->our_model->rowsize / CACHELINE_SIZE;

    std::vector<Histogram *> histograms;

    lprint("[BC] Looking for conflicts in last bank\n");
    for (int tries = 0; tries < MAX_TRIES; tries++) {

      for (auto cpu: this->cpus) {
        cpu = 0;
        pincpu(cpu);
        for (int fencing = 0; fencing < FENCING_OPTIONS; fencing++) {
            fencing = 1;
            this->fence = fencing;

            int min_us;
            if (tries == 0) min_us =   20000; // 0.02s
            if (tries == 1) min_us =   50000; // 0.05s
            if (tries == 2) min_us =  100000; // 0.10s
            this->determineCount((uint8_t *)data1.mapping, min_us);

            lprint("___________________________________________\n");
            lprint("[BC] Try %d/%d - cpu %d - fencing option %d - count %d\n", 
                        tries+1, MAX_TRIES, cpu, fencing, this->count);
    
            std::vector<uint8_t *> candidates;
            for (int offset = this->our_model->rowsize; offset < 2*this->our_model->rowsize; offset += CACHELINE_SIZE) {
                candidates.push_back((uint8_t *)data1.mapping + offset);
            }   
        
            Histogram *h = getAccessTimes((uint8_t *) ((uint64_t)data1.mapping + this->our_model->rowsize - 1), candidates, true);
            h->print();

            int treshold = h->getTreshold();
            lprint("Treshold %d\n", treshold);
        
            std::set<uint8_t *>conflicts = h->getConflicts(treshold); 
            int conflict_count = (int) conflicts.size();
            lprint("[BC] - #conflicts: %d (min: %d | max: %d)\n", conflict_count, min_conflicts, max_conflicts);

            if (conflict_count > min_conflicts && 
                conflict_count < max_conflicts && 
                std::bitset<32>(conflict_count).count() == 1) {
                lprint("looks good\n");
            } else {
                continue;
            }

            int logical_banks = h->count() / h->countUp(treshold);
            int cachelines_in_bank = conflict_count;
            lprint("[BC] Number of logical banks: %d\n", logical_banks);
            lprint("[BC] Cachelines in bank: %d\n", cachelines_in_bank);

            int selector = this->our_model->rowsize - 1;
            lprint("[BC] Computing bank select bits. Start: %x\n", selector);
            for (auto addr: conflicts) {
                uintptr_t rel_addr = (uintptr_t)addr - (uintptr_t) data1.mapping;
                selector &= rel_addr;
            }
            std::string selector_str = std::bitset<24>(selector).to_string();
            lprint("[BC] Found selector: %x (%s)\n", selector, selector_str.c_str());
            
           



            
            lprint("Moving one row away\n"); 
            candidates.clear();
            for (int offset = this->our_model->rowsize*2; offset < 3*this->our_model->rowsize; offset += CACHELINE_SIZE) {
                candidates.push_back((uint8_t *)data1.mapping + offset);
            }   
        
            h = getAccessTimes((uint8_t *) ((uint64_t)data1.mapping + this->our_model->rowsize - 1), candidates, true);
            h->print();

            treshold = h->getTreshold();
            lprint("Treshold %d\n", treshold);
        
            conflicts = h->getConflicts(treshold); 
            conflict_count = (int) conflicts.size();
            lprint("[BC] - #conflicts: %d (min: %d | max: %d)\n", conflict_count, min_conflicts, max_conflicts);

            if (conflict_count > min_conflicts && 
                conflict_count < max_conflicts && 
                std::bitset<32>(conflict_count).count() == 1) {
                lprint("looks good\n");
            } else {
                continue;
            }

            logical_banks = h->count() / h->countUp(treshold);
            cachelines_in_bank = conflict_count;
            lprint("[BC] Number of logical banks: %d\n", logical_banks);
            lprint("[BC] Cachelines in bank: %d\n", cachelines_in_bank);

            int selector2 = this->our_model->rowsize - 1;
            lprint("[BC] Computing bank select bits. Start: %x\n", selector);
            for (auto addr: conflicts) {
                uintptr_t rel_addr = (uintptr_t)addr - (uintptr_t) data1.mapping;
                selector2 &= rel_addr;
            }
            selector_str = std::bitset<24>(selector2).to_string();
            lprint("[BC] Found selector: %x (%s)\n", selector2, selector_str.c_str());

            if (selector != selector2) {
                lprint("looks liked an XOR with the rowsize bit\n");
                int xorred_bit = selector ^ selector2;
                int other_bit = this->our_model->rowsize*2;
                lprint("xorred_bit: %x\n", xorred_bit);
                lprint("other_bit: %x\n", other_bit);
                lprint("ba0 = %x\n", (xorred_bit | other_bit));
            }
                //*_mask = selector;






            lprint("Moving three rows away\n"); 
            candidates.clear();
            for (int offset = this->our_model->rowsize*4; offset < 5*this->our_model->rowsize; offset += CACHELINE_SIZE) {
                candidates.push_back((uint8_t *)data1.mapping + offset);
            }   
        
            h = getAccessTimes((uint8_t *) ((uint64_t)data1.mapping + this->our_model->rowsize - 1), candidates, true);
            h->print();

            treshold = h->getTreshold();
            lprint("Treshold %d\n", treshold);
        
            conflicts = h->getConflicts(treshold); 
            conflict_count = (int) conflicts.size();
            lprint("[BC] - #conflicts: %d (min: %d | max: %d)\n", conflict_count, min_conflicts, max_conflicts);

            if (conflict_count > min_conflicts && 
                conflict_count < max_conflicts && 
                std::bitset<32>(conflict_count).count() == 1) {
                lprint("looks good\n");
            } else {
                continue;
            }

            logical_banks = h->count() / h->countUp(treshold);
            cachelines_in_bank = conflict_count;
            lprint("[BC] Number of logical banks: %d\n", logical_banks);
            lprint("[BC] Cachelines in bank: %d\n", cachelines_in_bank);

            int selector3 = this->our_model->rowsize - 1;
            lprint("[BC] Computing bank select bits. Start: %x\n", selector);
            for (auto addr: conflicts) {
                uintptr_t rel_addr = (uintptr_t)addr - (uintptr_t) data1.mapping;
                selector3 &= rel_addr;
            }
            selector_str = std::bitset<24>(selector3).to_string();
            lprint("[BC] Found selector: %x (%s)\n", selector3, selector_str.c_str());

            if (selector != selector3) {
                lprint("looks liked an XOR with the rowsize bit\n");
                int xorred_bit = selector ^ selector3;
                int other_bit = this->our_model->rowsize*4;
                lprint("xorred_bit: %x\n", xorred_bit);
                lprint("other_bit: %x\n", other_bit);
                lprint("ba1 = %x\n", (xorred_bit | other_bit));
            }



            lprint("Moving eight rows away\n"); 
            candidates.clear();
            for (int offset = this->our_model->rowsize*8; offset < 9*this->our_model->rowsize; offset += CACHELINE_SIZE) {
                candidates.push_back((uint8_t *)data1.mapping + offset);
            }   
        
            h = getAccessTimes((uint8_t *) ((uint64_t)data1.mapping + this->our_model->rowsize - 1), candidates, true);
            h->print();

            treshold = h->getTreshold();
            lprint("Treshold %d\n", treshold);
        
            conflicts = h->getConflicts(treshold); 
            conflict_count = (int) conflicts.size();
            lprint("[BC] - #conflicts: %d (min: %d | max: %d)\n", conflict_count, min_conflicts, max_conflicts);

            if (conflict_count > min_conflicts && 
                conflict_count < max_conflicts && 
                std::bitset<32>(conflict_count).count() == 1) {
                lprint("looks good\n");
            } else {
                continue;
            }

            logical_banks = h->count() / h->countUp(treshold);
            cachelines_in_bank = conflict_count;
            lprint("[BC] Number of logical banks: %d\n", logical_banks);
            lprint("[BC] Cachelines in bank: %d\n", cachelines_in_bank);

            int selector4 = this->our_model->rowsize - 1;
            lprint("[BC] Computing bank select bits. Start: %x\n", selector);
            for (auto addr: conflicts) {
                uintptr_t rel_addr = (uintptr_t)addr - (uintptr_t) data1.mapping;
                selector4 &= rel_addr;
            }
            selector_str = std::bitset<24>(selector4).to_string();
            lprint("[BC] Found selector: %x (%s)\n", selector4, selector_str.c_str());

            if (selector != selector4) {
                lprint("looks liked an XOR with the rowsize bit\n");
                int xorred_bit = selector ^ selector4;
                int other_bit = this->our_model->rowsize*8;
                lprint("xorred_bit: %x\n", xorred_bit);
                lprint("other_bit: %x\n", other_bit);
                lprint("ba2 = %x\n", (xorred_bit | other_bit));
            }




            lprint("[BC] Verifying...\n");
    //        bool verified = verifyMask(base, len, rowsize, treshold, *_mask);
    //        lprint("[BC] Verified? %d\n", verified);
    //
            exit(0);
        }
      }
    }


    lprint("STOP\n");
    exit(0);

    return true;
}

int readSettings(const char *filename, struct model *m) {
    FILE *f = fopen(filename,"r");
    if (f == NULL)
        return 0;

    if (fscanf(f, "%d\n", &m->ion_heap)     != 1) goto err;
    if (fscanf(f, "%d\n", &m->rowsize)      != 1) goto err;
    if (fscanf(f, "%x\n", &m->ba2)          != 1) goto err;
    if (fscanf(f, "%x\n", &m->ba1)          != 1) goto err;
    if (fscanf(f, "%x\n", &m->ba0)          != 1) goto err;
    if (fscanf(f, "%x\n", &m->rank)         != 1) goto err;

    if (fscanf(f, "%d\n", &m->treshold)     != 1) goto err;
    if (fscanf(f, "%d\n", &m->measurements) != 1) goto err;
    if (fscanf(f, "%d\n", &m->count)        != 1) goto err;
    if (fscanf(f, "%d\n", &m->fence)        != 1) goto err;
    if (fscanf(f, "%d\n", &m->cpu)          != 1) goto err;

    if (fscanf(f, "%d\n", &m->slowest_cpu)  != 1) goto err;
    if (fscanf(f, "%d\n", &m->fastest_cpu)  != 1) goto err;
    fclose(f);   
    return 1;

err:
    fclose(f);
    return 0;
}

void writeSettings(const char *filename, struct model *m) {
    FILE *f = fopen(filename,"w");
    if (f != NULL) {
        fprintf(f, "%d\n", m->ion_heap);
        fprintf(f, "%d\n", m->rowsize);
        fprintf(f, "%x\n", m->ba2);
        fprintf(f, "%x\n", m->ba1);
        fprintf(f, "%x\n", m->ba0);
        fprintf(f, "%x\n", m->rank);

        fprintf(f, "%d\n", m->treshold);
        fprintf(f, "%d\n", m->measurements);
        fprintf(f, "%d\n", m->count);
        fprintf(f, "%d\n", m->fence);
        fprintf(f, "%d\n", m->cpu);

        fprintf(f, "%d\n", m->fastest_cpu);
        fprintf(f, "%d\n", m->slowest_cpu);
        fclose(f);   
    }
}


bool BankConflicts::verifyMask(uint8_t *base, int len, int rowsize, int treshold, uint32_t mask) {
    lprint("[BC] Verifying Bank Mask\n");

    std::string mask_str = std::bitset<24>(mask).to_string();
    lprint("[BC]    base: %p\n", base);
    lprint("[BC]     len: %5dKB\n", len/1024);
    lprint("[BC] rowsize: %5dKB\n", rowsize/1024);
    lprint("[BC]    mask: %5x (%sb)\n", mask, mask_str.c_str());
    int logical_banks = (1 << std::bitset<32>(mask).count());
    int cachelines_in_bank = rowsize / logical_banks / CACHELINE_SIZE;
    lprint("[BC]  #banks: %5d (logical)\n", logical_banks);
    lprint("[BC]  #lines: %5d (per bank)\n", cachelines_in_bank);

    int bank = 0;
    for (uint32_t bank_selector = 0; 
                  bank_selector < (uint32_t) len; 
                  bank_selector++) {
        if ((bank_selector & mask) == bank_selector) {
            uint8_t *addr1 = (uint8_t *) ((uint64_t) base + bank_selector);
            std::set<uint8_t *>  conflict_candidates;
            std::set<uint8_t *>noconflict_candidates;
            for (int offset = rowsize; offset < len; offset += CACHELINE_SIZE) {
                uint8_t *addr2 = (uint8_t *) ((uint64_t) base + offset);
                if ((offset & mask) == bank_selector) 
                    conflict_candidates.insert(addr2);
                else 
                    noconflict_candidates.insert(addr2);
            }
            if (  conflict_candidates.size() < (size_t) cachelines_in_bank ||
                noconflict_candidates.size() < (size_t) cachelines_in_bank) {
                lprint("Not enough conflict candidates found\n");
                return false;
            }
            uint8_t *  conflict_addr = random_element(  conflict_candidates);
            uint8_t *noconflict_addr = random_element(noconflict_candidates);
            
            lprint("[BC] Bank %2d | Select: %5x | addr1: %p | conflict: %p | non-conflict: %p ? ", 
                    bank, bank_selector, addr1, conflict_addr, noconflict_addr);

            std::vector<uint8_t *> candidates;
            candidates.push_back(  conflict_addr);
            candidates.push_back(noconflict_addr);
            Histogram *h = getAccessTimes(addr1, candidates, false);
            std::set<uint8_t *> conflicts = h->getConflicts(treshold);
            if (conflicts.size() != 1) {
                lprint("Weird number of conflicts: %zu\n", conflicts.size());
                return false;
            }
            if (conflicts.count(conflict_addr) != 1) {
                lprint("Expected conflict, but non measured\n");
                return false;
            }

            lprint("ok\n");


            bank++;
        }
    }

    return true;
}


void dump_hardware(struct model *m) {
    lprint("\n");
    lprint("=============================================================\n");
    lprint("        HARDWARE\n");
    lprint("=============================================================\n");
    lprint("[RS] Model:\n");
    lprint("[RS] - ro.product.model:  %s\n", m->model.c_str());
    lprint("[RS] - ro.product.name:   %s\n", m->name.c_str());
    lprint("[RS] - ro.product.board:  %s\n", m->board.c_str());
    lprint("[RS] - ro.board.platform: %s\n", m->platform.c_str());
    lprint("[RS] CPU:\n");
    lprint("[RS] - count:   %d\n", m->cpus);
    lprint("[RS] - fastest: %d\n", m->fastest_cpu);
    lprint("[RS] - slowest: %d\n", m->slowest_cpu);

    lprint("[RS] Contents of /proc/cpuinfo:\n");
    dumpfile("/proc/cpuinfo");

    lprint("[RS] Contents of /proc/version:\n");
    dumpfile("/proc/version");

    lprint("[RS] Content of /proc/sys/vm/overcommit_memory:\n");
    dumpfile("/proc/sys/vm/overcommit_memory");

    lprint("[RS] Content of /proc/meminfo:\n");
    dumpfile("/proc/meminfo");

    lprint("[RS] Output of ls -l /sys/kernel/mm:\n");
    lprint("%s",run("/system/bin/ls -l /sys/kernel/mm/").c_str());

    lprint("[RS] Output of ls -l /proc/self/pagemap:\n");
    lprint("%s",run("/system/bin/ls -l /proc/self/pagemap").c_str());

    lprint("[RS] Testing whether we can use pagemap for normal pages:\n");
    m->pagemap = 0x0;
    void *tmap = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC,
                                  MAP_POPULATE | MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    uintptr_t tmap_phys = get_phys_addr((uintptr_t) tmap);
    if (tmap_phys) {
        m->pagemap |= 0x1;
        lprint("[RS] - Pagemap available for normal pages (virt %p has phys %p\n", tmap, (void *)tmap_phys);
    } else {
        lprint("[RS] - Pagemap not available for normal pages\n");
    }
    munmap(tmap, 4096);
   

    lprint("[RS] Testing whether we can use pagemap for ION pages:\n");
    struct ion_data data;
    if (ION_alloc_mmap(&data, K(64), m->ion_heap) < 0) {
        lprint("[RS] - Failed to allocate 64K - invalid ION heap?\n");
    } else {
        uintptr_t virt1 = (uintptr_t) data.mapping;
        uintptr_t virt2 = (uintptr_t) data.mapping + K(32);
        uintptr_t phys1 = get_phys_addr(virt1);
        uintptr_t phys2 = get_phys_addr(virt2);
        lprint("[RS] - 64K ION chunk at virt: %p | phys: %p\n", (void *) virt1, (void *) phys1);
        lprint("[RS] - 64K ION chunk +32K at: %p | phys: %p\n", (void *) virt2, (void *) phys2);

        if (!phys1) {
            lprint("[RS] - Pagemap not available for ION pages\n");
        } else {
            m->pagemap |= 0x2;
            lprint("[RS] - Pagemap available for first ION page\n");

            if (phys2 - phys1 == K(32)) {
                m->pagemap |= 0x4;
                lprint("[RS] - Pagemap available for random ION pages\n");
            }
        }

        ION_clean(&data);
    }
    lprint("\n");
}

int lookupModel(struct model *our_model, struct model *db_model) {
    if (readSettings("/data/local/tmp/rh-settings.txt", db_model)) {
        return EXACT_MODEL;
    }
    for (auto &known_model: models) {
        if (our_model->model    == known_model.model &&
            our_model->name     == known_model.name  &&
            our_model->board    == known_model.board &&
            our_model->platform == known_model.platform &&
            known_model.ion_heap != -1) {
            
            memcpy(db_model, &known_model, sizeof(known_model));
            return KNOWN_MODEL;
        }
    }
    for (auto &known_model: models) {
        if (our_model->platform == known_model.platform &&
            known_model.ion_heap != -1) {

            memcpy(db_model, &known_model, sizeof(known_model));
            return FAMILIAR_MODEL;
        }
    }

    memcpy(db_model, &unknown_model, sizeof(unknown_model));
    return UNKNOWN_MODEL;
}

void dump_settings(struct model *m) {
    lprint("\n");
    lprint("=============================================================\n");
    lprint("        HAMMER SETTINGS\n");
    lprint("=============================================================\n");
    lprint("[RS] ION heap:     %d\n", m->ion_heap);
    lprint("[RS] Rowsize:      %d\n", m->rowsize);
    lprint("[RS] ba2:          %x\n", m->ba2);
    lprint("[RS] ba1:          %x\n", m->ba1);
    lprint("[RS] ba0:          %x\n", m->ba0);
    lprint("[RS] rank:         %x\n", m->rank);
    if (m->treshold) {
        lprint("[RS] Settings for autodetection\n");
        lprint("[RS] - Treshold:     %d\n", m->treshold);
        lprint("[RS] - Measurements: %d\n", m->measurements);
        lprint("[RS] - Loop count:   %d\n", m->count);
        lprint("[RS] - Fence option: %d\n", m->fence);
        lprint("[RS] - CPU:          %d\n", m->cpu);
    } else {
        lprint("[RS] Did not run autodetection\n");
    }

    lprint("[RS] Database entry:\n");
    lprint("{\"%s\",\"%s\",\"%s\",\"%s\",\"%s\",%d,K(%d),%x,%x,%x,%x}\n",
            m->model.c_str(),       // generic name - nothing better than just the model...
            m->model.c_str(),       // ro.product.model
            m->name.c_str(),        // ro.product.name
            m->board.c_str(),       // ro.product.board
            m->platform.c_str(),    // ro.board.platform 
            m->ion_heap,
            m->rowsize/1024, 
            m->ba2, m->ba1, m->ba0, m->rank);
    lprint("\n");
}

void MergeModel(struct model *m, struct model *db) {
    m->measurements = db->measurements;
    m->count        = db->count;
    m->fence        = db->fence;
    m->cpu          = db->cpu;
    m->treshold     = db->treshold;
    m->rowsize      = db->rowsize;
    m->ion_heap     = db->ion_heap;
    m->ba2          = db->ba2;
    m->ba1          = db->ba1;
    m->ba0          = db->ba0;
    m->rank         = db->rank;
   
}

/*** CONSTRUCTOR ***/
BankConflicts::BankConflicts(void ) {
    this->measurements = MEASUREMENTS;
    this->count        = DEFAULT_LOOPCOUNT;
    this->fence        = DEFAULT_FENCE;
}



void BankConflicts::getModel(int force_autodetect, struct model *m) {
    lprint("\n");
    lprint("=============================================================\n");
    lprint("        SEARCHING FOR MODEL\n");
    lprint("=============================================================\n");
    this->our_model = m;
    lprint("[BC] Collecting basic hardware info\n");
    m->model    = getprop("ro.product.model");
    m->name     = getprop("ro.product.name");
    m->board    = getprop("ro.product.board");
    m->platform = getprop("ro.board.platform");

    m->cpus = -1;
    m->slowest_cpu = -1;
    m->fastest_cpu = -1;

    lprint("[BC] Collecting CPU info\n");
    m->cpus = getcpus(&m->slowest_cpu, &m->fastest_cpu);
    if (m->slowest_cpu == m->fastest_cpu) {
        this->cpus = {m->slowest_cpu};
    } else {
        this->cpus = {m->slowest_cpu, m->fastest_cpu};
    }
    
    struct model db_model;
    int familiarity = lookupModel(this->our_model, &db_model);
    
    if (familiarity == EXACT_MODEL) {
        lprint("[BC] Successfully completed rowsize detection during a previous run\n");

        if (!force_autodetect) {
            MergeModel(m, &db_model);
            return;
        }
    }

    if (familiarity == KNOWN_MODEL) {
        lprint("[BC] Successfully completed rowsize detection on the same model\n");

        if (!force_autodetect) {
            MergeModel(m, &db_model);
            return;
        }
    }


    /* We have to do some actual work... */
    

    lprint("[BC] ION init: generating a list of possible ION system-contig heaps\n");
    std::vector<int> heap_ids = ION_autodetect(m->platform);

    
    struct sigaction new_action, old_USR1;
    new_action.sa_handler = usr1_handler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction(SIGUSR1, &new_action, &old_USR1);
    sigusr1 = false;

    for (auto id: heap_ids) {
        lprint("\n");
        lprint("============================================\n");
        lprint("[BC] Autodetecting Rowsize with ION heap %2d\n", id);
        lprint("============================================\n");
        lprint("\n");
        struct ion_data data;
        if (ION_alloc_mmap(&data, RS_CHUNKSIZE, id) < 0) {
            lprint("[BC] Could not allocate %dKB with id %d\n", RS_CHUNKSIZE / 1024, id);
            continue;
        }
        bool success = this->findRowsize((uint8_t *) data.mapping, data.len);
        ION_clean(&data);

        if (sigusr1) {
            lprint("[BC] Interrupted, already OOM?\n");
            break;
        }

        if (success) {
            lprint("[BC] Successfully completed rowsize detection\n");
            this->our_model->ion_heap = id;

//          this->findMask();

            writeSettings("/data/local/tmp/rh-settings.txt", this->our_model);
            sigaction(SIGUSR1, &old_USR1, NULL);
            return;
        }
    }

    lprint("[BC] Autodetection failed - falling back\n");
    MergeModel(m, &db_model);
    m->ion_heap = heap_ids[0];
    m->ba2 = 0x0000;
    m->ba1 = 0x0000;
    m->ba0 = 0x0000;
    m->rank = 0x0000;
    sigaction(SIGUSR1, &old_USR1, NULL);
    return;
}












/***********************************************************
 * MAIN ENTRY POINT FOR AUTO DETECT 
 ***********************************************************/
int RS_autodetect(int force_autodetect, struct model *our_model) {
    /* Construct a database model that is as similar as this device's hardware
     * as possible. There are four types of database models:
     * - EXACT_MODEL:    We have auto-detection results *from this phone*
     *                   obtained during an earlier run
     * - KNOWN_MODEL:    We have auto-detection results *from a different phone*
     *                   that has the same name or model
     * - FAMILIAR_MODEL: We have auto-detection results *from a different phone*
     *                   that has the same platform or board
     * - UNKNOWN_MODEL:  We have no auto-detection results.
     */
    
    BankConflicts *bc = new BankConflicts();
    bc->getModel(force_autodetect, our_model);
    dump_hardware(our_model);
    dump_settings(our_model);

    return 0;
}
