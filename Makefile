CFLAGS = -Wall

all: libmemtrail.so sample

libmemtrail.so: memtrail.cpp
	gcc -O2 -g2 $(CFLAGS) -shared -fPIC -o $@ $< -ldl

sample: sample.cpp
	g++ -O0 -g2 -o $@ $< -ldl

test: libmemtrail.so sample
	LD_PRELOAD=./libmemtrail.so ./sample

test-gdb:
	# http://stackoverflow.com/questions/4703763/how-to-run-gdb-with-ld-preload
	gdb --ex 'set exec-wrapper env LD_PRELOAD=./libmemtrail.so' --args ./sample

clean:
	rm -f libmemtrail.so sample


.PHONY: all test clean
