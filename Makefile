CXX ?= g++
CXXFLAGS = -Wall -fno-omit-frame-pointer -fvisibility=hidden
NM ?= nm

all: libleakcount.so sample

libleakcount.so: leakcount.cpp
	$(CXX) -O2 -g2 $(CXXFLAGS) -shared -fPIC -o $@ $< -ldl

sample: sample.cpp
	$(CXX) -O0 -g2 -o $@ $< -ldl

pre-test: libleakcount.so leakcount.sym
	$(NM) --dynamic --defined-only libleakcount.so | sed -n 's/^[0-9a-fA-F]\+ T //p' | sort | diff -du leakcount.sym -

test: pre-test sample
	LD_PRELOAD=./libleakcount.so ./sample

test-debug: libleakcount.so sample
	# http://stackoverflow.com/questions/4703763/how-to-run-gdb-with-ld-preload
	gdb --ex 'set exec-wrapper env LD_PRELOAD=./libleakcount.so' --args ./sample

clean:
	rm -f libleakcount.so sample


.PHONY: all pre-test test test-debug clean
