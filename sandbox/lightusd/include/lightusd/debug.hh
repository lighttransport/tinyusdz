// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Debug and logging utilities
//
// Build configurations:
// - LIGHTUSD_PRODUCTION_BUILD: Disable all debug output, minimize file paths
// - LIGHTUSD_DEBUG_PRINT: Enable DCOUT debug macro (requires non-production)
//
// Runtime control (Debug):
// - lightusd::set_debug_output_enabled(bool) to toggle DCOUT at runtime
// - LIGHTUSD_ENABLE_DCOUT environment variable (checked at startup)
//
// Runtime control (Profiling):
// - lightusd::set_profiling_enabled(bool) to toggle profiling at runtime
// - LIGHTUSD_ENABLE_PROFILING environment variable (checked at startup)
// - Profiling works in both debug and production builds

#pragma once

#include <sstream>
#include <iostream>
#include <string>
#include <chrono>
#include <vector>
#include <map>
#include <mutex>

namespace lightusd {
namespace v1 {

// Global flag to control DCOUT output at runtime.
// Can be set via set_debug_output_enabled() or LIGHTUSD_ENABLE_DCOUT env var.
extern bool g_debug_output_enabled;

/// Enable or disable debug output (DCOUT) at runtime.
void set_debug_output_enabled(bool enabled);

/// Check if debug output is currently enabled.
bool is_debug_output_enabled();

/// Initialize debug settings from environment variables.
/// Called automatically at startup, but can be called again to re-read env.
void init_debug_settings();

// ============================================================================
// Profiling API
// ============================================================================

/// Global flag to control profiling at runtime.
/// Can be set via set_profiling_enabled() or LIGHTUSD_ENABLE_PROFILING env var.
extern bool g_profiling_enabled;

/// Enable or disable profiling at runtime.
void set_profiling_enabled(bool enabled);

/// Check if profiling is currently enabled.
bool is_profiling_enabled();

/// Profile entry - stores timing data for a single measurement
struct ProfileEntry {
    const char* name;           // Name of the profiled section
    const char* file;           // Source file
    const char* func;           // Function name
    int line;                   // Line number
    int64_t duration_us;        // Duration in microseconds
    int64_t timestamp_us;       // Timestamp when measurement started

    ProfileEntry()
        : name(nullptr), file(nullptr), func(nullptr)
        , line(0), duration_us(0), timestamp_us(0) {}
};

/// Profile statistics for aggregated data
struct ProfileStats {
    std::string name;
    int64_t total_us = 0;       // Total time in microseconds
    int64_t min_us = 0;         // Minimum time
    int64_t max_us = 0;         // Maximum time
    int64_t count = 0;          // Number of calls

    double avg_us() const { return count > 0 ? static_cast<double>(total_us) / count : 0.0; }
    double avg_ms() const { return avg_us() / 1000.0; }
    double total_ms() const { return total_us / 1000.0; }
};

/// Profiler - collects and reports timing data
/// Thread-safe singleton for collecting profile data across the application.
class Profiler {
public:
    /// Get singleton instance
    static Profiler& instance();

    /// Record a profile entry
    void record(const ProfileEntry& entry);

    /// Clear all recorded data
    void clear();

    /// Get number of recorded entries
    size_t entry_count() const;

    /// Get all entries (for custom processing)
    std::vector<ProfileEntry> entries() const;

    /// Get aggregated statistics by name
    std::map<std::string, ProfileStats> statistics() const;

    /// Print summary to stdout
    void print_summary() const;

    /// Print detailed report to stdout
    void print_report() const;

    /// Export to JSON string
    std::string to_json() const;

    /// Set maximum number of entries to keep (0 = unlimited)
    void set_max_entries(size_t max_entries);

private:
    Profiler();
    ~Profiler();
    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;

    struct Impl;
    Impl* impl_;
};

/// ScopedProfiler - RAII timer that records to the global Profiler
/// Works in both debug and production builds when profiling is enabled.
class ScopedProfiler {
public:
    ScopedProfiler(const char* name, const char* file, const char* func, int line);
    ~ScopedProfiler();

