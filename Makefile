CXX = g++
CXXFLAGS = -Wall -fno-omit-frame-pointer -fvisibility=hidden

all: libleakcount.so sample

libleakcount.so: leakcount.cpp
	$(CXX) -O2 -g2 $(CXXFLAGS) -shared -fPIC -o $@ $< -ldl

sample: sample.cpp
	$(CXX) -O0 -g2 -o $@ $< -ldl

test: libleakcount.so sample
	LD_PRELOAD=./libleakcount.so ./sample

test-gdb:
	# http://stackoverflow.com/questions/4703763/how-to-run-gdb-with-ld-preload
	gdb --ex 'set exec-wrapper env LD_PRELOAD=./libleakcount.so' --args ./sample

clean:
	rm -f libleakcount.so sample


.PHONY: all test clean
