VERBOSITY ?= 0

CXX ?= g++
CXXFLAGS = -Wall -fno-omit-frame-pointer -fvisibility=hidden -std=gnu++17 $(UNWIND_INCLUDES) -DVERBOSITY=$(VERBOSITY)

PYTHON ?= python3

all: libmemtrail.so sample benchmark

libmemtrail.so: memtrail.cpp memtrail.version

ifeq ($(shell test -d libunwind && echo true),true)

$(info info: using local libunwind)

libunwind/configure: libunwind/configure.ac
	autoreconf -i libunwind

libunwind/Makefile: libunwind/configure
	cd libunwind && ./configure --disable-cxx-exceptions --disable-debug-frame --disable-block-signals --disable-shared --enable-static --with-pic --prefix=$(abspath local)

local/lib/pkgconfig/libunwind.pc: libunwind/Makefile
	$(MAKE) -C libunwind install

libmemtrail.so: local/lib/pkgconfig/libunwind.pc

export PKG_CONFIG_PATH := $(abspath local)/lib/pkgconfig:$(PKG_CONFIG_PATH)

else

$(info info: using system's libunwind)

endif

libmemtrail.so: Makefile
	pkg-config --cflags --libs --static libunwind
	$(CXX) -O2 -g2 $(CXXFLAGS) -shared -fPIC -Wl,--version-script,memtrail.version -o $@ memtrail.cpp $$(pkg-config --cflags --libs --static libunwind) -ldl

%: %.cpp
	$(CXX) -O0 -g2 -Wno-unused-result -o $@ $< -ldl

gprof2dot.py:
	wget --quiet --timestamping https://raw.githubusercontent.com/jrfonseca/gprof2dot/main/gprof2dot.py
	chmod +x gprof2dot.py

sample: sample.cpp memtrail.h

test: libmemtrail.so sample gprof2dot.py
	$(RM) memtrail.data $(wildcard memtrail.*.json) $(wildcard memtrail.*.dot)
	$(PYTHON) memtrail record ./sample
	$(PYTHON) memtrail dump
	$(PYTHON) memtrail report --show-snapshots --show-snapshot-deltas --show-cumulative-snapshot-delta --show-maximum --show-leaks --output-graphs
	$(foreach LABEL, snapshot-0 snapshot-1 snapshot-1-delta maximum leaked, ./gprof2dot.py -f json memtrail.$(LABEL).json > memtrail.$(LABEL).dot ;)

test-debug: libmemtrail.so sample
	$(RM) memtrail.data $(wildcard memtrail.*.json) $(wildcard memtrail.*.dot)
	$(PYTHON) memtrail record --debug ./sample

bench: libmemtrail.so benchmark
	$(RM) memtrail.data
	$(PYTHON) memtrail record ./benchmark
	time -p $(PYTHON) memtrail report --show-maximum

profile: benchmark gprof2dot.py
	$(PYTHON) memtrail record ./benchmark
	$(PYTHON) -m cProfile -o memtrail.pstats -- memtrail report --show-maximum
	./gprof2dot.py -f pstats memtrail.pstats > memtrail.dot

clean:
	$(RM) libmemtrail.so gprof2dot.py sample benchmark


.PHONY: all test test-debug bench profile clean
