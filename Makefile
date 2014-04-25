CXX ?= g++
CXXFLAGS = -std=gnu++11 -Wall -fno-omit-frame-pointer -fvisibility=hidden
NM ?= nm

all: .libmemtrail.so.check sample benchmark

libmemtrail.so: memtrail.cpp memtrail.sym
	$(CXX) -O2 -g2 $(CXXFLAGS) -shared -fPIC -o $@ $< -lunwind -ldl

.libmemtrail.so.check: libmemtrail.so memtrail.sym
	$(NM) --dynamic --defined-only libmemtrail.so | sed -n 's/^[0-9a-fA-F]\+ T //p' | diff -du memtrail.sym -
	touch $@

%: %.cpp
	$(CXX) -O0 -g2 -o $@ $< -ldl

gprof2dot.py:
	wget --quiet --timestamping http://gprof2dot.jrfonseca.googlecode.com/git/gprof2dot.py
	chmod +x gprof2dot.py

test: .libmemtrail.so.check sample gprof2dot.py
	$(RM) memtrail.data
	./memtrail record ./sample
	./memtrail dump
	./memtrail report
	./gprof2dot.py -f json memtrail.snapshot-0.json > memtrail.snapshot-0.dot
	./gprof2dot.py -f json memtrail.maximum.json > memtrail.maximum.dot
	./gprof2dot.py -f json memtrail.leaked.json > memtrail.leaked.dot

test-debug: .libmemtrail.so.check sample
	$(RM) memtrail.data
	./memtrail record --debug ./sample

bench: .libmemtrail.so.check benchmark
	$(RM) memtrail.data
	./memtrail record ./benchmark
	time -p ./memtrail report

profile: benchmark gprof2dot.py
	./memtrail record ./benchmark
	python -m cProfile -o memtrail.pstats -- ./memtrail report
	./gprof2dot.py -f pstats memtrail.pstats > memtrail.dot

clean:
	$(RM) libmemtrail.so gprof2dot.py sample


.PHONY: all test test-debug bench profile clean
