/*
 * lightui/assert.h — Debug assertion macros
 *
 * Rich assertion macros that print file, line, function, expression,
 * and expected-vs-actual values on failure, then call assert() to abort.
 *
 * Usage:
 *   LUI_ASSERT(ptr != NULL);
 *   LUI_ASSERT_EQ(x, 42);          // int ==
 *   LUI_ASSERT_NE(x, 0);           // int !=
 *   LUI_ASSERT_LT(a, b);           // int <
 *   LUI_ASSERT_LE(a, b);           // int <=
 *   LUI_ASSERT_GT(a, b);           // int >
 *   LUI_ASSERT_GE(a, b);           // int >=
 *   LUI_ASSERT_FLOAT_EQ(f, 1.0f, 0.001f);  // float near-equal
 *   LUI_ASSERT_STR_EQ(s, "hello");          // string ==
 *   LUI_ASSERT_NULL(ptr);
 *   LUI_ASSERT_NOT_NULL(ptr);
 *   LUI_ASSERT_TRUE(expr);
 *   LUI_ASSERT_FALSE(expr);
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef LIGHTUI_ASSERT_H
#define LIGHTUI_ASSERT_H

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

/* ---- Core assertion with message ---------------------------------------- */

/**
 * LUI_ASSERT(expr)
 * Basic assertion with file/line/function context.
 */
#define LUI_ASSERT(expr)                                                      \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr,                                                    \
                "\n=== ASSERTION FAILED ===\n"                                 \
                "  File:       %s\n"                                           \
                "  Line:       %d\n"                                           \
                "  Function:   %s\n"                                           \
                "  Expression: %s\n"                                           \
                "========================\n",                                  \
                __FILE__, __LINE__, __func__, #expr);                          \
            assert(expr);                                                      \
        }                                                                      \
    } while (0)

/**
 * LUI_ASSERT_MSG(expr, msg)
 * Assertion with a custom message string.
 */
#define LUI_ASSERT_MSG(expr, msg)                                             \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr,                                                    \
                "\n=== ASSERTION FAILED ===\n"                                 \
                "  File:       %s\n"                                           \
                "  Line:       %d\n"                                           \
                "  Function:   %s\n"                                           \
                "  Expression: %s\n"                                           \
                "  Message:    %s\n"                                           \
                "========================\n",                                  \
                __FILE__, __LINE__, __func__, #expr, (msg));                   \
            assert(expr);                                                      \
        }                                                                      \
    } while (0)

/* ---- Integer comparisons ------------------------------------------------ */

