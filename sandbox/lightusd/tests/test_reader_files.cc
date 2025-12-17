// SPDX-License-Identifier: Apache-2.0
// Test USDA and USDC readers with test files

#include "lightusd/lightusd.hh"
#include "lightusd/usda_reader.hh"
#include "lightusd/usdc_reader.hh"

#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {

// Read file into string
bool read_file(const std::string& path, std::string& out) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return false;
    out.assign((std::istreambuf_iterator<char>(ifs)),
               std::istreambuf_iterator<char>());
    return true;
}

// Recursively find all files with given extension
void find_files(const std::string& dir, const std::string& ext,
                std::vector<std::string>& files, bool exclude_failure = false) {
    DIR* d = opendir(dir.c_str());
    if (!d) return;

    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        std::string name = entry->d_name;
        if (name == "." || name == "..") continue;

        std::string path = dir + "/" + name;

        struct stat st;
        if (stat(path.c_str(), &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            // Skip failure-case directories if requested
            if (exclude_failure && name.find("failure") != std::string::npos) {
                continue;
            }
            find_files(path, ext, files, exclude_failure);
        } else if (name.size() > ext.size() &&
                   name.substr(name.size() - ext.size()) == ext) {
            files.push_back(path);
        }
    }
    closedir(d);
}

// Find failure case files
void find_failure_files(const std::string& dir, const std::string& ext,
                        std::vector<std::string>& files) {
    // Look in failure-case subdirectory
    std::string failure_dir = dir + "/failure-case";
    DIR* d = opendir(failure_dir.c_str());
    if (!d) return;

    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        std::string name = entry->d_name;
        if (name.size() > ext.size() &&
            name.substr(name.size() - ext.size()) == ext) {
            files.push_back(failure_dir + "/" + name);
        }
    }
    closedir(d);
}

struct TestResults {
    int passed = 0;
    int failed = 0;
    std::vector<std::string> failures;
};

// Test USDA files
void test_usda_files(const std::string& test_dir, TestResults& results, bool expect_failure = false) {
    std::vector<std::string> files;
    if (expect_failure) {
        find_failure_files(test_dir, ".usda", files);
    } else {
        find_files(test_dir, ".usda", files, true);  // exclude failure-case
    }

    std::sort(files.begin(), files.end());

    std::cout << (expect_failure ? "[USDA Failure Cases]" : "[USDA Valid Files]")
              << " Testing " << files.size() << " files...\n";

    for (const auto& path : files) {
        std::string content;
        if (!read_file(path, content)) {
            std::cerr << "  SKIP (cannot read): " << path << "\n";
            continue;
        }

        // Use read_usda_string from the API
        lightusd::v1::ReaderOptions options;
        auto result = lightusd::v1::read_usda_string(content, path, options);
        bool success = result.ok();

        if (expect_failure) {
            // Failure expected - success is BAD
            if (success) {
                results.failed++;
                results.failures.push_back(path + " (expected failure but passed)");
                std::cout << "  UNEXPECTED PASS: " << path << "\n";
            } else {
                results.passed++;
            }
        } else {
            // Success expected
            if (success) {
                results.passed++;
            } else {
                results.failed++;
                results.failures.push_back(path + ": " + result.format_errors());
                std::cout << "  FAIL: " << path << "\n";
                std::cout << "        " << result.format_errors() << "\n";
            }
        }
    }
}

