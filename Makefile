CXX ?= g++
CXXFLAGS = -Wall -fno-omit-frame-pointer -fvisibility=hidden
NM ?= nm

all: .libmemtrail.so.check sample benchmark

libmemtrail.so: memtrail.cpp memtrail.version
	$(CXX) -O2 -g2 $(CXXFLAGS) -shared -fPIC -Wl,--version-script,memtrail.version -o $@ $< -lunwind -ldl

.libmemtrail.so.check: libmemtrail.so memtrail.sym
	$(NM) --dynamic --defined-only libmemtrail.so | sed -n 's/^[0-9a-fA-F]\+ T //p' | diff -du memtrail.sym -
	touch $@

%: %.cpp
	$(CXX) -O0 -g2 -o $@ $< -ldl

gprof2dot.py:
	wget --quiet --timestamping http://gprof2dot.jrfonseca.googlecode.com/git/gprof2dot.py
	chmod +x gprof2dot.py

sample: sample.cpp memtrail.h

test: .libmemtrail.so.check sample gprof2dot.py
	$(RM) memtrail.data $(wildcard memtrail.*.json) $(wildcard memtrail.*.dot)
	./memtrail record ./sample
	./memtrail dump
	./memtrail report --show-snapshots --show-snapshot-deltas --show-cumulative-snapshot-delta --show-maximum --show-leaks --output-graphs
	$(foreach LABEL, snapshot-0 snapshot-1 snapshot-1-delta maximum leaked, ./gprof2dot.py -f json memtrail.$(LABEL).json > memtrail.$$LABEL.dot ;)

test-debug: .libmemtrail.so.check sample
	$(RM) memtrail.data $(wildcard memtrail.*.json) $(wildcard memtrail.*.dot)
	./memtrail record --debug ./sample

bench: .libmemtrail.so.check benchmark
	$(RM) memtrail.data
	./memtrail record ./benchmark
	time -p ./memtrail report --show-maximum

profile: benchmark gprof2dot.py
	./memtrail record ./benchmark
	python -m cProfile -o memtrail.pstats -- ./memtrail report --show-maximum
	./gprof2dot.py -f pstats memtrail.pstats > memtrail.dot

clean:
	$(RM) libmemtrail.so gprof2dot.py sample


.PHONY: all test test-debug bench profile clean
