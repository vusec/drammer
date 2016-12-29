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

#include <time.h> 
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "logger.h"

Logger::Logger(const char *basename, int log_rotate) {
    l_basename = basename;
    l_rotate = log_rotate;
    l_fp = NULL;

    setvbuf(stderr, NULL, _IONBF, 0);
    setvbuf(stdout, NULL, _IONBF, 0);

    if (l_basename != NULL) 
        openFile(time(NULL));
}

void Logger::openFile(time_t c_time) {
    sprintf(l_filename,"%s.%lu", l_basename, c_time);
    l_fp = fopen(l_filename, "w");
    if (l_fp == NULL) {
        fprintf(stderr,"Could not open %s: %s\n", l_filename, strerror(errno));
        exit(0);
    }
    l_time = c_time;
    setvbuf(l_fp, NULL, _IONBF, 0);
}

void Logger::closeFile(void) {
    fclose(l_fp);
}

void Logger::rotate(void) {
    if (l_rotate == 0)
        return;

    time_t cur_time = time(NULL);
    if (cur_time - l_time > l_rotate) {
        closeFile();
         openFile(cur_time);
    }
}

void Logger::fprint(const char *format, ...) {
    va_list args;
    va_start(args, format);
    if (l_fp) {
        rotate();
        vfprintf(l_fp, format, args);
    }
    va_end(args);
}

void Logger::log(const char *format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stdout, format, args);
    if (l_fp) {
        rotate();
        vfprintf(l_fp, format, args);
    }
    va_end(args);
}