    /// Get elapsed time so far (in microseconds)
    int64_t elapsed_us() const;

private:
    const char* name_;
    const char* file_;
    const char* func_;
    int line_;
    std::chrono::time_point<std::chrono::high_resolution_clock> start_;
};

// ============================================================================
// Memory Tracking API
// ============================================================================

/// Global flag to control memory tracking at runtime.
/// Can be set via set_memory_tracking_enabled() or LIGHTUSD_ENABLE_MEMORY_TRACKING env var.
extern bool g_memory_tracking_enabled;

/// Enable or disable memory tracking at runtime.
void set_memory_tracking_enabled(bool enabled);

/// Check if memory tracking is currently enabled.
bool is_memory_tracking_enabled();

/// Memory allocation entry
struct MemoryEntry {
    const char* tag;            // Tag/category for this allocation
    const char* file;           // Source file
    const char* func;           // Function name
    int line;                   // Line number
    size_t size;                // Allocation size in bytes
    void* ptr;                  // Pointer (for tracking alloc/free pairs)
    int64_t timestamp_us;       // When allocation occurred
    bool is_allocation;         // true = alloc, false = free

    MemoryEntry()
        : tag(nullptr), file(nullptr), func(nullptr)
        , line(0), size(0), ptr(nullptr), timestamp_us(0), is_allocation(true) {}
};

/// Memory statistics for a tag/category
struct MemoryStats {
    std::string tag;
    size_t current_bytes = 0;       // Currently allocated
    size_t peak_bytes = 0;          // Peak allocation
    size_t total_allocated = 0;     // Total bytes allocated over time
    size_t total_freed = 0;         // Total bytes freed over time
    size_t alloc_count = 0;         // Number of allocations
    size_t free_count = 0;          // Number of frees

    double current_mb() const { return current_bytes / (1024.0 * 1024.0); }
    double peak_mb() const { return peak_bytes / (1024.0 * 1024.0); }
};

/// Memory limit exceeded callback type
/// Return true to allow the allocation anyway, false to deny
using MemoryLimitCallback = bool(*)(size_t requested_bytes, size_t current_bytes, size_t limit_bytes);

/// MemoryTracker - tracks memory allocations and enforces limits
/// Thread-safe singleton for memory management across the application.
class MemoryTracker {
public:
    /// Get singleton instance
    static MemoryTracker& instance();

    /// Record an allocation
    void record_alloc(const char* tag, size_t size, void* ptr,
                      const char* file, const char* func, int line);

    /// Record a deallocation
    void record_free(const char* tag, size_t size, void* ptr,
                     const char* file, const char* func, int line);

    /// Record allocation by pointer (looks up size from previous alloc)
    void record_free(void* ptr);

    /// Check if allocation would exceed limit (doesn't record)
    /// Returns true if allocation is allowed
    bool check_limit(size_t size) const;

    /// Try to allocate - checks limit and records if allowed
    /// Returns true if allocation is allowed and recorded
    bool try_alloc(const char* tag, size_t size, void* ptr,
                   const char* file, const char* func, int line);

    // ========== Limits ==========

    /// Set global memory limit (0 = unlimited)
    void set_limit(size_t limit_bytes);

    /// Set limit in megabytes (convenience)
    void set_limit_mb(size_t limit_mb);

    /// Get current limit (0 = unlimited)
    size_t limit() const;

    /// Set callback for when limit is exceeded
    void set_limit_callback(MemoryLimitCallback callback);

    // ========== Queries ==========

    /// Get current total allocated bytes (across all tags)
    size_t current_bytes() const;

    /// Get peak allocated bytes
    size_t peak_bytes() const;

    /// Get current bytes for a specific tag
    size_t current_bytes(const char* tag) const;

    /// Get number of active allocations
    size_t active_alloc_count() const;

