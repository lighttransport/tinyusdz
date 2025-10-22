// Comprehensive test suite for string to floating point parsing
// Uses std::from_chars as reference implementation
// Includes exhaustive 32-bit float testing with multithreading

#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <iomanip>
#include <charconv>
#include <cstring>
#include <cmath>
#include <limits>
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>
#include <random>
#include <algorithm>
#include <cstdint>

// Include fast_float library if you want to test against it
#include "fast_float/fast_float.h"

// Function under test - replace with your actual string to float function
// For now, we'll test fast_float against std::from_chars
template<typename T>
bool parse_float_custom(const char* begin, const char* end, T& value) {
    auto result = fast_float::from_chars(begin, end, value);
    return result.ec == std::errc();
}

template<typename T>
bool parse_float_reference(const char* begin, const char* end, T& value) {
    auto result = std::from_chars(begin, end, value);
    return result.ec == std::errc();
}

// Helper to convert float to string with full precision
std::string float_to_string(float f) {
    // Handle special cases
    if (std::isnan(f)) return "nan";
    if (std::isinf(f)) return f > 0 ? "inf" : "-inf";

    // Use scientific notation for very large/small values
    std::ostringstream oss;
    oss << std::setprecision(9) << f;
    return oss.str();
}

std::string double_to_string(double d) {
    if (std::isnan(d)) return "nan";
    if (std::isinf(d)) return d > 0 ? "inf" : "-inf";

    std::ostringstream oss;
    oss << std::setprecision(17) << d;
    return oss.str();
}

// Test result structure
struct TestResult {
    bool passed;
    std::string test_name;
    std::string input;
    std::string expected;
    std::string actual;
    std::string error_msg;
};

class FloatParseTestSuite {
public:
    std::vector<TestResult> results;
    std::mutex results_mutex;
    std::atomic<size_t> total_tests{0};
    std::atomic<size_t> passed_tests{0};
    std::atomic<size_t> failed_tests{0};

    void add_result(const TestResult& result) {
        std::lock_guard<std::mutex> lock(results_mutex);
        results.push_back(result);
        total_tests++;
        if (result.passed) {
            passed_tests++;
        } else {
            failed_tests++;
        }
    }

    void print_summary() {
        std::cout << "\n=== Test Summary ===" << std::endl;
        std::cout << "Total tests: " << total_tests << std::endl;
        std::cout << "Passed: " << passed_tests << std::endl;
        std::cout << "Failed: " << failed_tests << std::endl;

        if (failed_tests > 0) {
            std::cout << "\nFailed tests:" << std::endl;
            for (const auto& result : results) {
                if (!result.passed) {
                    std::cout << "  Test: " << result.test_name << std::endl;
                    std::cout << "    Input: '" << result.input << "'" << std::endl;
                    std::cout << "    Expected: " << result.expected << std::endl;
                    std::cout << "    Actual: " << result.actual << std::endl;
                    if (!result.error_msg.empty()) {
                        std::cout << "    Error: " << result.error_msg << std::endl;
                    }
                }
            }
        }
    }

