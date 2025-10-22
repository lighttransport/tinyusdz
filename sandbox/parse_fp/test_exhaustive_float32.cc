// Exhaustive test for all 32-bit float patterns
// Tests string-to-float parsing against std::from_chars reference
// Optimized for performance with multithreading

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
#include <cstdint>
#include <array>
#include <algorithm>

#include "fast_float/fast_float.h"

// Configuration
constexpr size_t PROGRESS_INTERVAL = 10000000; // Report progress every 10M values
constexpr size_t ERROR_REPORT_LIMIT = 1000;    // Only report first 1000 errors

// Global statistics
std::atomic<uint64_t> g_total_tested{0};
std::atomic<uint64_t> g_total_passed{0};
std::atomic<uint64_t> g_total_failed{0};
std::atomic<uint64_t> g_total_skipped{0};
std::atomic<bool> g_stop_on_error{false};

// Error reporting
struct ErrorInfo {
    uint32_t float_bits;
    std::string str_representation;
    float expected_value;
    float actual_value;
    bool custom_parsed;
    bool reference_parsed;
};

std::vector<ErrorInfo> g_errors;
std::mutex g_error_mutex;

// Fast float to string conversion with proper precision
inline std::string float_to_string_precise(float f) {
    // Handle special cases first
    if (std::isnan(f)) return "nan";
    if (f == std::numeric_limits<float>::infinity()) return "inf";
    if (f == -std::numeric_limits<float>::infinity()) return "-inf";

    // Use high precision to ensure round-trip conversion
    char buffer[64];
    int len = std::snprintf(buffer, sizeof(buffer), "%.9g", f);

    // Verify round-trip by parsing back
    float parsed;
    auto result = std::from_chars(buffer, buffer + len, parsed);

    // If round-trip fails, use more precision
    if (result.ec != std::errc() || parsed != f) {
        len = std::snprintf(buffer, sizeof(buffer), "%.17g", static_cast<double>(f));
    }

    return std::string(buffer, len);
}

// Worker function for testing a range of float bit patterns
void test_float_range(uint64_t start_bits, uint64_t end_bits, int thread_id) {
    uint64_t local_tested = 0;
    uint64_t local_passed = 0;
    uint64_t local_failed = 0;
    uint64_t local_skipped = 0;

    auto last_progress_time = std::chrono::steady_clock::now();

    for (uint64_t bits = start_bits; bits <= end_bits && !g_stop_on_error; ++bits) {
        uint32_t float_bits = static_cast<uint32_t>(bits);
        float test_float;
        std::memcpy(&test_float, &float_bits, sizeof(float));

        local_tested++;

        // Skip multiple NaN representations (test only one canonical NaN)
        if (std::isnan(test_float)) {
            // Only test the canonical quiet NaN (0x7FC00000)
            if (float_bits != 0x7FC00000) {
                local_skipped++;
                continue;
            }
        }

        // Convert float to string
        std::string str_repr = float_to_string_precise(test_float);

        // Test with custom implementation (fast_float)
        float custom_result = 0;
        auto custom_parse_result = fast_float::from_chars(
            str_repr.data(),
            str_repr.data() + str_repr.size(),
            custom_result
        );
        bool custom_success = (custom_parse_result.ec == std::errc());

        // Test with reference implementation (std::from_chars)
        float reference_result = 0;
        auto ref_parse_result = std::from_chars(
            str_repr.data(),
            str_repr.data() + str_repr.size(),
            reference_result
        );
        bool reference_success = (ref_parse_result.ec == std::errc());

        // Compare results
        bool test_passed = false;

        if (custom_success != reference_success) {
            // Parsing success/failure mismatch
            test_passed = false;
        } else if (custom_success) {
            // Both parsed successfully - check values
            if (std::isnan(test_float)) {
                // For NaN, just check that both results are NaN
                test_passed = std::isnan(custom_result) && std::isnan(reference_result);
            } else {
                // For normal values, require bit-exact match
                uint32_t custom_bits, reference_bits;
                std::memcpy(&custom_bits, &custom_result, sizeof(float));
                std::memcpy(&reference_bits, &reference_result, sizeof(float));
                test_passed = (custom_bits == reference_bits);
            }
        } else {
            // Both failed to parse - this is correct
            test_passed = true;
        }

        if (test_passed) {
            local_passed++;
        } else {
            local_failed++;

            // Record error details
            if (g_errors.size() < ERROR_REPORT_LIMIT) {
                ErrorInfo error;
                error.float_bits = float_bits;
                error.str_representation = str_repr;
                error.expected_value = reference_result;
                error.actual_value = custom_result;
                error.custom_parsed = custom_success;
                error.reference_parsed = reference_success;

                std::lock_guard<std::mutex> lock(g_error_mutex);
                if (g_errors.size() < ERROR_REPORT_LIMIT) {
                    g_errors.push_back(error);
                }
            }
        }

        // Update global statistics periodically
        if (local_tested % 1000000 == 0) {
            g_total_tested += 1000000;
            g_total_passed += local_passed;
            g_total_failed += local_failed;
            g_total_skipped += local_skipped;

            local_passed = 0;
            local_failed = 0;
            local_skipped = 0;

            // Progress reporting
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_progress_time).count() >= 10) {
                uint64_t total = g_total_tested.load();
                double percent = (double(bits - start_bits) / double(end_bits - start_bits)) * 100.0;

                std::cout << "[Thread " << thread_id << "] "
                         << "Progress: " << percent << "% "
                         << "(" << total << " total tested)" << std::endl;

                last_progress_time = now;
            }
        }
    }

    // Final update
    g_total_tested += (local_tested % 1000000);
    g_total_passed += local_passed;
    g_total_failed += local_failed;
    g_total_skipped += local_skipped;
}

