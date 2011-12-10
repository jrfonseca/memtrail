CXX = g++
CXXFLAGS = -Wall -fno-omit-frame-pointer -fvisibility=hidden

all: libmemtrail.so sample

libmemtrail.so: memtrail.cpp
	$(CXX) -O2 -g2 $(CXXFLAGS) -shared -fPIC -o $@ $< -ldl

sample: sample.cpp
	$(CXX) -O0 -g2 -o $@ $< -ldl

pre-test: libmemtrail.so
	nm -D libmemtrail.so | sed -n 's/^[0-9a-fA-F]\+ T //p' | diff -du - symbols.txt

test: pre-test sample
	./memtrail record ./sample
	./memtrail dump
	./memtrail report

test-debug: libmemtrail.so sample
	./memtrail record --debug ./sample

clean:
	rm -f libmemtrail.so sample


.PHONY: all test clean