    // Test edge cases and special values
    void test_edge_cases() {
        std::cout << "Testing edge cases..." << std::endl;

        struct EdgeCase {
            std::string input;
            float expected;
            bool should_parse;
            std::string description;
        };

        std::vector<EdgeCase> edge_cases = {
            // Basic numbers
            {"0", 0.0f, true, "Zero"},
            {"1", 1.0f, true, "One"},
            {"-1", -1.0f, true, "Negative one"},
            {"0.0", 0.0f, true, "Zero with decimal"},

            // Scientific notation
            {"1e10", 1e10f, true, "Scientific notation positive"},
            {"1e-10", 1e-10f, true, "Scientific notation negative"},
            {"1.23e5", 1.23e5f, true, "Scientific with decimal"},
            {"1E10", 1e10f, true, "Scientific with capital E"},

            // Special values
            {"inf", std::numeric_limits<float>::infinity(), true, "Positive infinity"},
            {"-inf", -std::numeric_limits<float>::infinity(), true, "Negative infinity"},
            {"infinity", std::numeric_limits<float>::infinity(), true, "Full infinity word"},
            {"-infinity", -std::numeric_limits<float>::infinity(), true, "Negative full infinity"},
            {"nan", std::numeric_limits<float>::quiet_NaN(), true, "NaN"},
            {"NaN", std::numeric_limits<float>::quiet_NaN(), true, "NaN capital"},

            // Limits
            {"3.40282347e+38", std::numeric_limits<float>::max(), true, "Float max"},
            {"-3.40282347e+38", -std::numeric_limits<float>::max(), true, "Float min"},
            {"1.17549435e-38", std::numeric_limits<float>::min(), true, "Float smallest positive"},

            // Very small numbers (subnormal)
            {"1e-45", 1e-45f, true, "Subnormal number"},
            {"1.4e-45", 1.4e-45f, true, "Smallest subnormal"},

            // Precision tests
            {"0.1", 0.1f, true, "0.1 (cannot be exactly represented)"},
            {"0.3", 0.3f, true, "0.3"},
            {"0.333333333333333333", 0.333333333333333333f, true, "Many decimals"},

            // Leading/trailing zeros
            {"000123.456", 123.456f, true, "Leading zeros"},
            {"123.456000", 123.456f, true, "Trailing zeros"},
            {"0000.0000", 0.0f, true, "All zeros"},

            // Sign handling
            {"+123.456", 123.456f, true, "Explicit positive sign"},
            {"++123", 0, false, "Double positive sign"},
            {"--123", 0, false, "Double negative sign"},

            // Invalid inputs
            {"", 0, false, "Empty string"},
            {"abc", 0, false, "Non-numeric"},
            {"12.34.56", 0, false, "Multiple decimal points"},
            {"1e", 0, false, "Incomplete scientific notation"},
            {"e10", 0, false, "Scientific without mantissa"},
        };

        for (const auto& test : edge_cases) {
            float custom_val = 0, ref_val = 0;
            bool custom_ok = parse_float_custom(test.input.c_str(),
                                               test.input.c_str() + test.input.length(),
                                               custom_val);
            bool ref_ok = parse_float_reference(test.input.c_str(),
                                               test.input.c_str() + test.input.length(),
                                               ref_val);

            TestResult result;
            result.test_name = "EdgeCase: " + test.description;
            result.input = test.input;
            result.passed = (custom_ok == ref_ok);

            if (result.passed && custom_ok) {
                // Both parsed successfully - check values match
                if (std::isnan(test.expected)) {
                    result.passed = std::isnan(custom_val) && std::isnan(ref_val);
                } else {
                    result.passed = (custom_val == ref_val);
                }
            }

            result.expected = ref_ok ? std::to_string(ref_val) : "parse_failed";
            result.actual = custom_ok ? std::to_string(custom_val) : "parse_failed";

            if (!result.passed) {
                result.error_msg = "Mismatch between custom and reference implementation";
            }

            add_result(result);
        }
    }

    // Test random floating point strings
    void test_random_floats(size_t count) {
        std::cout << "Testing " << count << " random floats..." << std::endl;

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> dist(-1e10f, 1e10f);
        std::uniform_int_distribution<int> exp_dist(-38, 38);

        for (size_t i = 0; i < count; ++i) {
            float test_val = dist(gen);

            // Sometimes use scientific notation
            std::string str_val;
            if (i % 3 == 0) {
                int exp = exp_dist(gen);
                str_val = std::to_string(test_val / std::pow(10.0f, exp)) + "e" + std::to_string(exp);
            } else {
                str_val = float_to_string(test_val);
            }

            float custom_val = 0, ref_val = 0;
            bool custom_ok = parse_float_custom(str_val.c_str(),
                                               str_val.c_str() + str_val.length(),
                                               custom_val);
            bool ref_ok = parse_float_reference(str_val.c_str(),
                                               str_val.c_str() + str_val.length(),
                                               ref_val);

            TestResult result;
            result.test_name = "Random float #" + std::to_string(i);
            result.input = str_val;
            result.passed = (custom_ok == ref_ok);

            if (result.passed && custom_ok) {
                // Allow for small floating point differences
                float diff = std::abs(custom_val - ref_val);
                float max_val = std::max(std::abs(custom_val), std::abs(ref_val));
                float rel_error = max_val > 0 ? diff / max_val : diff;
                result.passed = (rel_error < 1e-6f) || (custom_val == ref_val);
            }

            result.expected = ref_ok ? float_to_string(ref_val) : "parse_failed";
            result.actual = custom_ok ? float_to_string(custom_val) : "parse_failed";

            if (!result.passed) {
                result.error_msg = "Mismatch in random float parsing";
            }

            add_result(result);
        }
    }

