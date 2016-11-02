##
 # Copyright 2016, Victor van der Veen
 #
 # Licensed under the Apache License, Version 2.0 (the "License");
 # you may not use this file except in compliance with the License.
 # You may obtain a copy of the License at
 #
 #     http://www.apache.org/licenses/LICENSE-2.0
 #
 # Unless required by applicable law or agreed to in writing, software
 # distributed under the License is distributed on an "AS IS" BASIS,
 # WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 # See the License for the specific language governing permissions and
 # limitations under the License.
 ## 

STANDALONE_TOOLCHAIN ?= $(HOME)/src/android-ndk-r11c/sysroot-arm/bin

CC    = $(STANDALONE_TOOLCHAIN)/arm-linux-androideabi-gcc
CXX   = $(STANDALONE_TOOLCHAIN)/arm-linux-androideabi-g++
CPP   = $(STANDALONE_TOOLCHAIN)/arm-linux-androideabi-g++
STRIP = $(STANDALONE_TOOLCHAIN)/arm-linux-androideabi-strip

CPPFLAGS = -std=c++11 -O3 -Wall
LDFLAGS  = -pthread -static
INCLUDES = -I$(PWD)/../include

TMPDIR  = /data/local/tmp/
TARGET ?= rh-test

all: $(TARGET)

rh-test: rh-test.o ion.o rowsize.o templating.o massage.o
	$(CPP) $(CPPFLAGS) -o $@ $^ $(LDFLAGS)
	$(STRIP) $@

%.o: %.cc
	$(CPP) $(CPPFLAGS) $(INCLUDES) -c -o $@ $<

install:
	make all
	adb push $(TARGET) $(TMPDIR)
	adb shell chmod 755 $(TMPDIR)$(TARGET)

clean:
	rm -f $(TARGET) *.o a.out

upload:
	scp rh-test vvdveen.com:/home/vvdveen/www/drammer/rh-test
	

reboot:
	adb reboot

test:
	adb shell "$(TMPDIR)$(TARGET) -f/data/local/tmp/out.txt"
