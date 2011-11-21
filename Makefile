CFLAGS = -Wall

all: libleakcount.so sample

libleakcount.so: leakcount.cpp
	gcc -O2 -g2 $(CFLAGS) -shared -fPIC -o $@ $< -ldl

sample: sample.cpp
	g++ -O0 -g2 -o $@ $< -ldl

test: libleakcount.so sample
	LD_PRELOAD=./libleakcount.so ./sample

test-gdb:
	# http://stackoverflow.com/questions/4703763/how-to-run-gdb-with-ld-preload
	gdb --ex 'set exec-wrapper env LD_PRELOAD=./libleakcount.so' --args ./sample

clean:
	rm -f libleakcount.so sample


.PHONY: all test clean
