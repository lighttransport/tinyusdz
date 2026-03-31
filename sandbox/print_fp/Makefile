CXX := g++
CXXFLAGS := -O2 -g -std=c++17 -I ../../src/external/dragonbox/ -I../../src/external
DRAGONBOX_SRC := ../../src/external/dragonbox/dragonbox_to_chars.cpp

all: print_fp test_exhaustive example_usage test_decimal test_shortest_and_ranges test_fastpath_perf test_dtos_integration test_print_vectors

print_fp:
	$(CXX) $(CXXFLAGS) print_fp.cc $(DRAGONBOX_SRC) -o print_fp
	#clang++ -O2 -g print_fp.cc -I ../../src/external/dragonbox/ ../../src/external/dragonbox/dragonbox_to_chars.cpp -I../../src/external
	#clang++ -O2 -g print_fp.cc -I ../../src/external/

test_exhaustive:
	$(CXX) -O3 -g -std=c++17 test_exhaustive.cc $(DRAGONBOX_SRC) -I../../src/external -o test_exhaustive

example_usage:
	$(CXX) $(CXXFLAGS) example_usage.cc $(DRAGONBOX_SRC) -o example_usage

test_decimal:
	$(CXX) $(CXXFLAGS) test_decimal.cc $(DRAGONBOX_SRC) -o test_decimal

test_shortest_and_ranges:
	$(CXX) $(CXXFLAGS) test_shortest_and_ranges.cc $(DRAGONBOX_SRC) -o test_shortest_and_ranges

test_fastpath_perf:
	$(CXX) $(CXXFLAGS) test_fastpath_perf.cc $(DRAGONBOX_SRC) -o test_fastpath_perf

test_dtos_integration:
	$(CXX) $(CXXFLAGS) ../../test_dtos_integration.cc ../../src/str-util.cc $(DRAGONBOX_SRC) -I../../src -o test_dtos_integration

test_print_vectors:
	$(CXX) $(CXXFLAGS) ../../test_print_vectors.cc ../../src/str-util.cc $(DRAGONBOX_SRC) -I../../src -o test_print_vectors

clean:
	rm -f print_fp test_exhaustive example_usage test_decimal test_shortest_and_ranges test_fastpath_perf test_dtos_integration test_print_vectors

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

# Run decimal number printing tests
test_run_decimal: test_decimal
	./test_decimal

# Run shortest representation and range tests
test_run_shortest: test_shortest_and_ranges
	./test_shortest_and_ranges

# Run fast path performance test
test_run_fastpath: test_fastpath_perf
	./test_fastpath_perf

# Run dtos integration test
test_run_dtos: test_dtos_integration
	./test_dtos_integration

# Run vector/matrix printing test
test_run_vectors: test_print_vectors
	./test_print_vectors

.PHONY: all clean test_sanity test_float_quick test_float_exhaustive test_float_exhaustive_threads test_double_quick test_double_medium test_double_large test_run_decimal test_run_shortest test_run_fastpath test_run_dtos test_run_vectors
