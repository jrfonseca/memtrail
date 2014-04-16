About
=====

**memtrail** is a `LD_PRELOAD` based memory debugger/profiler for Linux.

There are already many other open-source memory debugging/profiling tools for
Linux, several of which are listed on Links section below, and the most
powerful arguably being Valgrind.  However, I needed a tool that could quantify
leaks and identify memory hogs, for long-running and CPU intensive workloads,
which simply run too slow under Valgrind's dynamic binary instrumentation,
hence this project.


Requirements
============

* Linux

* Python 2.7

* gzip

* [gprof2dot](http://code.google.com/p/jrfonseca/wiki/Gprof2Dot) for graph output


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

    memtrail report


It will produce something like


    leaked: 5217 bytes
    
    ->39.26% (2048B): test_calloc
    | ->39.26% (2048B): main
    |   ->39.26% (2048B): __libc_start_main
    |     ->39.26% (2048B): _start
    | 
    ->19.63% (1024B): test_malloc
    | ->19.63% (1024B): main
    |   ->19.63% (1024B): __libc_start_main
    |     ->19.63% (1024B): _start
    | 
    ->19.63% (1024B): test_memalign
    | ->19.63% (1024B): main
    |   ->19.63% (1024B): __libc_start_main
    |     ->19.63% (1024B): _start
    | 
    -> 9.81% (512B): test_cxx
    | -> 9.81% (512B): main
    |   -> 9.81% (512B): __libc_start_main
    |     -> 9.81% (512B): _start
    | 
    -> 9.81% (512B): TestGlobal::TestGlobal()
    | -> 9.81% (512B): __static_initialization_and_destruction_0
    |   -> 9.81% (512B): _GLOBAL__sub_I_leaked
    |     -> 9.81% (512B): __libc_csu_init
    |       -> 9.81% (512B): __libc_start_main
    |         -> 9.81% (512B): _start
    | 
    -> 1.23% (64B): TestGlobal::~TestGlobal()
    | -> 1.23% (64B): __run_exit_handlers
    |   -> 1.23% (64B): exit
    |     -> 1.23% (64B): __libc_start_main
    |       -> 1.23% (64B): _start
    | 
    -> 0.61% (32B) in 2 places, all below the 1.00% threshold
    
    memtrail.leaked.json written

You can then use `gprof2dot.py` to obtain graphs highlighting memory leaks or
consumption:

    gprof2dot.py -f json memtrail.leaked.json | dot -Tpng -o memtrail.leaked.png


Known Issues
============

* Computing the maximum memory allocation is awfully inefficient.  Use the
  `--no-maximum` option if you are only interested in memory leaks.


Links
=====

Memory debugging:

* [Valgrind's Memcheck](http://valgrind.org/docs/manual/mc-manual.html)

* [duma](http://duma.sourceforge.net/)

* [LeakTracer](http://www.andreasen.org/LeakTracer/)

* [glibc mtrace](http://www.gnu.org/s/hello/manual/libc/Allocation-Debugging.html)

* [Hans-Boehm garbage collector](http://www.hpl.hp.com/personal/Hans_Boehm/gc/leak.html)

* [Leaky](http://mxr.mozilla.org/mozilla/source/tools/leaky/leaky.html)

* [failmalloc](http://www.nongnu.org/failmalloc/)

* [dmalloc](http://dmalloc.com/)

Memory profiling:

* [Valgrind's Massif](http://valgrind.org/docs/manual/ms-manual.html)

* [Google Performance Tools](http://google-perftools.googlecode.com/svn/trunk/doc/heapprofile.html)

* [MemProf](http://www.secretlabs.de/projects/memprof/)

Catalogs:

* [Mozilla's](http://www.mozilla.org/performance/tools.html)

* [Owen Taylor's](http://people.redhat.com/otaylor/memprof/memtools.html)