void print_error_summary() {
    if (g_errors.empty()) {
        std::cout << "\nNo errors found!" << std::endl;
        return;
    }

    std::cout << "\n=== Error Summary ===" << std::endl;
    std::cout << "Total errors: " << g_total_failed.load() << std::endl;

    if (g_total_failed > ERROR_REPORT_LIMIT) {
        std::cout << "Showing first " << ERROR_REPORT_LIMIT << " errors:" << std::endl;
    }

    for (size_t i = 0; i < std::min(g_errors.size(), size_t(10)); ++i) {
        const auto& error = g_errors[i];
        std::cout << "\nError #" << (i + 1) << ":" << std::endl;
        std::cout << "  Bit pattern: 0x" << std::hex << error.float_bits << std::dec << std::endl;
        std::cout << "  String: \"" << error.str_representation << "\"" << std::endl;
        std::cout << "  Reference parsed: " << (error.reference_parsed ? "yes" : "no");
        if (error.reference_parsed) {
            std::cout << " (value: " << error.expected_value << ")";
        }
        std::cout << std::endl;
        std::cout << "  Custom parsed: " << (error.custom_parsed ? "yes" : "no");
        if (error.custom_parsed) {
            std::cout << " (value: " << error.actual_value << ")";
        }
        std::cout << std::endl;
    }

    if (g_errors.size() > 10) {
        std::cout << "\n... and " << (g_errors.size() - 10) << " more errors" << std::endl;
    }
}

