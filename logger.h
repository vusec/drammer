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

#include <stdio.h>
#include <time.h>

class Logger {
    public:
        Logger(const char *basename, int log_rotate);
        void log(const char *format, ...); 
        void fprint(const char *format, ...); // write to log file only

    private:
        void rotate(void);
        void openFile(time_t c_time);
        void closeFile(void);

        time_t l_time;
        const char *l_basename;
        char l_filename[128];
        FILE *l_fp;
        int l_rotate;
};

