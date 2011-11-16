all: libleakcount.so sample

libleakcount.so: leakcount.cpp
	gcc -shared -fPIC -o $@ $< -ldl

sample: sample.cpp
	g++ -o $@ $< -ldl

test: libleakcount.so sample
	LD_PRELOAD=./libleakcount.so ./sample

clean:
	rm -f libleakcount.so sample


.PHONY: all test clean
