all: libleakcount.so sample

libleakcount.so: leakcount.cpp
	gcc -shared -fPIC -ldl -o $@ $<

sample: sample.cpp
	g++ -o $@ $<

test: libleakcount.so sample
	LD_PRELOAD=./libleakcount.so ./sample

clean:
	rm -f libleakcount.so sample


.PHONY: all test clean
