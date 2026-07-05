// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - test check macro.
//
// NEXT_CHECK(expr) replaces bare assert() in the tests/next suite. Unlike
// assert(), it ALWAYS evaluates `expr` and reports+aborts on failure, so the
// tests validate in every build type -- assert() is a no-op under NDEBUG
// (Release/RelWithDebInfo), which silently turned these tests into
// check-nothing runs AND dropped any side effect placed inside an assert().
#pragma once

#include <cstdio>
#include <cstdlib>

#define NEXT_CHECK(expr)                                                       \
  do {                                                                         \
    if (!(expr)) {                                                             \
      std::fprintf(stderr, "TEST CHECK FAILED: %s\n  at %s:%d\n", #expr,       \
                   __FILE__, __LINE__);                                        \
      std::fflush(stderr);                                                     \
      std::abort();                                                            \
    }                                                                          \
  } while (0)
