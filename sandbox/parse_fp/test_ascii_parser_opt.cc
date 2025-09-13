#define TINYUSDZ_PARSER_OPT  // Enable optimizations

#include "../../src/ascii-parser.hh"
#include "../../src/io-util.hh"
#include "../../src/stream-reader.hh"
#include <iostream>
#include <vector>
#include <chrono>
#include <sstream>
#include <random>

// Test data generation
std::string gen_test_floatarray(size_t n) {
    std::stringstream ss;
    std::random_device rd;
    std::mt19937 engine(rd());
    std::uniform_real_distribution<> dist(-1000.0, 1000.0);

    ss << "[";
    for (size_t i = 0; i < n; i++) {
        double f = dist(engine);
        ss << f;
        if (i < (n-1)) {
            ss << ", ";
        }
    }
    ss << "]";

    return ss.str();
}

std::string gen_test_float2array(size_t n) {
    std::stringstream ss;
    std::random_device rd;
    std::mt19937 engine(rd());
    std::uniform_real_distribution<> dist(-1000.0, 1000.0);

    ss << "[";
    for (size_t i = 0; i < n; i++) {
        double f1 = dist(engine);
        double f2 = dist(engine);
        ss << "(" << f1 << ", " << f2 << ")";
        if (i < (n-1)) {
            ss << ", ";
        }
    }
    ss << "]";

    return ss.str();
}

int main() {
    std::cout << "Testing TinyUSDZ ASCII Parser with TINYUSDZ_PARSER_OPT enabled\n";
    std::cout << "============================================================\n";
    
    // Test 1: Float array parsing
    std::cout << "\nTest 1: Float array parsing (10K elements)\n";
    {
        std::string input = gen_test_floatarray(10000);
        std::cout << "Input size: " << input.size() << " bytes\n";
        
        tinyusdz::StreamReader sr(reinterpret_cast<const uint8_t*>(input.c_str()), 
                                  input.size(), /* swap_endian */ false);
        tinyusdz::ascii::AsciiParser parser(&sr);
        
        std::vector<float> result;
        
        auto start = std::chrono::steady_clock::now();
        bool success = parser.ParseBasicTypeArray(&result);
        auto end = std::chrono::steady_clock::now();
        
        if (success) {
            std::cout << "✓ Successfully parsed " << result.size() << " floats\n";
            std::cout << "Parse time: " << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << " µs\n";
            if (result.size() > 0) {
                std::cout << "First value: " << result[0] << "\n";
                std::cout << "Last value: " << result[result.size()-1] << "\n";
            }
        } else {
            std::cout << "✗ Parse failed: " << parser.GetError() << "\n";
        }
    }
    
    // Test 2: Float2 array parsing
    std::cout << "\nTest 2: Float2 array parsing (5K elements)\n";
    {
        std::string input = gen_test_float2array(5000);
        std::cout << "Input size: " << input.size() << " bytes\n";
        
        tinyusdz::StreamReader sr(reinterpret_cast<const uint8_t*>(input.c_str()), 
                                  input.size(), /* swap_endian */ false);
        tinyusdz::ascii::AsciiParser parser(&sr);
        
        std::vector<tinyusdz::value::float2> result;
        
        auto start = std::chrono::steady_clock::now();
        bool success = parser.ParseBasicTypeArray(&result);
        auto end = std::chrono::steady_clock::now();
        
        if (success) {
            std::cout << "✓ Successfully parsed " << result.size() << " float2 tuples\n";
            std::cout << "Parse time: " << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << " µs\n";
            if (result.size() > 0) {
                std::cout << "First tuple: (" << result[0][0] << ", " << result[0][1] << ")\n";
                std::cout << "Last tuple: (" << result.back()[0] << ", " << result.back()[1] << ")\n";
            }
        } else {
            std::cout << "✗ Parse failed: " << parser.GetError() << "\n";
        }
    }
    
    // Test 3: Special values
    std::cout << "\nTest 3: Special values parsing\n";
    {
        std::string input = "[1.0, -2.5, inf, -inf, nan, 3.14e-5, 2.3e4]";
        std::cout << "Input: " << input << "\n";
        
        tinyusdz::StreamReader sr(reinterpret_cast<const uint8_t*>(input.c_str()), 
                                  input.size(), /* swap_endian */ false);
        tinyusdz::ascii::AsciiParser parser(&sr);
        
        std::vector<float> result;
        
        bool success = parser.ParseBasicTypeArray(&result);
        
        if (success) {
            std::cout << "✓ Successfully parsed " << result.size() << " values:\n";
            for (size_t i = 0; i < result.size(); i++) {
                std::cout << "  [" << i << "] = ";
                if (std::isinf(result[i])) {
                    if (result[i] > 0) std::cout << "inf";
                    else std::cout << "-inf";
                } else if (std::isnan(result[i])) {
                    std::cout << "nan";
                } else {
                    std::cout << result[i];
                }
                std::cout << "\n";
            }
        } else {
            std::cout << "✗ Parse failed: " << parser.GetError() << "\n";
        }
    }
    
    std::cout << "\nAll tests completed!\n";
    return 0;
}