# Drammer
This software is the open-source component of our paper "Drammer: Deterministic
Rowhammer Attacks on Mobile Devices", published in ACM Computer and
Communications Security (CCS) 2016. It allows you to test whether an Android
device is vulnerable to the Rowhammer bug. It does **not** allow you to root
your device.

This code base contains our *native*, C/C++-based mobile Rowhammer
test implementation. 

# Disclaimer 
If, for some weird reason, you think running this code broke your device, you
get to keep both pieces.

# Android GUI app
If you don't want to build the test yourself, we also provide an
[Android app](https://vvdveen.com/drammer/drammer.apk) as a GUI for the native 
component that may or may not be currently available on the 
[Google Play Store](https://play.google.com/store/apps/details?id=org.iseclab.drammer)
depending on the store's policy.

The app supports *relaxed* and *aggressive* hammering, which corresponds to the
number of seconds to run 'defrag' (-d command line option described below): you
can choose a timeout between 0 (no defrag) and 60 seconds, although higher
timeouts likely cause the app to become unresponsive.

The app optionally collects basic statistics on the type of device and test
results so that we can gain insights into the number and type of vulnerable
devices in the wild, so please consider sharing them for science.

# Native installation
To build the native binary, you need an Android NDK toolchain. I used
android-ndk-r11c:

    wget https://dl.google.com/android/repository/android-ndk-r11c-linux-x86_64.zip
    unzip android-ndk-r11c-linux-x86_64.zip
    cd android-ndk-r11c
    ./build/tools/make-standalone-toolschain.sh --ndk-dir=`pwd` \
      --arch=arm --platform=android-24 \
      --install-dir=./sysroot-arm/ \
      --verbose

You can then build the program setting `STANDALONE_TOOLCHAIN` variable to point
to the toolchain:

    STANDALONE_TOOLCHAIN=path/to/android-ndk-r11c/sysroot-arm/bin make

This gives you a stripped ARMv7 binary that you can run on both ARMv7 (32-bit)
and ARMv8 (64-bit) devices. The Makefile provides an install feature that uses
the Android Debug Bridge (adb) to push the binary to your device's
/data/local/tmp/ directory. You can install adb by doing a `sudo apt-get install
android-tools-adb` (on Ubuntu) or by installing the Android SDK via
[android.com](https://developer.android.com/studio/index.html#downloads). Then
do a:

    make install
    make test

to install and start the Rowhammer test binary. Once installed, you may also
invoke it from the shell directly:

    adb shell
    cd /data/local/tmp
    ./rh-test

## Command line options
The native binary provides a number of command line options:

* *-a*   
  Do templating with all patterns. Without this option, only the patterns *010*
  and *101* are used, meaning that we hammer each row twice: once with it's
  aggressor rows containing all zeros while the victim row holds only ones, and
  once with the aggressor rows holding ones while the victim consists of zeros
  only. Enabling this option hammers each row with the following configurations:
  *000*, *001*, *010*, *011*, *100*, *101*, *110*, *111*, *00r*, *0r0*, *0rr*,
  *r00*, *r0r*, *rr0*, *rrr* (where *r* is random and changed every 100
  iterations). 

- *-c <number>*  
  Number of memory accesses per hammer round, defaults to 1000000. It is
  said that 2500000 yields the most flips.

- *-d <seconds>*  
  Number of seconds to run 'defrag' (disabled by default). This tricks the
  system into freeing more ION memory that can be used for templating. Since
  Android tries to keep as many background processes in memory as possible, the
  amount of memory available for ION allocations may be very small (all of the
  memory is either in use, or cached in the operating system). By allocating
  many ION chunks, this option forces Android's low memory killer to kill
  background processes, giving us more (contiguous) memory to hammer in the
  templating phase.  
  Use this option with caution: setting it too high likely hangs your device and
  trigger a reboot. My advice is to first try without *-d* (or with *-d0*), see
  how much memory you get, if not enough, hit `CTRL^C`, and restart with *-d3*.
  If this still does not give you enough memory, I usually repeat the sequence
  of breaking with `CTRL^C` and restarting with *-d3* again in favor of using a
  higher timeout value. To answer the question of "how much is enough": on a
  Nexus 5, that comes with 2GB of memory, you should be able to get 400 to 600
  MB of ION memory.

- *-f <file path>*  
  Write results not only to stdout but also to this file.

- *-h*  
  Dump the help screen.

- *-i*  
  Run an ION heap-type detector function.

- *-q <cpu>*  
  Pin the program to this CPU. Some big.LITTLE architectures require you to pin
  the program to a big core, to make sure memory accesses are as fast as
  possible.

- *-r <bytes>*  
  The rowsize in bytes. If this value is not provided, the program tries to find
  it using a timing side-channel (described in the paper) which may not always
  work. The most common value seems to be 65536 (64KB).

- *-s*
  Hammer more conservatively. By default, we hammer each page, but this option
  moves less bytes (currently set to 64 bytes).

- *-t <seconds>*   
  Stop hammering after this many seconds. The default behavior is to hammer all
  memory that we were able to allocate.

## Description of source files
The native code base is written in C and abuses some C++ functionality. There
are some comments in the source files that, combined with run-time output dumped
on stdout, should give you an indication of what is happening. The main output
of a run consists of numbers that indicate the average DRAM access time (in
nanoseconds).

What follows is a short description of all source files.

- *Makefile*  
  Build system.

- *helper.h*  
  Inline helper functions defined in a header file.

- *ion.cc* and *ion.h*  
  Implements all ION related functionality: allocate, share, and free. By using
  a custom *ION data* data structure defined in ion.h, we also provide some
  functions on top of these core ION ionctls: bulk (bulk allocations), mmap,
  clean, and clean_all. It is required to call ION_init() before performing any
  ION related operations, as this function takes care of opening the /dev/ion
  file and reads /proc/cpuinfo to determine which ION heap to use.  Note that
  the latter functionality is likely incomplete.

- *massage.cc* and *massage.h*  
  Implements exhaust (used for exhausting ION chunks: allocate until nothing is
  left) and defrag functions.

- *rh-test.cc*  
  Implements main() and is in charge of parsing the command line options and
  starting a template session.

- *rowsize.cc* and *rowsize.h*  
  Implements the auto detect function for finding the rowsize (described in more
  detail in the paper, Sections 5.1 and 8.1, and Figure 3)

- *templating.cc* and *templating.h*  
  Implements the actual Rowhammer test and builds template_t data structures
  (defined in templating.h, which might include some redundant fields). The
  is_exploitable() function checks whether a given template is in fact
  exploitable with Drammer. The main function is TMPL_run which loops over all
  hammerable ION chunks.
