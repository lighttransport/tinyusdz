all: print_fp test_exhaustive example_usage

print_fp:
	g++ -O2 -g -std=c++14 print_fp.cc -I ../../src/external/dragonbox/ ../../src/external/dragonbox/dragonbox_to_chars.cpp -I../../src/external -o print_fp
	#clang++ -O2 -g print_fp.cc -I ../../src/external/dragonbox/ ../../src/external/dragonbox/dragonbox_to_chars.cpp -I../../src/external
	#clang++ -O2 -g print_fp.cc -I ../../src/external/

test_exhaustive:
	g++ -O3 -g -std=c++14 test_exhaustive.cc -I ../../src/external/dragonbox/ ../../src/external/dragonbox/dragonbox_to_chars.cpp -I../../src/external -o test_exhaustive

example_usage:
	g++ -O2 -g -std=c++14 example_usage.cc -I ../../src/external/dragonbox/ ../../src/external/dragonbox/dragonbox_to_chars.cpp -I../../src/external -o example_usage

clean:
	rm -f print_fp test_exhaustive example_usage

# Run quick sanity test
test_sanity: test_exhaustive
	./test_exhaustive sanity

# Run quick float test with 1M random samples
test_float_quick: test_exhaustive
	./test_exhaustive float_quick

# Run full exhaustive float test (all 2^32 patterns - SLOW, uses all CPU cores)
test_float_exhaustive: test_exhaustive
	./test_exhaustive float_exhaustive

# Run exhaustive float test with specific thread count
test_float_exhaustive_threads: test_exhaustive
	@if [ -z "$(THREADS)" ]; then \
		echo "Usage: make test_float_exhaustive_threads THREADS=N"; \
		echo "Example: make test_float_exhaustive_threads THREADS=8"; \
		exit 1; \
	fi
	./test_exhaustive float_exhaustive $(THREADS)

# Run quick double test with 10M samples
test_double_quick: test_exhaustive
	./test_exhaustive double_quick

# Run medium double test with 100M samples
test_double_medium: test_exhaustive
	./test_exhaustive double_medium

# Run large double test with 1B samples (SLOW)
test_double_large: test_exhaustive
	./test_exhaustive double_large

.PHONY: all clean test_sanity test_float_quick test_float_exhaustive test_float_exhaustive_threads test_double_quick test_double_medium test_double_large
