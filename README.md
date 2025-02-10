About
=====

**memtrail** is a `LD_PRELOAD` based memory profiler and leak detector for Linux.

There are already many other open-source memory debugging/profiling tools for
Linux, several of which are listed on the [Links section below](#links), and
the most powerful arguably being Valgrind.  However, I needed a tool that could
quantify leaks and identify memory hogs, for long-running and CPU intensive
workloads, which simply run too slow under Valgrind's dynamic binary
instrumentation, hence this project.

![Build](https://github.com/jrfonseca/memtrail/workflows/build/badge.svg?branch=main)

Features
--------

* Very little runtime overhead

* Will immediately output maxmimum memory allocated and total leaked memory at
  the end of program execution.

* Text trees show callstacks for memory consuption or leaks

* Can produce graphs showing flow of memory consumption or leaks


Requirements
============

* Linux

* Python 3

* gzip

* binutils' addr2line

* [libunwind](http://www.nongnu.org/libunwind/)

  For best results (performance/stability) configure libunwind and build as

        ./configure --disable-cxx-exceptions --disable-debug-frame --disable-block-signals --disable-shared --enable-static --with-pic

  add set `UNWIND_SRC` environment variable to where the libunwind source is.

* [gprof2dot](https://github.com/jrfonseca/gprof2dot) for graph output


Build
=====

    make


Usage
=====

Run the application you want to debug as

    memtrail record /path/to/application [args...]

and it will generate a record  `memtrail.data` in the current
directory.

View results with

    memtrail report --show-maximum

It will produce something like


    maximum: 8,960B
      -> 45.71% (4,096B, 2x): calloc
      | -> 45.71% (4,096B, 2x): test_calloc() [sample.cpp:82]
      |   -> 45.71% (4,096B, 2x): main [sample.cpp:242]
      |     -> 45.71% (4,096B, 2x): __libc_start_call_main [sysdeps/nptl/libc_start_call_main.h:58]
      |       -> 45.71% (4,096B, 2x): call_init [csu/libc-start.c:128]
      |         -> 45.71% (4,096B, 2x): _start
      | 
      -> 31.43% (2,816B, 4x): malloc
      | -> 11.43% (1,024B, 1x): test_malloc() [sample.cpp:57]
      | | -> 11.43% (1,024B, 1x): main [sample.cpp:241]
      | |   -> 11.43% (1,024B, 1x): __libc_start_call_main [sysdeps/nptl/libc_start_call_main.h:58]
      | |     -> 11.43% (1,024B, 1x): call_init [csu/libc-start.c:128]
      | |       -> 11.43% (1,024B, 1x): _start
      | | 
      | -> 11.43% (1,024B, 1x): test_realloc() [sample.cpp:103]
      | | -> 11.43% (1,024B, 1x): main [sample.cpp:243]
      | |   -> 11.43% (1,024B, 1x): __libc_start_call_main [sysdeps/nptl/libc_start_call_main.h:58]
      | |     -> 11.43% (1,024B, 1x): call_init [csu/libc-start.c:128]
      | |       -> 11.43% (1,024B, 1x): _start
      | | 
      | -> 8.57% (768B, 2x): TestGlobal::TestGlobal() [sample.cpp:212]
      |   -> 8.57% (768B, 2x): __static_initialization_and_destruction_0(int, int) [sample.cpp:225]
      |     -> 8.57% (768B, 2x): _GLOBAL__sub_I_leaked [sample.cpp:252]
      |       -> 8.57% (768B, 2x): call_init [csu/libc-start.c:144]
      |         -> 8.57% (768B, 2x): _start
      | 
      -> 22.86% (2,048B, 1x): realloc
        -> 22.86% (2,048B, 1x): test_realloc() [sample.cpp:106]
          -> 22.86% (2,048B, 1x): main [sample.cpp:243]
            -> 22.86% (2,048B, 1x): __libc_start_call_main [sysdeps/nptl/libc_start_call_main.h:58]
              -> 22.86% (2,048B, 1x): call_init [csu/libc-start.c:128]
                -> 22.86% (2,048B, 1x): _start
    memtrail.maximum.json written

You can then use `gprof2dot.py` to obtain graphs highlighting memory leaks or
consumption:

    gprof2dot.py -f json memtrail.maximum.json | dot -Tpng -o memtrail.maximum.png

![Sample](sample.png)


It is also possible to trigger memtrail to take snapshots at specific points by
calling `memtrail_snapshot` from your code:

    #include "memtrail.h"
    
    ...
    
       memtrail_snapshot();


Links
=====

Memory debugging:

* [Valgrind's Memcheck](http://valgrind.org/docs/manual/mc-manual.html)

* [bcc memleak](https://github.com/iovisor/bcc)

* [Google Sanitizers](https://github.com/google/sanitizers)

* [duma](http://duma.sourceforge.net/)

* [LeakTracer](http://www.andreasen.org/LeakTracer/)

* [glibc mtrace](http://www.gnu.org/s/hello/manual/libc/Allocation-Debugging.html)

* [Hans-Boehm garbage collector](http://www.hpl.hp.com/personal/Hans_Boehm/gc/leak.html)

* [Leaky](http://mxr.mozilla.org/mozilla/source/tools/leaky/leaky.html)

* [failmalloc](http://www.nongnu.org/failmalloc/)

* [dmalloc](http://dmalloc.com/)

Memory profiling:

* [Valgrind's Massif](http://valgrind.org/docs/manual/ms-manual.html)

* [Memory Frame Graphs](https://www.brendangregg.com/FlameGraphs/memoryflamegraphs.html)

* [Heaptrack](https://github.com/KDE/heaptrack)

* [Google Performance Tools' HEAPPROFILE](https://github.com/gperftools/gperftools)

* [MemProf](http://www.secretlabs.de/projects/memprof/)