    /// Check if limit is exceeded
    bool is_limit_exceeded() const;

    /// Get available bytes before limit (returns SIZE_MAX if no limit)
    size_t available_bytes() const;

    // ========== Statistics ==========

    /// Get statistics for all tags
    std::map<std::string, MemoryStats> statistics() const;

    /// Get overall statistics (all tags combined)
    MemoryStats total_statistics() const;

    /// Get all entries (for debugging)
    std::vector<MemoryEntry> entries() const;

    // ========== Output ==========

    /// Print summary to stdout
    void print_summary() const;

    /// Print detailed report to stdout
    void print_report() const;

    /// Export to JSON string
    std::string to_json() const;

    // ========== Management ==========

    /// Clear all tracking data (does not affect actual memory)
    void clear();

    /// Set maximum number of entries to keep (0 = unlimited)
    void set_max_entries(size_t max_entries);

    /// Reset peak tracking
    void reset_peak();

private:
    MemoryTracker();
    ~MemoryTracker();
    MemoryTracker(const MemoryTracker&) = delete;
    MemoryTracker& operator=(const MemoryTracker&) = delete;

    struct Impl;
    Impl* impl_;
};

/// ScopedMemory - RAII helper for tracking a memory region
/// Automatically records alloc on construction, free on destruction.
class ScopedMemory {
public:
    ScopedMemory(const char* tag, size_t size,
                 const char* file, const char* func, int line);
    ~ScopedMemory();

    /// Get the tracked size
    size_t size() const { return size_; }

    /// Check if tracking is active
    bool is_tracked() const { return tracked_; }

private:
    const char* tag_;
    size_t size_;
    void* ptr_;
    const char* file_;
    const char* func_;
    int line_;
    bool tracked_;
};

/// Log levels for structured logging
enum class LogLevel {
    Debug,    // Detailed debug information (controlled by DCOUT)
    Info,     // General information
    Warning,  // Warning messages
    Error     // Error messages
};

/// Log a message at the specified level.
/// In production builds, Debug level messages are always suppressed.
void log_message(LogLevel level, const char* file, const char* func,
                 int line, const std::string& message);

} // namespace v1
} // namespace lightusd

// ============================================================================
// Debug Macros
// ============================================================================

// Enable local debug print if:
// 1. Not a production build
// 2. Not a fuzzer build
// 3. LIGHTUSD_DEBUG_PRINT is defined
#if !defined(LIGHTUSD_PRODUCTION_BUILD) && !defined(LIGHTUSD_FUZZER_BUILD)
#if defined(LIGHTUSD_DEBUG_PRINT)
#define LIGHTUSD_LOCAL_DEBUG_PRINT
#endif
#endif

// ============================================================================
// DCOUT - Debug Console Output
// ============================================================================
// Usage: DCOUT("value = " << value << ", count = " << count);
// Output: file.cc:function():42 value = 123, count = 5

#if defined(LIGHTUSD_LOCAL_DEBUG_PRINT)

#define DCOUT(x)                                                    \
    do {                                                            \
        if (lightusd::v1::g_debug_output_enabled) {                 \
            std::ostringstream dcout_ss_;                           \
            dcout_ss_ << x;                                         \
            std::cout << __FILE__ << ":" << __func__ << ":"         \
                      << __LINE__ << " "                            \
                      << dcout_ss_.str() << "\n";                   \
        }                                                           \
    } while (false)

#else

#define DCOUT(x) do {} while (false)

#endif

// ============================================================================
// Error/Warning Macros with Return
// ============================================================================

#if defined(LIGHTUSD_PRODUCTION_BUILD)

// Production build: Don't include full file path for privacy
#define PUSH_ERROR_AND_RETURN(err_collector, msg)                   \
    do {                                                            \
        std::ostringstream ss_err_;                                 \
        ss_err_ << __func__ << "():" << __LINE__ << " " << msg;     \
        (err_collector).push_back(ss_err_.str());                   \
        return false;                                               \
    } while (false)

