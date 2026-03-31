all: parse_fp test_parse_fp16 example_fp16

parse_fp:
	g++ -O2 -g -I../../src/external/fast_float/include parse_fp.cc -o parse_fp -pthread

test_parse_fp16:
	g++ -O2 -g -Wall -Wextra test_parse_fp16.cc -o test_parse_fp16 -lm

example_fp16:
	g++ -O2 -g -Wall -Wextra example_fp16.cc -o example_fp16 -lm

test: test_parse_fp16
	./test_parse_fp16

example: example_fp16
	./example_fp16

clean:
	rm -f parse_fp test_parse_fp16 example_fp16

.PHONY: all test example clean
