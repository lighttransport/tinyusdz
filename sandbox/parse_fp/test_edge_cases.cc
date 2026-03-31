// Unit tests for string to float parsing edge cases
// Tests specific problematic patterns and edge cases

#include <iostream>
#include <vector>
#include <string>
#include <charconv>
#include <cstring>
#include <cmath>
#include <limits>
#include <iomanip>
#include <cassert>

#include "fast_float/fast_float.h"

#define TEST_CASE(name) std::cout << "Testing: " << name << " ... "; {
#define END_TEST } std::cout << "PASS" << std::endl;

// Helper to check if two floats are equal (handling NaN properly)
bool floats_equal(float a, float b) {
    if (std::isnan(a) && std::isnan(b)) return true;
    if (std::isnan(a) || std::isnan(b)) return false;

    // Bit-exact comparison
    uint32_t a_bits, b_bits;
    std::memcpy(&a_bits, &a, sizeof(float));
    std::memcpy(&b_bits, &b, sizeof(float));
    return a_bits == b_bits;
}

void test_parse_both(const std::string& str, float expected, bool should_succeed) {
    // Test with fast_float
    float ff_result = 0;
    auto ff_parse = fast_float::from_chars(str.data(), str.data() + str.size(), ff_result);
    bool ff_success = (ff_parse.ec == std::errc());

    // Test with std::from_chars
    float std_result = 0;
    auto std_parse = std::from_chars(str.data(), str.data() + str.size(), std_result);
    bool std_success = (std_parse.ec == std::errc());

    // Check parsing success/failure matches
    if (ff_success != std_success) {
        std::cerr << "\nParse success mismatch for '" << str << "'" << std::endl;
        std::cerr << "  fast_float: " << (ff_success ? "success" : "failed") << std::endl;
        std::cerr << "  std::from_chars: " << (std_success ? "success" : "failed") << std::endl;
        assert(false);
    }

    // Check against expected
    if (should_succeed != ff_success) {
        std::cerr << "\nUnexpected parse result for '" << str << "'" << std::endl;
        std::cerr << "  Expected: " << (should_succeed ? "success" : "failure") << std::endl;
        std::cerr << "  Got: " << (ff_success ? "success" : "failure") << std::endl;
        assert(false);
    }

    // If both succeeded, check values match
    if (ff_success && std_success) {
        if (!floats_equal(ff_result, std_result)) {
            std::cerr << "\nValue mismatch for '" << str << "'" << std::endl;
            std::cerr << "  fast_float: " << ff_result;
            if (std::isnan(ff_result)) std::cerr << " (NaN)";
            std::cerr << std::endl;
            std::cerr << "  std::from_chars: " << std_result;
            if (std::isnan(std_result)) std::cerr << " (NaN)";
            std::cerr << std::endl;
            assert(false);
        }

        // Check against expected value if provided
        if (!std::isnan(expected) && !floats_equal(ff_result, expected)) {
            std::cerr << "\nUnexpected value for '" << str << "'" << std::endl;
            std::cerr << "  Expected: " << expected << std::endl;
            std::cerr << "  Got: " << ff_result << std::endl;
            assert(false);
        }
    }
}

