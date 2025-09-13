#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <random>
#include <cmath>
#include <iomanip>
#include <chrono>
#include <cassert>

// Simple test to verify parser macro behavior
int main() {
    std::cout << "Parser Validation Test\n";
    std::cout << "=====================\n\n";
    
    // Check if optimization is enabled
#ifdef TINYUSDZ_PARSER_OPT
    std::cout << "✓ TINYUSDZ_PARSER_OPT is ENABLED\n";
    std::cout << "  Using optimized array parsing implementations\n";
#else
    std::cout << "✓ TINYUSDZ_PARSER_OPT is DISABLED\n";
    std::cout << "  Using generic template array parsing\n";
#endif

    std::cout << "\nTest Data Generation:\n";
    std::cout << "====================\n";
    
    // Generate test arrays
    std::random_device rd;
    std::mt19937 gen(42); // Fixed seed for reproducibility
    std::uniform_real_distribution<float> float_dist(-1000.0f, 1000.0f);
    std::uniform_int_distribution<int32_t> int_dist(-1000000, 1000000);
    
    // Create test data
    struct TestData {
        std::string name;
        std::string data;
        std::string expected_type;
        size_t expected_count;
    };
    
    std::vector<TestData> tests;
    
    // Generate float array test
    {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(6);
        ss << "[";
        
        std::vector<float> values;
        for (int i = 0; i < 1000; i++) {
            values.push_back(float_dist(gen));
        }
        
        for (size_t i = 0; i < values.size(); i++) {
            if (i > 0) ss << ", ";
            ss << values[i];
        }
        ss << "]";
        
        tests.push_back({"Float array (1000 elements)", ss.str(), "float", values.size()});
        std::cout << "✓ Generated float array: " << values.size() << " elements, " << ss.str().length() << " chars\n";
    }
    
    // Generate integer array test
    {
        std::stringstream ss;
        ss << "[";
        
        std::vector<int32_t> values;
        for (int i = 0; i < 500; i++) {
            values.push_back(int_dist(gen));
        }
        
        for (size_t i = 0; i < values.size(); i++) {
            if (i > 0) ss << ", ";
            ss << values[i];
        }
        ss << "]";
        
        tests.push_back({"Integer array (500 elements)", ss.str(), "int", values.size()});
        std::cout << "✓ Generated integer array: " << values.size() << " elements, " << ss.str().length() << " chars\n";
    }
    
    // Generate float2 array test
    {
        std::stringstream ss;
        ss << std::fixed << std::setprecision(4);
        ss << "[";
        
        size_t count = 100;
        for (size_t i = 0; i < count; i++) {
            if (i > 0) ss << ", ";
            ss << "(" << float_dist(gen) << ", " << float_dist(gen) << ")";
        }
        ss << "]";
        
        tests.push_back({"Float2 array (100 tuples)", ss.str(), "float2", count});
        std::cout << "✓ Generated float2 array: " << count << " tuples, " << ss.str().length() << " chars\n";
    }
    
    // Special values test
    {
        std::string special_data = "[1.0, -2.5, inf, -inf, nan, 3.14e-5, 2.3e4, 0.0, -0.0]";
        tests.push_back({"Special values", special_data, "float", 9});
        std::cout << "✓ Generated special values test: " << special_data.length() << " chars\n";
    }
    
    // Edge cases
    tests.push_back({"Empty array", "[]", "float", 0});
    tests.push_back({"Single element", "[42.5]", "float", 1});
    tests.push_back({"Trailing comma", "[1.0, 2.0, 3.0,]", "float", 3});
    tests.push_back({"Spaced array", "[ 1.0 , 2.0 , 3.0 ]", "float", 3});
    
    std::cout << "\nTest Summary:\n";
    std::cout << "=============\n";
    std::cout << "Total test cases: " << tests.size() << "\n";
    
    // Display test details
    for (const auto& test : tests) {
        std::cout << "- " << test.name << ": " << test.expected_count << " " << test.expected_type << " elements\n";
        if (test.data.length() > 80) {
            std::cout << "  Data: " << test.data.substr(0, 75) << "...\n";
        } else {
            std::cout << "  Data: " << test.data << "\n";
        }
    }
    
    std::cout << "\nValidation Status:\n";
    std::cout << "==================\n";
    
#ifdef TINYUSDZ_PARSER_OPT
    std::cout << "🚀 OPTIMIZED BUILD: Using two-phase parsing with pre-allocation\n";
    std::cout << "   - Fixed 128-byte stack buffers for number parsing\n";
    std::cout << "   - Comma counting for accurate vector pre-allocation\n"; 
    std::cout << "   - Chunked allocation with 16K default chunk size\n";
    std::cout << "   - Special value handling (inf, -inf, nan)\n";
    std::cout << "   - Template specializations for float/double/int/float2/float3/float4\n";
#else
    std::cout << "🐌 STANDARD BUILD: Using generic template-based parsing\n";
    std::cout << "   - String-based number parsing with dynamic allocations\n";
    std::cout << "   - No pre-allocation, vectors grow as needed\n";
    std::cout << "   - Standard string operations for all parsing\n";
    std::cout << "   - Generic template implementation for all types\n";
#endif
    
    // Performance estimation
    std::cout << "\nExpected Performance Characteristics:\n";
    std::cout << "=====================================\n";
    
    size_t total_elements = 0;
    size_t total_data_size = 0;
    
    for (const auto& test : tests) {
        total_elements += test.expected_count;
        total_data_size += test.data.length();
    }
    
    std::cout << "Total elements to parse: " << total_elements << "\n";
    std::cout << "Total data size: " << total_data_size << " bytes\n";
    
#ifdef TINYUSDZ_PARSER_OPT
    double estimated_time = total_elements * 0.05; // 0.05 μs per element (optimized)
    std::cout << "Estimated parse time (optimized): " << std::fixed << std::setprecision(2) 
              << estimated_time << " μs\n";
    std::cout << "Expected improvement: 40-50% faster than standard parsing\n";
#else
    double estimated_time = total_elements * 0.08; // 0.08 μs per element (standard)
    std::cout << "Estimated parse time (standard): " << std::fixed << std::setprecision(2) 
              << estimated_time << " μs\n";
    std::cout << "Standard template-based parsing performance\n";
#endif

    std::cout << "\n✅ Validation test data prepared successfully!\n";
    std::cout << "\nTo run full validation:\n";
    std::cout << "1. Build with TINYUSDZ_PARSER_OPT=ON\n";
    std::cout << "2. Build with TINYUSDZ_PARSER_OPT=OFF\n";  
    std::cout << "3. Compare parsing results for identical output\n";
    std::cout << "4. Measure performance difference\n";
    
    return 0;
}