About
=====

THIS IS WORK IN PROGRESS AND IS NOT READ FOR GENERAL CONSUMPTION.

**memtrail** is a `LD_PRELOAD` based memory debugger/profiler for Linux.

There are already many other open-source memory debugging/profiling tools for
Linux, several of which are listed on Links section below, and the most
powerful arguably being Valgrind.  However, I needed a tool that could quantify
leaks and identify memory hogs, for long-running and CPU intensive workloads,
which simply run too slow under Valgrind's dynamic binary instrumentation.


Requirements
============

* Linux

* gzip

* [gprof2dot](http://code.google.com/p/jrfonseca/wiki/Gprof2Dot) for graph output


Build
=====

    make


Usage
=====

Run the application you want to debug as

    /path/to/memtrail /path/to/application [args...]

and it will generate a record  `memtrail.data` in the current
directory.

View results with

    /path/to/process.py memtrail.data


Links
=====

Memory debugging:

* [Valgrind's Memcheck](http://valgrind.org/docs/manual/mc-manual.html)

* [duma](http://duma.sourceforge.net/)

* [LeakTracer](http://www.andreasen.org/LeakTracer/)

* [glibc mtrace](http://www.gnu.org/s/hello/manual/libc/Allocation-Debugging.html)

* [Hans-Boehm garbagge collector](http://www.hpl.hp.com/personal/Hans_Boehm/gc/leak.html)

* [Leaky](http://mxr.mozilla.org/mozilla/source/tools/leaky/leaky.html)

* [failmalloc](http://www.nongnu.org/failmalloc/)

* [dmalloc](http://dmalloc.com/)

Memory profiling:

* [Valgrind's Massif](http://valgrind.org/docs/manual/ms-manual.html)

* [MemProf](http://www.secretlabs.de/projects/memprof/)

Catalogs:

* [Mozilla's](http://www.mozilla.org/performance/tools.html)

* [Owen Taylor's](http://people.redhat.com/otaylor/memprof/memtools.html)