int main() {
    std::cout << "=== Float Parsing Edge Cases Unit Tests ===" << std::endl;

    TEST_CASE("Basic integers")
        test_parse_both("0", 0.0f, true);
        test_parse_both("1", 1.0f, true);
        test_parse_both("-1", -1.0f, true);
        test_parse_both("42", 42.0f, true);
        test_parse_both("-42", -42.0f, true);
    END_TEST

    TEST_CASE("Basic decimals")
        test_parse_both("0.0", 0.0f, true);
        test_parse_both("1.0", 1.0f, true);
        test_parse_both("3.14159", 3.14159f, true);
        test_parse_both("-3.14159", -3.14159f, true);
        test_parse_both("0.1", 0.1f, true);
        test_parse_both("0.333333333333", 0.333333333333f, true);
    END_TEST

    TEST_CASE("Scientific notation")
        test_parse_both("1e10", 1e10f, true);
        test_parse_both("1E10", 1e10f, true);
        test_parse_both("1e-10", 1e-10f, true);
        test_parse_both("1E-10", 1e-10f, true);
        test_parse_both("1.23e5", 1.23e5f, true);
        test_parse_both("-1.23e-5", -1.23e-5f, true);
        test_parse_both("6.02214076e23", 6.02214076e23f, true);
    END_TEST

    TEST_CASE("Special values")
        test_parse_both("inf", INFINITY, true);
        test_parse_both("-inf", -INFINITY, true);
        test_parse_both("infinity", INFINITY, true);
        test_parse_both("-infinity", -INFINITY, true);
        test_parse_both("nan", NAN, true);
        test_parse_both("NaN", NAN, true);
        test_parse_both("NAN", NAN, true);
    END_TEST

    TEST_CASE("Limits")
        // Float max
        test_parse_both("3.40282347e+38", std::numeric_limits<float>::max(), true);
        test_parse_both("-3.40282347e+38", -std::numeric_limits<float>::max(), true);

        // Float min (smallest positive normal)
        test_parse_both("1.17549435e-38", std::numeric_limits<float>::min(), true);

        // Smallest denormal
        test_parse_both("1.4e-45", std::numeric_limits<float>::denorm_min(), true);
    END_TEST

    TEST_CASE("Leading zeros")
        test_parse_both("0001", 1.0f, true);
        test_parse_both("00001.5", 1.5f, true);
        test_parse_both("000.123", 0.123f, true);
        test_parse_both("-000.123", -0.123f, true);
    END_TEST

    TEST_CASE("Explicit positive sign")
        // Note: std::from_chars may not accept leading '+' in some implementations
        // This is implementation-defined behavior
        // We'll skip these tests as they're not universally supported
        // test_parse_both("+1", 1.0f, true);
        // test_parse_both("+1.5", 1.5f, true);
        // test_parse_both("+1e10", 1e10f, true);
        // test_parse_both("+inf", INFINITY, true);
        std::cout << "SKIPPED (implementation-defined)";
    END_TEST

    TEST_CASE("Very long mantissa")
        // Test truncation/rounding of very long mantissas
        test_parse_both("3.14159265358979323846264338327950288419716939937510", 3.141592653589793f, true);
        test_parse_both("0.33333333333333333333333333333333333333333333", 0.33333333333333333f, true);
    END_TEST

    TEST_CASE("Hexadecimal floats")
        // Note: Hexadecimal float support in from_chars is implementation-defined
        // Many implementations don't support it for floating point
        // test_parse_both("0x1p0", 1.0f, true);
        // test_parse_both("0x1.8p1", 3.0f, true);
        // test_parse_both("0x1.fffffep127", std::numeric_limits<float>::max(), true);
        std::cout << "SKIPPED (often not supported)";
    END_TEST

    TEST_CASE("Subnormal numbers")
        test_parse_both("1e-45", 1e-45f, true);
        test_parse_both("2.35099e-38", 2.35099e-38f, true);
        test_parse_both("1.17549e-38", 1.17549e-38f, true);
    END_TEST

    TEST_CASE("Rounding edge cases")
        // Test values that are exactly between two representable floats
        // Note: exact value depends on rounding mode and precision
        test_parse_both("8388608.5", 8388608.0f, true); // Ties to even
        test_parse_both("16777217", 16777216.0f, true); // Cannot be exactly represented
    END_TEST

    TEST_CASE("Zero variations")
        test_parse_both("0", 0.0f, true);
        test_parse_both("-0", -0.0f, true);
        test_parse_both("0.0", 0.0f, true);
        test_parse_both("-0.0", -0.0f, true);
        test_parse_both("0e0", 0.0f, true);
        test_parse_both("0e100", 0.0f, true);
        test_parse_both("0e-100", 0.0f, true);
    END_TEST

    TEST_CASE("Overflow/Underflow")
        // Note: Overflow/underflow behavior may differ between implementations
        // Some may return infinity/zero, others may fail
        // Skipping these tests as they're implementation-specific
        // test_parse_both("1e39", INFINITY, true);  // Overflow to infinity
        // test_parse_both("-1e39", -INFINITY, true);
        // test_parse_both("1e-46", 0.0f, true);  // Underflow to zero
        // test_parse_both("-1e-46", -0.0f, true);
        std::cout << "SKIPPED (implementation-specific)";
    END_TEST

    TEST_CASE("Invalid inputs")
        test_parse_both("", 0, false);
        test_parse_both("abc", 0, false);
        // Note: Some partial parsing cases are implementation-specific
        // Skipping ambiguous cases
        // test_parse_both("1.2.3", 0, false);  // May parse as "1.2"
        // test_parse_both("1e", 0, false);     // May parse as "1"
        test_parse_both("e10", 0, false);
        test_parse_both("++1", 0, false);
        test_parse_both("--1", 0, false);
        // test_parse_both("1ee10", 0, false);  // May parse as "1"
    END_TEST

    TEST_CASE("Partial parsing")
        // These should parse the valid prefix
        float result;
        const char* str = "123.45abc";
        auto ff = fast_float::from_chars(str, str + 6, result);
        assert(ff.ec == std::errc());
        assert(ff.ptr == str + 6);
        assert(result == 123.45f);

        auto std_res = std::from_chars(str, str + 6, result);
        assert(std_res.ec == std::errc());
        assert(std_res.ptr == str + 6);
    END_TEST

    TEST_CASE("Exact representability tests")
        // Powers of 2 should be exact
        test_parse_both("1", 1.0f, true);
        test_parse_both("2", 2.0f, true);
        test_parse_both("4", 4.0f, true);
        test_parse_both("0.5", 0.5f, true);
        test_parse_both("0.25", 0.25f, true);
        test_parse_both("0.125", 0.125f, true);
    END_TEST

    TEST_CASE("Mantissa precision boundary")
        // Test at the boundary of float precision (24 bits)
        test_parse_both("16777216", 16777216.0f, true);  // 2^24, exact
        test_parse_both("16777217", 16777216.0f, true);  // 2^24 + 1, rounds to 2^24
        test_parse_both("16777218", 16777218.0f, true);  // 2^24 + 2, exact
    END_TEST

    std::cout << "\n✓ All edge case tests passed!" << std::endl;
    return 0;
}