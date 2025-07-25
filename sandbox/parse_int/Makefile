all:
	clang++ -O2 -g -stdlib=libc++ parse_int.cc -o parse_int ../../src/tiny-string.cc

clean:
	rm -f parse_int a.out

test: all
	./parse_int 1000000 1 1
	./parse_int 1000000 1 4
	./parse_int 1000000 1 8

.PHONY: all clean test