#define LUI_ASSERT_EQ(actual, expected)                                       \
    do {                                                                       \
        long long lui__a = (long long)(actual);                                \
        long long lui__e = (long long)(expected);                              \
        if (lui__a != lui__e) {                                                \
            fprintf(stderr,                                                    \
                "\n=== ASSERTION FAILED ===\n"                                 \
                "  File:       %s\n"                                           \
                "  Line:       %d\n"                                           \
                "  Function:   %s\n"                                           \
                "  Check:      %s == %s\n"                                     \
                "  Expected:   %lld\n"                                         \
                "  Actual:     %lld\n"                                         \
                "========================\n",                                  \
                __FILE__, __LINE__, __func__,                                  \
                #actual, #expected, lui__e, lui__a);                            \
            assert(lui__a == lui__e);                                           \
        }                                                                      \
    } while (0)

#define LUI_ASSERT_NE(actual, not_expected)                                   \
    do {                                                                       \
        long long lui__a = (long long)(actual);                                \
        long long lui__n = (long long)(not_expected);                           \
        if (lui__a == lui__n) {                                                \
            fprintf(stderr,                                                    \
                "\n=== ASSERTION FAILED ===\n"                                 \
                "  File:       %s\n"                                           \
                "  Line:       %d\n"                                           \
                "  Function:   %s\n"                                           \
                "  Check:      %s != %s\n"                                     \
                "  Got:        %lld (should differ)\n"                          \
                "========================\n",                                  \
                __FILE__, __LINE__, __func__,                                  \
                #actual, #not_expected, lui__a);                                \
            assert(lui__a != lui__n);                                           \
        }                                                                      \
    } while (0)

#define LUI__ASSERT_CMP(a, op, b, op_str)                                    \
    do {                                                                       \
        long long lui__a = (long long)(a);                                     \
        long long lui__b = (long long)(b);                                     \
        if (!(lui__a op lui__b)) {                                             \
            fprintf(stderr,                                                    \
                "\n=== ASSERTION FAILED ===\n"                                 \
                "  File:       %s\n"                                           \
                "  Line:       %d\n"                                           \
                "  Function:   %s\n"                                           \
                "  Check:      %s " op_str " %s\n"                             \
                "  Left:       %lld\n"                                         \
                "  Right:      %lld\n"                                         \
                "========================\n",                                  \
                __FILE__, __LINE__, __func__,                                  \
                #a, #b, lui__a, lui__b);                                        \
            assert(lui__a op lui__b);                                           \
        }                                                                      \
    } while (0)

#define LUI_ASSERT_LT(a, b) LUI__ASSERT_CMP(a, <,  b, "<")
#define LUI_ASSERT_LE(a, b) LUI__ASSERT_CMP(a, <=, b, "<=")
#define LUI_ASSERT_GT(a, b) LUI__ASSERT_CMP(a, >,  b, ">")
#define LUI_ASSERT_GE(a, b) LUI__ASSERT_CMP(a, >=, b, ">=")

/* ---- Unsigned hex comparison (useful for colors) ------------------------ */

#define LUI_ASSERT_HEX_EQ(actual, expected)                                   \
    do {                                                                       \
        unsigned long long lui__a = (unsigned long long)(actual);               \
        unsigned long long lui__e = (unsigned long long)(expected);             \
        if (lui__a != lui__e) {                                                \
            fprintf(stderr,                                                    \
                "\n=== ASSERTION FAILED ===\n"                                 \
                "  File:       %s\n"                                           \
                "  Line:       %d\n"                                           \
                "  Function:   %s\n"                                           \
                "  Check:      %s == %s\n"                                     \
                "  Expected:   0x%llX\n"                                       \
                "  Actual:     0x%llX\n"                                       \
                "========================\n",                                  \
                __FILE__, __LINE__, __func__,                                  \
                #actual, #expected, lui__e, lui__a);                            \
            assert(lui__a == lui__e);                                           \
        }                                                                      \
    } while (0)

/* ---- Float comparison --------------------------------------------------- */

#define LUI_ASSERT_FLOAT_EQ(actual, expected, tolerance)                      \
    do {                                                                       \
        double lui__a = (double)(actual);                                       \
        double lui__e = (double)(expected);                                     \
        double lui__t = (double)(tolerance);                                    \
        if (fabs(lui__a - lui__e) > lui__t) {                                  \
            fprintf(stderr,                                                    \
                "\n=== ASSERTION FAILED ===\n"                                 \
                "  File:       %s\n"                                           \
                "  Line:       %d\n"                                           \
                "  Function:   %s\n"                                           \
                "  Check:      |%s - %s| <= %s\n"                              \
                "  Expected:   %g\n"                                           \
                "  Actual:     %g\n"                                           \
                "  Tolerance:  %g\n"                                           \
                "  Difference: %g\n"                                           \
                "========================\n",                                  \
                __FILE__, __LINE__, __func__,                                  \
                #actual, #expected, #tolerance,                                 \
                lui__e, lui__a, lui__t, fabs(lui__a - lui__e));                 \
            assert(fabs(lui__a - lui__e) <= lui__t);                            \
        }                                                                      \
    } while (0)

/* ---- String comparison -------------------------------------------------- */

#define LUI_ASSERT_STR_EQ(actual, expected)                                   \
    do {                                                                       \
        const char *lui__a = (actual);                                         \
        const char *lui__e = (expected);                                        \
        if (!lui__a || !lui__e || strcmp(lui__a, lui__e) != 0) {                \
            fprintf(stderr,                                                    \
                "\n=== ASSERTION FAILED ===\n"                                 \
                "  File:       %s\n"                                           \
                "  Line:       %d\n"                                           \
                "  Function:   %s\n"                                           \
                "  Check:      %s == %s\n"                                     \
                "  Expected:   \"%s\"\n"                                       \
                "  Actual:     \"%s\"\n"                                       \
                "========================\n",                                  \
                __FILE__, __LINE__, __func__,                                  \
                #actual, #expected,                                             \
                lui__e ? lui__e : "(null)",                                     \
                lui__a ? lui__a : "(null)");                                    \
            assert(lui__a && lui__e && strcmp(lui__a, lui__e) == 0);            \
        }                                                                      \
    } while (0)

/* ---- Pointer checks ----------------------------------------------------- */

#define LUI_ASSERT_NULL(ptr)                                                  \
    do {                                                                       \
        const void *lui__p = (const void *)(ptr);                              \
        if (lui__p != NULL) {                                                  \
            fprintf(stderr,                                                    \
                "\n=== ASSERTION FAILED ===\n"                                 \
                "  File:       %s\n"                                           \
                "  Line:       %d\n"                                           \
                "  Function:   %s\n"                                           \
                "  Check:      %s == NULL\n"                                   \
                "  Actual:     %p\n"                                           \
                "========================\n",                                  \
                __FILE__, __LINE__, __func__, #ptr, lui__p);                   \
            assert(lui__p == NULL);                                            \
        }                                                                      \
    } while (0)

#define LUI_ASSERT_NOT_NULL(ptr)                                              \
    do {                                                                       \
        if ((ptr) == NULL) {                                                   \
            fprintf(stderr,                                                    \
                "\n=== ASSERTION FAILED ===\n"                                 \
                "  File:       %s\n"                                           \
                "  Line:       %d\n"                                           \
                "  Function:   %s\n"                                           \
                "  Check:      %s != NULL\n"                                   \
                "  Actual:     NULL\n"                                          \
                "========================\n",                                  \
                __FILE__, __LINE__, __func__, #ptr);                           \
            assert((ptr) != NULL);                                             \
        }                                                                      \
    } while (0)

/* ---- Boolean checks ----------------------------------------------------- */

#define LUI_ASSERT_TRUE(expr)                                                 \
    do {                                                                       \
        if (!(expr)) {                                                         \
            fprintf(stderr,                                                    \
                "\n=== ASSERTION FAILED ===\n"                                 \
                "  File:       %s\n"                                           \
                "  Line:       %d\n"                                           \
                "  Function:   %s\n"                                           \
                "  Check:      %s is true\n"                                   \
                "  Actual:     false\n"                                         \
                "========================\n",                                  \
                __FILE__, __LINE__, __func__, #expr);                          \
            assert(expr);                                                      \
        }                                                                      \
    } while (0)

#define LUI_ASSERT_FALSE(expr)                                                \
    do {                                                                       \
        if ((expr)) {                                                          \
            fprintf(stderr,                                                    \
                "\n=== ASSERTION FAILED ===\n"                                 \
                "  File:       %s\n"                                           \
                "  Line:       %d\n"                                           \
                "  Function:   %s\n"                                           \
                "  Check:      %s is false\n"                                  \
                "  Actual:     true\n"                                          \
                "========================\n",                                  \
                __FILE__, __LINE__, __func__, #expr);                          \
            assert(!(expr));                                                   \
        }                                                                      \
    } while (0)

#endif /* LIGHTUI_ASSERT_H */
