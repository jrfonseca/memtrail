CXX = g++
CXXFLAGS = -Wall -fno-omit-frame-pointer

all: libmemtrail.so sample

libmemtrail.so: memtrail.cpp
	$(CXX) -O2 -g2 $(CXXFLAGS) -shared -fPIC -o $@ $< -ldl

sample: sample.cpp
	$(CXX) -O0 -g2 -o $@ $< -ldl

test: libmemtrail.so sample
	./memtrail record ./sample
	./memtrail report

test-gdb:
	# http://stackoverflow.com/questions/4703763/how-to-run-gdb-with-ld-preload
	gdb --ex 'set exec-wrapper env LD_PRELOAD=./libmemtrail.so' --args ./sample

clean:
	rm -f libmemtrail.so sample


.PHONY: all test clean
