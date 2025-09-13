#pragma once

#include <vector>
#include <array>
#include <cstddef>
#include <cstdint>

// Configuration for optimized float parsing
struct ParseConfig {
    size_t chunk_size = 16384;  // Default 16K items per chunk
    bool enable_special_values = true;  // Support inf, -inf, nan
    bool allow_trailing_comma = true;
};

// Forward declaration
class OptimizedLexer;

// Optimized float array parsing functions
// These can be integrated into TinyUSDZ's ASCII parser

namespace tinyusdz {
namespace parse_fp {

// Parse a float array with optimized two-phase approach
// Input: string data in format "[1.0, 2.0, 3.0, ...]"
// Returns: true on success, false on parse error
bool ParseFloatArrayOptimized(
    const char* begin,
    const char* end,
    std::vector<float>& result,
    const ParseConfig& config = ParseConfig());

// Parse a double array with optimized two-phase approach
bool ParseDoubleArrayOptimized(
    const char* begin,
    const char* end,
    std::vector<double>& result,
    const ParseConfig& config = ParseConfig());

// Parse float2 array with tuple-aware lexing
// Input: string data in format "[(1.0, 2.0), (3.0, 4.0), ...]"
bool ParseFloat2ArrayOptimized(
    const char* begin,
    const char* end,
    std::vector<std::array<float, 2>>& result,
    const ParseConfig& config = ParseConfig());

// Parse float3 array with tuple-aware lexing
// Input: string data in format "[(1.0, 2.0, 3.0), (4.0, 5.0, 6.0), ...]"
bool ParseFloat3ArrayOptimized(
    const char* begin,
    const char* end,
    std::vector<std::array<float, 3>>& result,
    const ParseConfig& config = ParseConfig());

// Parse float4 array with tuple-aware lexing
// Input: string data in format "[(1.0, 2.0, 3.0, 4.0), (5.0, 6.0, 7.0, 8.0), ...]"
bool ParseFloat4ArrayOptimized(
    const char* begin,
    const char* end,
    std::vector<std::array<float, 4>>& result,
    const ParseConfig& config = ParseConfig());

// Matrix parsing functions (for future implementation)
bool ParseMatrix3dArrayOptimized(
    const char* begin,
    const char* end,
    std::vector<std::array<double, 9>>& result,
    const ParseConfig& config = ParseConfig());

bool ParseMatrix4dArrayOptimized(
    const char* begin,
    const char* end,
    std::vector<std::array<double, 16>>& result,
    const ParseConfig& config = ParseConfig());

// Utility function to estimate array size by counting delimiters
// This can be used for pre-allocation in other parsers
size_t EstimateArraySize(
    const char* begin,
    const char* end,
    char delimiter = ',');

size_t EstimateTupleCount(
    const char* begin,
    const char* end,
    char open_paren = '(');

} // namespace parse_fp
} // namespace tinyusdz