#define PUSH_ERROR_AND_RETURN_TAG(tag, err_collector, msg)          \
    do {                                                            \
        std::ostringstream ss_err_;                                 \
        ss_err_ << "[" << tag << "] "                               \
                << __func__ << "():" << __LINE__ << " " << msg;     \
        (err_collector).push_back(ss_err_.str());                   \
        return false;                                               \
    } while (false)

#define PUSH_WARN(warn_collector, msg)                              \
    do {                                                            \
        std::ostringstream ss_warn_;                                \
        ss_warn_ << __func__ << "():" << __LINE__ << " " << msg;    \
        (warn_collector).push_back(ss_warn_.str());                 \
    } while (false)

#else  // !LIGHTUSD_PRODUCTION_BUILD

// Debug build: Include full file path
#define PUSH_ERROR_AND_RETURN(err_collector, msg)                   \
    do {                                                            \
        std::ostringstream ss_err_;                                 \
        ss_err_ << __FILE__ << ":" << __func__ << "():"             \
                << __LINE__ << " " << msg;                          \
        (err_collector).push_back(ss_err_.str());                   \
        return false;                                               \
    } while (false)

#define PUSH_ERROR_AND_RETURN_TAG(tag, err_collector, msg)          \
    do {                                                            \
        std::ostringstream ss_err_;                                 \
        ss_err_ << "[" << tag << "] " << __FILE__ << ":"            \
                << __func__ << "():" << __LINE__ << " " << msg;     \
        (err_collector).push_back(ss_err_.str());                   \
        return false;                                               \
    } while (false)

#define PUSH_WARN(warn_collector, msg)                              \
    do {                                                            \
        std::ostringstream ss_warn_;                                \
        ss_warn_ << __FILE__ << ":" << __func__ << "():"            \
                 << __LINE__ << " " << msg;                         \
        (warn_collector).push_back(ss_warn_.str());                 \
    } while (false)

#endif  // LIGHTUSD_PRODUCTION_BUILD

// ============================================================================
// Conditional Debug Helpers
// ============================================================================

// Execute code block only in debug mode
#if defined(LIGHTUSD_LOCAL_DEBUG_PRINT)
#define DEBUG_ONLY(x) x
#else
#define DEBUG_ONLY(x)
#endif

