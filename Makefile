CXX = g++
CXXFLAGS = -Wall -fno-omit-frame-pointer

all: libmemtrail.so sample

libmemtrail.so: memtrail.cpp
	$(CXX) -O2 -g2 $(CXXFLAGS) -shared -fPIC -o $@ $< -ldl

sample: sample.cpp
	$(CXX) -O0 -g2 -o $@ $< -ldl

test: libmemtrail.so sample
	./memtrail record ./sample
	./memtrail dump
	./memtrail report

test-debug: libmemtrail.so sample
	./memtrail record --debug ./sample

clean:
	rm -f libmemtrail.so sample


.PHONY: all test clean
