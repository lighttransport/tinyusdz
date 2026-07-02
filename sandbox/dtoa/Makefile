# sandbox/dtoa — USD-optimized dtoa comparison (dragonbox baseline vs ryu vs zmij)
#
# Mirrors sandbox/print_fp/Makefile. One g++ invocation per target: .cc TUs
# compile as C++, the ryu wrapper .c TUs compile as C, all linked together.
#
# Flag variants:
#   DEVFLAGS  : -O2 -g                          (fast iteration / debugging)
#   RELFLAGS  : -O3 -DNDEBUG -fno-exceptions -fno-rtti
#               (matches how src/next builds the real dtoa — the honest numbers)
#   NATFLAGS  : RELFLAGS + -march=native        (candidate cores' full potential)

CXX := g++
STD := -std=c++17
INCS := -I . -I ../../src/external/zmij -I third_party/ryu \
        -I ../../src/external -I ../../src/external/dragonbox

DEVFLAGS := -O2 -g $(STD) $(INCS)
RELFLAGS := -O3 -DNDEBUG -fno-exceptions -fno-rtti $(STD) $(INCS)
NATFLAGS := $(RELFLAGS) -march=native

# Shared TUs: ryu wrappers (compiled as C by the g++ driver — .c extension),
# the repo's vendored zmij (src/external/zmij, same copy the lib uses), and the
# repo's production dtoa (the conformance oracle).
CORE := ryu_f2dec.c ryu_d2dec.c ../../src/external/zmij/zmij.cc \
        ../../src/next/writer/dtoa.cc

all: conformance bench bench_native

# Conformance is a correctness gate; build it optimized so the exhaustive sweep
# (all 2^32 floats + 1e9 doubles) finishes quickly.
conformance: conformance.cc $(CORE) render_usd.hh dtoa_candidates.hh
	$(CXX) $(RELFLAGS) conformance.cc $(CORE) -pthread -o conformance

# Release-faithful benchmark (portable, matches the shipping lib flags).
bench: bench.cc $(CORE) render_usd.hh dtoa_candidates.hh
	$(CXX) $(RELFLAGS) bench.cc $(CORE) -o bench

# Same benchmark with -march=native (upper bound for the vendored cores).
bench_native: bench.cc $(CORE) render_usd.hh dtoa_candidates.hh
	$(CXX) $(NATFLAGS) bench.cc $(CORE) -o bench_native

# -O2 dev build of the benchmark (optional reference point).
bench_dev: bench.cc $(CORE) render_usd.hh dtoa_candidates.hh
	$(CXX) $(DEVFLAGS) bench.cc $(CORE) -o bench_dev

# --- run helpers ------------------------------------------------------------
# Full exhaustive gate (all 2^32 floats + 1e9 doubles). Minutes, multicore.
check: conformance
	./conformance all

# Fast gate for iteration (curated + 10M random floats + 10M random doubles).
check_quick: conformance
	./conformance quick

run_bench: bench
	./bench

run_bench_native: bench_native
	./bench_native

clean:
	rm -f conformance bench bench_native bench_dev

.PHONY: all check check_quick run_bench run_bench_native clean
