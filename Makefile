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

STANDALONE_TOOLCHAIN   ?= $(HOME)/src/android-ndk-r11c/sysroot-arm/bin
STANDALONE_TOOLCHAIN64 ?= $(HOME)/src/android-ndk-r11c/sysroot-arm64/bin


CC       = $(STANDALONE_TOOLCHAIN)/arm-linux-androideabi-gcc
CXX      = $(STANDALONE_TOOLCHAIN)/arm-linux-androideabi-g++
CPP      = $(STANDALONE_TOOLCHAIN)/arm-linux-androideabi-g++
STRIP    = $(STANDALONE_TOOLCHAIN)/arm-linux-androideabi-strip

CC_64    = $(STANDALONE_TOOLCHAIN64)/aarch64-linux-android-gcc
CXX_64   = $(STANDALONE_TOOLCHAIN64)/aarch64-linux-android-g++
CPP_64   = $(STANDALONE_TOOLCHAIN64)/aarch64-linux-android-g++
STRIP_64 = $(STANDALONE_TOOLCHAIN64)/aarch64-linux-android-strip

CPPFLAGS = -std=c++11 -O3 -Wall -march=armv7-a
LDFLAGS  = -pthread -static
INCLUDES = -I$(PWD)/../include

CPPFLAGS_64 = -std=c++11 -O3 -Wall -DARMV8

TMPDIR  = /data/local/tmp/
TARGET ?= rh-test

all: $(TARGET) rh-test64

rh-test: rh-test.o ion.o rowsize.o templating.o massage.o logger.o helper.h
	$(CPP) $(CPPFLAGS) -o $@ $^ $(LDFLAGS)
	$(STRIP) $@

rh-test64: rh-test.o64 ion.o64 rowsize.o64 templating.o64 massage.o64 logger.o64 helper.h
	$(CPP_64) $(CPPFLAGS_64) -o $@ $^ $(LDFLAGS)
	$(STRIP_64) $@

%.o: %.cc
	$(CPP) $(CPPFLAGS) $(INCLUDES) -c -o $@ $<

%.o64: %.cc
	$(CPP_64) $(CPPFLAGS_64) $(INCLUDES) -c -o $@ $<

install:
	make all
	adb push $(TARGET) $(TMPDIR)
	adb shell chmod 755 $(TMPDIR)$(TARGET)

clean:
	rm -f $(TARGET) rh-test64 *.o *.o64 a.out

upload:
	scp rh-test vvdveen.com:/home/vvdveen/www/drammer/rh-test
	

reboot:
	adb reboot

test:
	adb shell "$(TMPDIR)$(TARGET) -f/data/local/tmp/out.txt"
