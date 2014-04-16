CXX ?= g++
CXXFLAGS = -Wall -fno-omit-frame-pointer -fvisibility=hidden
NM ?= nm

all: libmemtrail.so sample

libmemtrail.so: memtrail.cpp
	$(CXX) -O2 -g2 $(CXXFLAGS) -shared -fPIC -o $@ $< -ldl

gprof2dot.py:
	wget --quiet --timestamping http://gprof2dot.jrfonseca.googlecode.com/git/gprof2dot.py
	chmod +x gprof2dot.py

%: %.cpp
	$(CXX) -O0 -g2 -o $@ $< -ldl

pre-test: libmemtrail.so memtrail.sym
	$(NM) --dynamic --defined-only libmemtrail.so | sed -n 's/^[0-9a-fA-F]\+ T //p' | sort | diff -du memtrail.sym -

test: pre-test sample gprof2dot.py
	./memtrail record ./sample
	./memtrail dump
	./memtrail report
	./gprof2dot.py -f json memtrail.maximum.json > memtrail.maximum.dot
	./gprof2dot.py -f json memtrail.leaked.json > memtrail.leaked.dot

test-debug: libmemtrail.so sample
	./memtrail record --debug ./sample

bench: libmemtrail.so benchmark
	./memtrail record ./benchmark
	time -p ./memtrail report --no-maximum

profile: benchmark gprof2dot.py
	./memtrail record ./benchmark
	python -m cProfile -o memtrail.pstats -- ./memtrail report
	./gprof2dot.py -f pstats memtrail.pstats > memtrail.dot

clean:
	rm -f libmemtrail.so gprof2dot.py sample


.PHONY: all pre-test test test-debug bench profile clean