int main(int argc, char* argv[]) {
    // Parse command line arguments
    size_t num_threads = std::thread::hardware_concurrency();
    bool stop_on_error = false;
    bool quick_test = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--threads" && i + 1 < argc) {
            num_threads = std::stoul(argv[++i]);
        } else if (arg == "--stop-on-error") {
            stop_on_error = true;
        } else if (arg == "--quick") {
            quick_test = true;
        } else if (arg == "--help") {
            std::cout << "Usage: " << argv[0] << " [options]" << std::endl;
            std::cout << "Options:" << std::endl;
            std::cout << "  --threads N       Number of threads (default: " << num_threads << ")" << std::endl;
            std::cout << "  --stop-on-error   Stop testing when first error is found" << std::endl;
            std::cout << "  --quick           Quick test (only first 100M patterns)" << std::endl;
            std::cout << "  --help            Show this help message" << std::endl;
            return 0;
        }
    }

    if (num_threads == 0) num_threads = 8;

    g_stop_on_error = stop_on_error;

    std::cout << "=== Exhaustive 32-bit Float String Parsing Test ===" << std::endl;
    std::cout << "Testing fast_float against std::from_chars reference" << std::endl;
    std::cout << "Using " << num_threads << " threads" << std::endl;

    uint64_t total_patterns = quick_test ? 100000000ULL : (1ULL << 32);
    std::cout << "Testing " << total_patterns << " bit patterns ";
    if (quick_test) {
        std::cout << "(quick mode)";
    } else {
        std::cout << "(full 2^32 patterns)";
    }
    std::cout << std::endl;

    if (!quick_test) {
        std::cout << "\nWARNING: Full test will take several hours!" << std::endl;
        std::cout << "Use --quick for a faster test of first 100M patterns" << std::endl;
        std::cout << "Starting in 5 seconds... (Ctrl+C to cancel)" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    auto start_time = std::chrono::steady_clock::now();

    // Create threads
    std::vector<std::thread> threads;
    uint64_t chunk_size = total_patterns / num_threads;

    for (size_t i = 0; i < num_threads; ++i) {
        uint64_t start = i * chunk_size;
        uint64_t end = (i == num_threads - 1) ? (total_patterns - 1) : (start + chunk_size - 1);

        threads.emplace_back(test_float_range, start, end, i);
    }

    // Monitor progress in main thread
    while (g_total_tested < total_patterns) {
        std::this_thread::sleep_for(std::chrono::seconds(30));

        uint64_t tested = g_total_tested.load();
        uint64_t passed = g_total_passed.load();
        uint64_t failed = g_total_failed.load();
        uint64_t skipped = g_total_skipped.load();

        double percent = (double(tested) / double(total_patterns)) * 100.0;

        auto elapsed = std::chrono::steady_clock::now() - start_time;
        auto elapsed_sec = std::chrono::duration_cast<std::chrono::seconds>(elapsed).count();

        double rate = tested / (elapsed_sec + 1.0);
        uint64_t remaining = total_patterns - tested;
        uint64_t eta_sec = remaining / (rate + 1.0);

        std::cout << "\n=== Progress Update ===" << std::endl;
        std::cout << "Progress: " << std::fixed << std::setprecision(2) << percent << "%" << std::endl;
        std::cout << "Tested: " << tested << " / " << total_patterns << std::endl;
        std::cout << "Passed: " << passed << std::endl;
        std::cout << "Failed: " << failed << std::endl;
        std::cout << "Skipped: " << skipped << " (duplicate NaN representations)" << std::endl;
        std::cout << "Rate: " << std::fixed << std::setprecision(0) << rate << " tests/sec" << std::endl;
        std::cout << "ETA: " << (eta_sec / 3600) << "h " << ((eta_sec % 3600) / 60) << "m" << std::endl;

        if (failed > 0 && stop_on_error) {
            g_stop_on_error = true;
            std::cout << "\nStopping due to error (--stop-on-error enabled)" << std::endl;
            break;
        }
    }

    // Wait for all threads
    for (auto& t : threads) {
        t.join();
    }

    auto end_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(end_time - start_time);

    // Final summary
    std::cout << "\n=== Final Results ===" << std::endl;
    std::cout << "Total tested: " << g_total_tested.load() << std::endl;
    std::cout << "Total passed: " << g_total_passed.load() << std::endl;
    std::cout << "Total failed: " << g_total_failed.load() << std::endl;
    std::cout << "Total skipped: " << g_total_skipped.load() << std::endl;
    std::cout << "Test duration: " << duration.count() << " seconds" << std::endl;

    double pass_rate = (double(g_total_passed) / double(g_total_tested - g_total_skipped)) * 100.0;
    std::cout << "Pass rate: " << std::fixed << std::setprecision(6) << pass_rate << "%" << std::endl;

    // Print error details if any
    print_error_summary();

    return g_total_failed > 0 ? 1 : 0;
}