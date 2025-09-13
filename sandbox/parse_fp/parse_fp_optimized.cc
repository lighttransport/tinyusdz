#include <vector>
#include <array>
#include <iostream>
#include <sstream>
#include <chrono>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <limits>
#include <random>

#include "fast_float/fast_float.h"

// Configuration for chunked allocation
struct ParseConfig {
    size_t chunk_size = 16384;  // Default 16K items per chunk
    bool enable_special_values = true;  // Support inf, -inf, nan
    bool allow_trailing_comma = true;
};

// Optimized lexer with two-phase approach
class OptimizedLexer {
public:
    OptimizedLexer(const char* begin, const char* end, const ParseConfig& cfg = ParseConfig())
        : p_begin(begin), p_end(end), curr(begin), config(cfg) {}

    // Phase 1: Count elements for pre-allocation
    size_t count_float_elements() {
        const char* p = p_begin;
        size_t count = 0;
        bool in_number = false;
        bool found_bracket = false;
        
        while (p < p_end) {
            char c = *p;
            
            if (c == '[') {
                found_bracket = true;
            } else if (c == ']') {
                if (in_number) count++;
                break;
            } else if (c == ',') {
                if (in_number) {
                    count++;
                    in_number = false;
                }
            } else if ((c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || 
                       c == 'e' || c == 'E' || c == 'i' || c == 'n' || c == 'a' || c == 'f') {
                if (!in_number && found_bracket) {
                    in_number = true;
                }
            }
            p++;
        }
        
        return count;
    }
    
    // Phase 1: Count vector elements (looking for parentheses)
    template<int N>
    size_t count_vector_elements() {
        const char* p = p_begin;
        size_t paren_count = 0;
        bool in_array = false;
        
        while (p < p_end) {
            char c = *p;
            
            if (c == '[') {
                in_array = true;
            } else if (c == ']') {
                break;
            } else if (c == '(' && in_array) {
                paren_count++;
            }
            p++;
        }
        
        return paren_count;
    }
    
    // Optimized float parsing with fixed buffer
    bool parse_float_optimized(float& result) {
        constexpr size_t BUFFER_SIZE = 128;
        char buffer[BUFFER_SIZE];
        size_t buffer_pos = 0;
        
        // Skip whitespace
        while (curr < p_end && is_whitespace(*curr)) {
            curr++;
        }
        
        if (curr >= p_end) return false;
        
        // Check for special values
        if (config.enable_special_values) {
            if (check_special_value("inf", result, std::numeric_limits<float>::infinity())) {
                return true;
            }
            if (check_special_value("-inf", result, -std::numeric_limits<float>::infinity())) {
                return true;
            }
            if (check_special_value("nan", result, std::numeric_limits<float>::quiet_NaN())) {
                return true;
            }
        }
        
        // Collect number characters
        while (curr < p_end && buffer_pos < BUFFER_SIZE - 1) {
            char c = *curr;
            
            if (is_float_char(c)) {
                buffer[buffer_pos++] = c;
                curr++;
            } else {
                break;
            }
        }
        
        if (buffer_pos == 0) return false;
        
        buffer[buffer_pos] = '\0';
        
        // Use fast_float for parsing
        auto answer = fast_float::from_chars(buffer, buffer + buffer_pos, result);
        return answer.ec == std::errc();
    }
    
    bool parse_double_optimized(double& result) {
        constexpr size_t BUFFER_SIZE = 128;
        char buffer[BUFFER_SIZE];
        size_t buffer_pos = 0;
        
        // Skip whitespace
        while (curr < p_end && is_whitespace(*curr)) {
            curr++;
        }
        
        if (curr >= p_end) return false;
        
        // Check for special values
        if (config.enable_special_values) {
            if (check_special_value_double("inf", result, std::numeric_limits<double>::infinity())) {
                return true;
            }
            if (check_special_value_double("-inf", result, -std::numeric_limits<double>::infinity())) {
                return true;
            }
            if (check_special_value_double("nan", result, std::numeric_limits<double>::quiet_NaN())) {
                return true;
            }
        }
        
        // Collect number characters
        while (curr < p_end && buffer_pos < BUFFER_SIZE - 1) {
            char c = *curr;
            
            if (is_float_char(c)) {
                buffer[buffer_pos++] = c;
                curr++;
            } else {
                break;
            }
        }
        
        if (buffer_pos == 0) return false;
        
        buffer[buffer_pos] = '\0';
        
        // Use fast_float for parsing
        auto answer = fast_float::from_chars(buffer, buffer + buffer_pos, result);
        return answer.ec == std::errc();
    }
    
    // Parse float array with two-phase approach
    bool parse_float_array(std::vector<float>& result) {
        // Phase 1: Count elements
        size_t estimated_count = count_float_elements();
        result.reserve(estimated_count);
        
        // Reset to beginning for phase 2
        curr = p_begin;
        
        // Find '['
        if (!seek_char('[')) return false;
        
        // Phase 2: Parse with chunked allocation
        size_t items_parsed = 0;
        
        while (curr < p_end) {
            skip_whitespace();
            
            // Check for end
            if (*curr == ']') {
                curr++;
                return true;
            }
            
            // Skip comma
            if (*curr == ',') {
                curr++;
                skip_whitespace();
                
                // Check for trailing comma
                if (*curr == ']') {
                    if (config.allow_trailing_comma) {
                        curr++;
                        return true;
                    }
                    return false;
                }
            }
            
            // Chunked allocation
            if (items_parsed > 0 && items_parsed % config.chunk_size == 0) {
                if (result.capacity() < estimated_count) {
                    result.reserve(result.capacity() + config.chunk_size);
                }
            }
            
            // Parse float
            float value;
            if (!parse_float_optimized(value)) {
                return false;
            }
            
            result.push_back(value);
            items_parsed++;
        }
        
        return false;
    }
    
    // Parse float2 array with two-phase approach
    bool parse_float2_array(std::vector<std::array<float, 2>>& result) {
        // Phase 1: Count tuples
        size_t estimated_count = count_vector_elements<2>();
        result.reserve(estimated_count);
        
        // Reset for phase 2
        curr = p_begin;
        
        // Find '['
        if (!seek_char('[')) return false;
        
        // Phase 2: Parse tuples
        size_t items_parsed = 0;
        
        while (curr < p_end) {
            skip_whitespace();
            
            // Check for end
            if (*curr == ']') {
                curr++;
                return true;
            }
            
            // Skip comma between tuples
            if (*curr == ',') {
                curr++;
                skip_whitespace();
                
                if (*curr == ']') {
                    if (config.allow_trailing_comma) {
                        curr++;
                        return true;
                    }
                    return false;
                }
            }
            
            // Chunked allocation
            if (items_parsed > 0 && items_parsed % config.chunk_size == 0) {
                if (result.capacity() < estimated_count) {
                    result.reserve(result.capacity() + config.chunk_size);
                }
            }
            
            // Parse tuple
            std::array<float, 2> tuple;
            if (!parse_float2_tuple(tuple)) {
                return false;
            }
            
            result.push_back(tuple);
            items_parsed++;
        }
        
        return false;
    }
    
    // Parse float3 array with two-phase approach
    bool parse_float3_array(std::vector<std::array<float, 3>>& result) {
        // Phase 1: Count tuples
        size_t estimated_count = count_vector_elements<3>();
        result.reserve(estimated_count);
        
        // Reset for phase 2
        curr = p_begin;
        
        // Find '['
        if (!seek_char('[')) return false;
        
        // Phase 2: Parse tuples
        size_t items_parsed = 0;
        
        while (curr < p_end) {
            skip_whitespace();
            
            // Check for end
            if (*curr == ']') {
                curr++;
                return true;
            }
            
            // Skip comma between tuples
            if (*curr == ',') {
                curr++;
                skip_whitespace();
                
                if (*curr == ']') {
                    if (config.allow_trailing_comma) {
                        curr++;
                        return true;
                    }
                    return false;
                }
            }
            
            // Chunked allocation
            if (items_parsed > 0 && items_parsed % config.chunk_size == 0) {
                if (result.capacity() < estimated_count) {
                    result.reserve(result.capacity() + config.chunk_size);
                }
            }
            
            // Parse tuple
            std::array<float, 3> tuple;
            if (!parse_float3_tuple(tuple)) {
                return false;
            }
            
            result.push_back(tuple);
            items_parsed++;
        }
        
        return false;
    }
    
    // Parse float4 array with two-phase approach
    bool parse_float4_array(std::vector<std::array<float, 4>>& result) {
        // Phase 1: Count tuples
        size_t estimated_count = count_vector_elements<4>();
        result.reserve(estimated_count);
        
        // Reset for phase 2
        curr = p_begin;
        
        // Find '['
        if (!seek_char('[')) return false;
        
        // Phase 2: Parse tuples
        size_t items_parsed = 0;
        
        while (curr < p_end) {
            skip_whitespace();
            
            // Check for end
            if (*curr == ']') {
                curr++;
                return true;
            }
            
            // Skip comma between tuples
            if (*curr == ',') {
                curr++;
                skip_whitespace();
                
                if (*curr == ']') {
                    if (config.allow_trailing_comma) {
                        curr++;
                        return true;
                    }
                    return false;
                }
            }
            
            // Chunked allocation
            if (items_parsed > 0 && items_parsed % config.chunk_size == 0) {
                if (result.capacity() < estimated_count) {
                    result.reserve(result.capacity() + config.chunk_size);
                }
            }
            
            // Parse tuple
            std::array<float, 4> tuple;
            if (!parse_float4_tuple(tuple)) {
                return false;
            }
            
            result.push_back(tuple);
            items_parsed++;
        }
        
        return false;
    }

private:
    bool is_whitespace(char c) const {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    }
    
    bool is_float_char(char c) const {
        return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E';
    }
    
    void skip_whitespace() {
        while (curr < p_end && is_whitespace(*curr)) {
            curr++;
        }
    }
    
    bool seek_char(char target) {
        while (curr < p_end) {
            if (*curr == target) {
                curr++;
                return true;
            }
            curr++;
        }
        return false;
    }
    
    bool check_special_value(const char* str, float& result, float value) {
        size_t len = strlen(str);
        if (curr + len <= p_end && strncmp(curr, str, len) == 0) {
            result = value;
            curr += len;
            return true;
        }
        return false;
    }
    
    bool check_special_value_double(const char* str, double& result, double value) {
        size_t len = strlen(str);
        if (curr + len <= p_end && strncmp(curr, str, len) == 0) {
            result = value;
            curr += len;
            return true;
        }
        return false;
    }
    
    bool parse_float2_tuple(std::array<float, 2>& tuple) {
        skip_whitespace();
        if (curr >= p_end || *curr != '(') return false;
        curr++;
        
        for (int i = 0; i < 2; i++) {
            skip_whitespace();
            if (!parse_float_optimized(tuple[i])) return false;
            skip_whitespace();
            
            if (i < 1) {
                if (curr >= p_end || *curr != ',') return false;
                curr++;
            }
        }
        
        skip_whitespace();
        if (curr >= p_end || *curr != ')') return false;
        curr++;
        
        return true;
    }
    
    bool parse_float3_tuple(std::array<float, 3>& tuple) {
        skip_whitespace();
        if (curr >= p_end || *curr != '(') return false;
        curr++;
        
        for (int i = 0; i < 3; i++) {
            skip_whitespace();
            if (!parse_float_optimized(tuple[i])) return false;
            skip_whitespace();
            
            if (i < 2) {
                if (curr >= p_end || *curr != ',') return false;
                curr++;
            }
        }
        
        skip_whitespace();
        if (curr >= p_end || *curr != ')') return false;
        curr++;
        
        return true;
    }
    
    bool parse_float4_tuple(std::array<float, 4>& tuple) {
        skip_whitespace();
        if (curr >= p_end || *curr != '(') return false;
        curr++;
        
        for (int i = 0; i < 4; i++) {
            skip_whitespace();
            if (!parse_float_optimized(tuple[i])) return false;
            skip_whitespace();
            
            if (i < 3) {
                if (curr >= p_end || *curr != ',') return false;
                curr++;
            }
        }
        
        skip_whitespace();
        if (curr >= p_end || *curr != ')') return false;
        curr++;
        
        return true;
    }
    
    const char* p_begin;
    const char* p_end;
    const char* curr;
    ParseConfig config;
};

// Test data generation functions (same as original)
std::string gen_floatarray(size_t n, bool delim_at_end) {
    std::stringstream ss;
    std::random_device rd;
    std::mt19937 engine(rd());
    std::uniform_real_distribution<> dist(-10000.0, 10000.0);

    ss << "[";
    for (size_t i = 0; i < n; i++) {
        double f = dist(engine);
        ss << std::to_string(f);
        if (delim_at_end) {
            ss << ",";
        } else if (i < (n-1)) {
            ss << ",";
        }
    }
    ss << "]";

    return ss.str();
}

std::string gen_float2array(size_t n, bool delim_at_end) {
    std::stringstream ss;
    std::random_device rd;
    std::mt19937 engine(rd());
    std::uniform_real_distribution<> dist(-10000.0, 10000.0);

    ss << "[";
    for (size_t i = 0; i < n; i++) {
        double f1 = dist(engine);
        double f2 = dist(engine);
        ss << "(" << std::to_string(f1) << ", " << std::to_string(f2) << ")";
        if (delim_at_end) {
            ss << ",";
        } else if (i < (n-1)) {
            ss << ",";
        }
    }
    ss << "]";

    return ss.str();
}

std::string gen_float3array(size_t n, bool delim_at_end) {
    std::stringstream ss;
    std::random_device rd;
    std::mt19937 engine(rd());
    std::uniform_real_distribution<> dist(-10000.0, 10000.0);

    ss << "[";
    for (size_t i = 0; i < n; i++) {
        double f1 = dist(engine);
        double f2 = dist(engine);
        double f3 = dist(engine);
        ss << "(" << std::to_string(f1) << ", " << std::to_string(f2) << ", " << std::to_string(f3) << ")";
        if (delim_at_end) {
            ss << ",";
        } else if (i < (n-1)) {
            ss << ",";
        }
    }
    ss << "]";

    return ss.str();
}

std::string gen_float4array(size_t n, bool delim_at_end) {
    std::stringstream ss;
    std::random_device rd;
    std::mt19937 engine(rd());
    std::uniform_real_distribution<> dist(-10000.0, 10000.0);

    ss << "[";
    for (size_t i = 0; i < n; i++) {
        double f1 = dist(engine);
        double f2 = dist(engine);
        double f3 = dist(engine);
        double f4 = dist(engine);
        ss << "(" << std::to_string(f1) << ", " << std::to_string(f2) << ", " << std::to_string(f3) << ", " << std::to_string(f4) << ")";
        if (delim_at_end) {
            ss << ",";
        } else if (i < (n-1)) {
            ss << ",";
        }
    }
    ss << "]";

    return ss.str();
}

// Test special values
std::string gen_special_floatarray() {
    return "[1.0, -2.5, inf, -inf, nan, 3.14e-5, 2.3e4]";
}

int main(int argc, char **argv) {
    size_t n = 1024*16;
    bool delim_at_end = true;
    int test_type = 0;  // 0: float, 1: float2, 2: float3, 3: float4, 4: special values
    size_t chunk_size = 16384;
    
    if (argc > 1) {
        n = std::stoi(argv[1]);
    }
    if (argc > 2) {
        delim_at_end = std::stoi(argv[2]) > 0;
    }
    if (argc > 3) {
        test_type = std::stoi(argv[3]);
    }
    if (argc > 4) {
        chunk_size = std::stoi(argv[4]);
    }

    ParseConfig config;
    config.chunk_size = chunk_size;
    config.allow_trailing_comma = delim_at_end;
    config.enable_special_values = true;

    std::cout << "Testing ";
    switch (test_type) {
        case 0: std::cout << "float array"; break;
        case 1: std::cout << "float2 array"; break;
        case 2: std::cout << "float3 array"; break;
        case 3: std::cout << "float4 array"; break;
        case 4: std::cout << "special values"; break;
        default: std::cout << "float array (default)"; test_type = 0; break;
    }
    std::cout << " with " << n << " elements\n";
    std::cout << "Chunk size: " << chunk_size << "\n";
    std::cout << "Allow trailing comma: " << (delim_at_end ? "yes" : "no") << "\n";

    if (test_type == 0) {
        // Test float array
        std::string input = gen_floatarray(n, delim_at_end);
        std::cout << "Input size: " << input.size() << " bytes\n";
        
        auto start = std::chrono::steady_clock::now();
        
        OptimizedLexer lexer(input.c_str(), input.c_str() + input.size(), config);
        std::vector<float> result;
        
        if (!lexer.parse_float_array(result)) {
            std::cerr << "Parse error\n";
            return -1;
        }
        
        auto end = std::chrono::steady_clock::now();
        
        std::cout << "Parsed " << result.size() << " floats\n";
        std::cout << "Parse time: " << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << " µs\n";
        
        // Verify a few values
        if (result.size() > 0) {
            std::cout << "First value: " << result[0] << "\n";
            std::cout << "Last value: " << result[result.size()-1] << "\n";
        }
        
    } else if (test_type == 1) {
        // Test float2 array
        std::string input = gen_float2array(n, delim_at_end);
        std::cout << "Input size: " << input.size() << " bytes\n";
        
        auto start = std::chrono::steady_clock::now();
        
        OptimizedLexer lexer(input.c_str(), input.c_str() + input.size(), config);
        std::vector<std::array<float, 2>> result;
        
        if (!lexer.parse_float2_array(result)) {
            std::cerr << "Parse error\n";
            return -1;
        }
        
        auto end = std::chrono::steady_clock::now();
        
        std::cout << "Parsed " << result.size() << " float2 tuples\n";
        std::cout << "Parse time: " << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << " µs\n";
        
        // Verify a few values
        if (result.size() > 0) {
            std::cout << "First tuple: (" << result[0][0] << ", " << result[0][1] << ")\n";
            std::cout << "Last tuple: (" << result.back()[0] << ", " << result.back()[1] << ")\n";
        }
        
    } else if (test_type == 2) {
        // Test float3 array
        std::string input = gen_float3array(n, delim_at_end);
        std::cout << "Input size: " << input.size() << " bytes\n";
        
        auto start = std::chrono::steady_clock::now();
        
        OptimizedLexer lexer(input.c_str(), input.c_str() + input.size(), config);
        std::vector<std::array<float, 3>> result;
        
        if (!lexer.parse_float3_array(result)) {
            std::cerr << "Parse error\n";
            return -1;
        }
        
        auto end = std::chrono::steady_clock::now();
        
        std::cout << "Parsed " << result.size() << " float3 tuples\n";
        std::cout << "Parse time: " << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << " µs\n";
        
        // Verify a few values
        if (result.size() > 0) {
            std::cout << "First tuple: (" << result[0][0] << ", " << result[0][1] << ", " << result[0][2] << ")\n";
            std::cout << "Last tuple: (" << result.back()[0] << ", " << result.back()[1] << ", " << result.back()[2] << ")\n";
        }
        
    } else if (test_type == 3) {
        // Test float4 array
        std::string input = gen_float4array(n, delim_at_end);
        std::cout << "Input size: " << input.size() << " bytes\n";
        
        auto start = std::chrono::steady_clock::now();
        
        OptimizedLexer lexer(input.c_str(), input.c_str() + input.size(), config);
        std::vector<std::array<float, 4>> result;
        
        if (!lexer.parse_float4_array(result)) {
            std::cerr << "Parse error\n";
            return -1;
        }
        
        auto end = std::chrono::steady_clock::now();
        
        std::cout << "Parsed " << result.size() << " float4 tuples\n";
        std::cout << "Parse time: " << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() << " µs\n";
        
        // Verify a few values
        if (result.size() > 0) {
            std::cout << "First tuple: (" << result[0][0] << ", " << result[0][1] << ", " << result[0][2] << ", " << result[0][3] << ")\n";
            std::cout << "Last tuple: (" << result.back()[0] << ", " << result.back()[1] << ", " << result.back()[2] << ", " << result.back()[3] << ")\n";
        }
        
    } else if (test_type == 4) {
        // Test special values
        std::string input = gen_special_floatarray();
        std::cout << "Input: " << input << "\n";
        
        OptimizedLexer lexer(input.c_str(), input.c_str() + input.size(), config);
        std::vector<float> result;
        
        if (!lexer.parse_float_array(result)) {
            std::cerr << "Parse error\n";
            return -1;
        }
        
        std::cout << "Parsed " << result.size() << " values:\n";
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
    }

    return 0;
}