// Assert with debug output
#if defined(LIGHTUSD_LOCAL_DEBUG_PRINT)
#define DASSERT(cond, msg)                                          \
    do {                                                            \
        if (!(cond)) {                                              \
            DCOUT("ASSERTION FAILED: " << #cond << " - " << msg);   \
        }                                                           \
    } while (false)
#else
#define DASSERT(cond, msg) do {} while (false)
#endif

// ============================================================================
// Profiling Macros
// ============================================================================

// Helper to create unique variable names
#define LIGHTUSD_CONCAT_IMPL(a, b) a##b
#define LIGHTUSD_CONCAT(a, b) LIGHTUSD_CONCAT_IMPL(a, b)

/// PROFILE_SCOPE(name) - Profile a scope with a custom name
/// Records timing data to the global Profiler when profiling is enabled.
/// Works in both debug and production builds.
/// Example:
///   void process() {
///       PROFILE_SCOPE("process_data");
///       // ... code to profile ...
///   }
#define PROFILE_SCOPE(name) \
    lightusd::v1::ScopedProfiler LIGHTUSD_CONCAT(profiler_, __LINE__) \
        (name, __FILE__, __func__, __LINE__)

/// PROFILE_FUNCTION() - Profile the current function
/// Uses the function name as the profile name.
/// Example:
///   void expensive_operation() {
///       PROFILE_FUNCTION();
///       // ... code to profile ...
///   }
#define PROFILE_FUNCTION() \
    lightusd::v1::ScopedProfiler LIGHTUSD_CONCAT(profiler_, __LINE__) \
        (__func__, __FILE__, __func__, __LINE__)

/// Legacy SCOPED_TIMER macro - now uses the profiling system
/// For backward compatibility and debug-only timing output.
#if defined(LIGHTUSD_LOCAL_DEBUG_PRINT)

#define SCOPED_TIMER(name) \
    lightusd::v1::ScopedProfiler LIGHTUSD_CONCAT(timer_, __LINE__) \
        (name, __FILE__, __func__, __LINE__)

#else

#define SCOPED_TIMER(name) do {} while (false)

#endif

/// PROFILE_BEGIN/END for manual timing sections
/// Use when you need to profile a section that doesn't match scope boundaries.
/// Example:
///   PROFILE_BEGIN(section_name);
///   // ... code ...
///   PROFILE_END(section_name);
#define PROFILE_BEGIN(name) \
    auto LIGHTUSD_CONCAT(profile_start_, name) = \
        std::chrono::high_resolution_clock::now()

#define PROFILE_END(name) \
    do { \
        if (lightusd::v1::g_profiling_enabled) { \
            auto end = std::chrono::high_resolution_clock::now(); \
            lightusd::v1::ProfileEntry entry; \
            entry.name = #name; \
            entry.file = __FILE__; \
            entry.func = __func__; \
            entry.line = __LINE__; \
            entry.duration_us = std::chrono::duration_cast<std::chrono::microseconds>( \
                end - LIGHTUSD_CONCAT(profile_start_, name)).count(); \
            entry.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>( \
                LIGHTUSD_CONCAT(profile_start_, name).time_since_epoch()).count(); \
            lightusd::v1::Profiler::instance().record(entry); \
        } \
    } while (false)

// ============================================================================
// Memory Tracking Macros
// ============================================================================

/// TRACK_ALLOC(tag, size, ptr) - Record a memory allocation
/// Example:
///   void* p = malloc(1024);
///   TRACK_ALLOC("buffers", 1024, p);
#define TRACK_ALLOC(tag, size, ptr) \
    do { \
        if (lightusd::v1::g_memory_tracking_enabled) { \
            lightusd::v1::MemoryTracker::instance().record_alloc( \
                tag, size, ptr, __FILE__, __func__, __LINE__); \
        } \
    } while (false)

/// TRACK_FREE(tag, size, ptr) - Record a memory deallocation
/// Example:
///   TRACK_FREE("buffers", 1024, p);
///   free(p);
#define TRACK_FREE(tag, size, ptr) \
    do { \
        if (lightusd::v1::g_memory_tracking_enabled) { \
            lightusd::v1::MemoryTracker::instance().record_free( \
                tag, size, ptr, __FILE__, __func__, __LINE__); \
        } \
    } while (false)

/// TRACK_FREE_PTR(ptr) - Record deallocation by pointer (looks up size)
/// Use when size is not readily available
#define TRACK_FREE_PTR(ptr) \
    do { \
        if (lightusd::v1::g_memory_tracking_enabled) { \
            lightusd::v1::MemoryTracker::instance().record_free(ptr); \
        } \
    } while (false)

/// CHECK_MEMORY_LIMIT(size) - Check if allocation would exceed limit
/// Returns true if allocation is allowed
#define CHECK_MEMORY_LIMIT(size) \
    lightusd::v1::MemoryTracker::instance().check_limit(size)

/// TRACK_SCOPE_MEMORY(tag, size) - RAII memory tracking for a scope
/// Tracks allocation on entry, deallocation on scope exit
/// Example:
///   {
///       TRACK_SCOPE_MEMORY("temp_buffer", 4096);
///       // ... use memory ...
///   } // automatically tracked as freed
#define TRACK_SCOPE_MEMORY(tag, size) \
    lightusd::v1::ScopedMemory LIGHTUSD_CONCAT(scoped_mem_, __LINE__) \
        (tag, size, __FILE__, __func__, __LINE__)

/// Convenience macro to set memory limit in MB
#define SET_MEMORY_LIMIT_MB(mb) \
    lightusd::v1::MemoryTracker::instance().set_limit_mb(mb)