// Test USDC files
void test_usdc_files(const std::string& test_dir, TestResults& results, bool expect_failure = false) {
    std::vector<std::string> files;
    if (expect_failure) {
        find_failure_files(test_dir, ".usdc", files);
    } else {
        find_files(test_dir, ".usdc", files, true);  // exclude failure-case
    }

    std::sort(files.begin(), files.end());

    std::cout << (expect_failure ? "[USDC Failure Cases]" : "[USDC Valid Files]")
              << " Testing " << files.size() << " files...\n";

    for (const auto& path : files) {
        std::string content;
        if (!read_file(path, content)) {
            std::cerr << "  SKIP (cannot read): " << path << "\n";
            continue;
        }

        lightusd::v1::UsdcReader reader;
        auto result = reader.read(reinterpret_cast<const uint8_t*>(content.data()),
                                  content.size());
        bool success = result.ok();

        if (expect_failure) {
            // Failure expected - success is BAD
            if (success) {
                results.failed++;
                results.failures.push_back(path + " (expected failure but passed)");
                std::cout << "  UNEXPECTED PASS: " << path << "\n";
            } else {
                results.passed++;
            }
        } else {
            // Success expected
            if (success) {
                results.passed++;
            } else {
                results.failed++;
                results.failures.push_back(path + ": " + result.error().message);
                std::cout << "  FAIL: " << path << "\n";
                std::cout << "        " << result.error().message << "\n";
            }
        }
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string usda_dir = "/mnt/disk1/work/tinyusdz-git/value-opt/tests/usda";
    std::string usdc_dir = "/mnt/disk1/work/tinyusdz-git/value-opt/tests/usdc";

    // Allow override from command line
    if (argc > 1) {
        usda_dir = argv[1];
        usda_dir += "/usda";
    }
    if (argc > 2) {
        usdc_dir = argv[2];
        usdc_dir += "/usdc";
    }

    TestResults usda_results;
    TestResults usda_failure_results;
    TestResults usdc_results;
    TestResults usdc_failure_results;

    std::cout << "========================================\n";
    std::cout << "LightUSD Reader Test Suite\n";
    std::cout << "========================================\n\n";

    // Test USDA valid files
    test_usda_files(usda_dir, usda_results, false);
    std::cout << "  Passed: " << usda_results.passed
              << ", Failed: " << usda_results.failed << "\n\n";

    // Test USDA failure cases
    test_usda_files(usda_dir, usda_failure_results, true);
    std::cout << "  Passed: " << usda_failure_results.passed
              << ", Failed: " << usda_failure_results.failed << "\n\n";

    // Test USDC valid files
    test_usdc_files(usdc_dir, usdc_results, false);
    std::cout << "  Passed: " << usdc_results.passed
              << ", Failed: " << usdc_results.failed << "\n\n";

    // Test USDC failure cases
    test_usdc_files(usdc_dir, usdc_failure_results, true);
    std::cout << "  Passed: " << usdc_failure_results.passed
              << ", Failed: " << usdc_failure_results.failed << "\n\n";

    // Summary
    std::cout << "========================================\n";
    std::cout << "Summary\n";
    std::cout << "========================================\n";
    std::cout << "USDA Valid:      " << usda_results.passed << "/"
              << (usda_results.passed + usda_results.failed) << " passed\n";
    std::cout << "USDA Failure:    " << usda_failure_results.passed << "/"
              << (usda_failure_results.passed + usda_failure_results.failed) << " correctly rejected\n";
    std::cout << "USDC Valid:      " << usdc_results.passed << "/"
              << (usdc_results.passed + usdc_results.failed) << " passed\n";
    std::cout << "USDC Failure:    " << usdc_failure_results.passed << "/"
              << (usdc_failure_results.passed + usdc_failure_results.failed) << " correctly rejected\n";

    int total_failures = usda_results.failed + usda_failure_results.failed
                       + usdc_results.failed + usdc_failure_results.failed;

    if (total_failures > 0) {
        std::cout << "\n========================================\n";
        std::cout << "Failed Files (" << total_failures << ")\n";
        std::cout << "========================================\n";

        auto print_failures = [](const std::vector<std::string>& failures) {
            for (const auto& f : failures) {
                // Extract just filename
                size_t pos = f.rfind('/');
                std::string name = (pos != std::string::npos) ? f.substr(pos + 1) : f;
                std::cout << "  " << name << "\n";
            }
        };

        if (!usda_results.failures.empty()) {
            std::cout << "\nUSDA failures:\n";
            print_failures(usda_results.failures);
        }
        if (!usda_failure_results.failures.empty()) {
            std::cout << "\nUSDA unexpected passes:\n";
            print_failures(usda_failure_results.failures);
        }
        if (!usdc_results.failures.empty()) {
            std::cout << "\nUSDC failures:\n";
            print_failures(usdc_results.failures);
        }
        if (!usdc_failure_results.failures.empty()) {
            std::cout << "\nUSDC unexpected passes:\n";
            print_failures(usdc_failure_results.failures);
        }
    }

    return total_failures > 0 ? 1 : 0;
}