    // Exhaustive test for all 32-bit float patterns
    void test_exhaustive_float32_chunk(uint32_t start_bits, uint32_t end_bits) {
        size_t local_passed = 0;
        size_t local_failed = 0;
        size_t local_total = 0;

        for (uint64_t bits = start_bits; bits <= end_bits; ++bits) {
            uint32_t float_bits = static_cast<uint32_t>(bits);
            float test_float;
            std::memcpy(&test_float, &float_bits, sizeof(float));

            // Skip if not a valid number (NaN has many representations)
            // We'll test one NaN separately
            if (std::isnan(test_float) && bits != 0x7FC00000) {
                continue;
            }

            // Convert to string
            std::string str_val = float_to_string(test_float);

            // Parse with both implementations
            float custom_val = 0, ref_val = 0;
            bool custom_ok = parse_float_custom(str_val.c_str(),
                                               str_val.c_str() + str_val.length(),
                                               custom_val);
            bool ref_ok = parse_float_reference(str_val.c_str(),
                                               str_val.c_str() + str_val.length(),
                                               ref_val);

            local_total++;

            bool test_passed = (custom_ok == ref_ok);
            if (test_passed && custom_ok) {
                // Check values match (bit-exact for non-NaN)
                if (std::isnan(test_float)) {
                    test_passed = std::isnan(custom_val) && std::isnan(ref_val);
                } else {
                    uint32_t custom_bits, ref_bits;
                    std::memcpy(&custom_bits, &custom_val, sizeof(float));
                    std::memcpy(&ref_bits, &ref_val, sizeof(float));
                    test_passed = (custom_bits == ref_bits);
                }
            }

            if (test_passed) {
                local_passed++;
            } else {
                local_failed++;

                // Only report first few failures to avoid spam
                if (failed_tests < 100) {
                    TestResult result;
                    result.test_name = "Exhaustive float32 bits: 0x" +
                                      std::to_string(float_bits);
                    result.input = str_val;
                    result.passed = false;
                    result.expected = ref_ok ? float_to_string(ref_val) : "parse_failed";
                    result.actual = custom_ok ? float_to_string(custom_val) : "parse_failed";
                    result.error_msg = "Bit pattern mismatch";
                    add_result(result);
                }
            }

            // Progress reporting every million values
            if (local_total % 1000000 == 0) {
                std::cout << "  Progress: " << local_total << " values tested in this chunk..." << std::endl;
            }
        }

        passed_tests += local_passed;
        failed_tests += local_failed;
        total_tests += local_total;
    }

    void test_exhaustive_float32_parallel(size_t num_threads = 0) {
        if (num_threads == 0) {
            num_threads = std::thread::hardware_concurrency();
            if (num_threads == 0) num_threads = 8; // fallback
        }

        std::cout << "Starting exhaustive 32-bit float test with " << num_threads << " threads..." << std::endl;
        std::cout << "This will test approximately 4.3 billion patterns..." << std::endl;

        auto start_time = std::chrono::steady_clock::now();

        std::vector<std::thread> threads;
        uint64_t total_patterns = (1ULL << 32);
        uint64_t chunk_size = total_patterns / num_threads;

        for (size_t i = 0; i < num_threads; ++i) {
            uint32_t start = i * chunk_size;
            uint32_t end = (i == num_threads - 1) ? 0xFFFFFFFF : (start + chunk_size - 1);

            threads.emplace_back([this, start, end, i]() {
                std::cout << "Thread " << i << " testing range 0x" << std::hex << start
                         << " to 0x" << end << std::dec << std::endl;
                test_exhaustive_float32_chunk(start, end);
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);

        std::cout << "Exhaustive test completed in " << duration.count() << " seconds" << std::endl;
    }
};

int main(int argc, char* argv[]) {
    FloatParseTestSuite test_suite;

    // Parse command line arguments
    bool run_exhaustive = false;
    size_t num_threads = 0;
    size_t random_count = 10000;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--exhaustive") {
            run_exhaustive = true;
        } else if (arg == "--threads" && i + 1 < argc) {
            num_threads = std::stoul(argv[++i]);
        } else if (arg == "--random" && i + 1 < argc) {
            random_count = std::stoul(argv[++i]);
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --exhaustive     Run exhaustive 32-bit float test (warning: takes hours)" << std::endl;
            std::cout << "  --threads N      Number of threads for exhaustive test (default: auto)" << std::endl;
            std::cout << "  --random N       Number of random floats to test (default: 10000)" << std::endl;
            std::cout << "  --help           Show this help message" << std::endl;
            return 0;
        }
    }

    std::cout << "=== String to Float Parsing Test Suite ===" << std::endl;
    std::cout << "Testing implementation against std::from_chars reference" << std::endl;

    // Always run edge cases
    test_suite.test_edge_cases();

    // Run random tests
    test_suite.test_random_floats(random_count);

    // Run exhaustive test if requested
    if (run_exhaustive) {
        std::cout << "\nWARNING: Exhaustive test will take several hours to complete!" << std::endl;
        std::cout << "Press Ctrl+C to cancel, or wait 5 seconds to continue..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));

        test_suite.test_exhaustive_float32_parallel(num_threads);
    }

    // Print summary
    test_suite.print_summary();

    return test_suite.failed_tests > 0 ? 1 : 0;
}