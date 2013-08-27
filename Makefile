CXX ?= g++
CXXFLAGS = -Wall -fno-omit-frame-pointer -fvisibility=hidden
NM ?= nm

all: libmemtrail.so gprof2dot.py sample

libmemtrail.so: memtrail.cpp
	$(CXX) -O2 -g2 $(CXXFLAGS) -shared -fPIC -o $@ $< -ldl

gprof2dot.py:
	wget --quiet --timestamping http://gprof2dot.jrfonseca.googlecode.com/git/gprof2dot.py
	chmod +x gprof2dot.py

sample: sample.cpp
	$(CXX) -O0 -g2 -o $@ $< -ldl

pre-test: libmemtrail.so memtrail.sym
	$(NM) --dynamic --defined-only libmemtrail.so | sed -n 's/^[0-9a-fA-F]\+ T //p' | diff -du memtrail.sym -

test: pre-test sample
	./memtrail record ./sample
	./memtrail dump
	./memtrail report

test-debug: libmemtrail.so sample
	./memtrail record --debug ./sample

clean:
	rm -f libmemtrail.so gprof2dot.py sample


.PHONY: all pre-test test test-debug clean
