// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Debug and logging implementation

#include "lightusd/debug.hh"
#include <cstdlib>
#include <algorithm>
#include <iomanip>

namespace lightusd {
namespace v1 {

// Global debug output flag - defaults to false
bool g_debug_output_enabled = false;

// Global profiling flag - defaults to false
bool g_profiling_enabled = false;

// Global memory tracking flag - defaults to false
bool g_memory_tracking_enabled = false;

// Helper to parse bool from environment variable
static bool parse_env_bool(const char* env_value, bool default_value) {
    if (!env_value || env_value[0] == '\0') {
        return default_value;
    }
    char c = env_value[0];
    if (c == '1' || c == 't' || c == 'T' || c == 'y' || c == 'Y') {
        return true;
    } else if (c == '0' || c == 'f' || c == 'F' || c == 'n' || c == 'N') {
        return false;
    }
    return default_value;
}

// Static initializer to check environment variables at startup
namespace {

struct DebugInitializer {
    DebugInitializer() {
        init_debug_settings();
    }
};

// This will be constructed before main() is called
static DebugInitializer g_debug_initializer;

} // anonymous namespace

void set_debug_output_enabled(bool enabled) {
    g_debug_output_enabled = enabled;
}

bool is_debug_output_enabled() {
    return g_debug_output_enabled;
}

void set_profiling_enabled(bool enabled) {
    g_profiling_enabled = enabled;
}

bool is_profiling_enabled() {
    return g_profiling_enabled;
}

void set_memory_tracking_enabled(bool enabled) {
    g_memory_tracking_enabled = enabled;
}

bool is_memory_tracking_enabled() {
    return g_memory_tracking_enabled;
}

void init_debug_settings() {
    // Check LIGHTUSD_ENABLE_DCOUT environment variable
    const char* env_dcout = std::getenv("LIGHTUSD_ENABLE_DCOUT");
    g_debug_output_enabled = parse_env_bool(env_dcout, false);

    // Check LIGHTUSD_ENABLE_PROFILING environment variable
    const char* env_profiling = std::getenv("LIGHTUSD_ENABLE_PROFILING");
    g_profiling_enabled = parse_env_bool(env_profiling, false);

    // Check LIGHTUSD_ENABLE_MEMORY_TRACKING environment variable
    const char* env_memory = std::getenv("LIGHTUSD_ENABLE_MEMORY_TRACKING");
    g_memory_tracking_enabled = parse_env_bool(env_memory, false);

    // Check LIGHTUSD_MEMORY_LIMIT_MB environment variable
    const char* env_limit = std::getenv("LIGHTUSD_MEMORY_LIMIT_MB");
    if (env_limit) {
        int limit_mb = std::atoi(env_limit);
        if (limit_mb > 0) {
            MemoryTracker::instance().set_limit_mb(static_cast<size_t>(limit_mb));
        }
    }
}

// ============================================================================
// Profiler Implementation
// ============================================================================

struct Profiler::Impl {
    mutable std::mutex mutex_;
    std::vector<ProfileEntry> entries_;
    size_t max_entries_ = 0;  // 0 = unlimited

    Impl() = default;
};

Profiler::Profiler() : impl_(new Impl()) {}

Profiler::~Profiler() {
    delete impl_;
}

Profiler& Profiler::instance() {
    static Profiler instance;
    return instance;
}

void Profiler::record(const ProfileEntry& entry) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    // Check max entries limit
    if (impl_->max_entries_ > 0 && impl_->entries_.size() >= impl_->max_entries_) {
        // Remove oldest entry (FIFO)
        impl_->entries_.erase(impl_->entries_.begin());
    }

    impl_->entries_.push_back(entry);
}

void Profiler::clear() {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    impl_->entries_.clear();
}

size_t Profiler::entry_count() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->entries_.size();
}

std::vector<ProfileEntry> Profiler::entries() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->entries_;
}

std::map<std::string, ProfileStats> Profiler::statistics() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    std::map<std::string, ProfileStats> stats;

    for (const auto& entry : impl_->entries_) {
        if (!entry.name) continue;

        std::string name(entry.name);
        auto it = stats.find(name);

        if (it == stats.end()) {
            ProfileStats s;
            s.name = name;
            s.total_us = entry.duration_us;
            s.min_us = entry.duration_us;
            s.max_us = entry.duration_us;
            s.count = 1;
            stats[name] = s;
        } else {
            it->second.total_us += entry.duration_us;
            it->second.min_us = std::min(it->second.min_us, entry.duration_us);
            it->second.max_us = std::max(it->second.max_us, entry.duration_us);
            it->second.count++;
        }
    }

    return stats;
}

void Profiler::print_summary() const {
    auto stats = statistics();

    if (stats.empty()) {
        std::cout << "[Profiler] No profile data collected.\n";
        return;
    }

    std::cout << "\n=== Profile Summary ===\n";
    std::cout << std::left << std::setw(40) << "Name"
              << std::right << std::setw(10) << "Count"
              << std::setw(12) << "Total(ms)"
              << std::setw(12) << "Avg(ms)"
              << std::setw(12) << "Min(us)"
              << std::setw(12) << "Max(us)"
              << "\n";
    std::cout << std::string(98, '-') << "\n";

    // Sort by total time (descending)
    std::vector<ProfileStats> sorted_stats;
    for (const auto& pair : stats) {
        sorted_stats.push_back(pair.second);
    }
    std::sort(sorted_stats.begin(), sorted_stats.end(),
              [](const ProfileStats& a, const ProfileStats& b) {
                  return a.total_us > b.total_us;
              });

    for (const auto& s : sorted_stats) {
        std::cout << std::left << std::setw(40) << s.name
                  << std::right << std::setw(10) << s.count
                  << std::setw(12) << std::fixed << std::setprecision(2) << s.total_ms()
                  << std::setw(12) << std::fixed << std::setprecision(3) << s.avg_ms()
                  << std::setw(12) << s.min_us
                  << std::setw(12) << s.max_us
                  << "\n";
    }

    std::cout << std::string(98, '-') << "\n";
    std::cout << "Total entries: " << entry_count() << "\n\n";
}

void Profiler::print_report() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    if (impl_->entries_.empty()) {
        std::cout << "[Profiler] No profile data collected.\n";
        return;
    }

    std::cout << "\n=== Profile Report (Chronological) ===\n";
    std::cout << std::left << std::setw(40) << "Name"
              << std::right << std::setw(12) << "Duration(us)"
              << "  " << std::left << "Location"
              << "\n";
    std::cout << std::string(80, '-') << "\n";

    for (const auto& entry : impl_->entries_) {
        std::string location;
        if (entry.func) {
            location = std::string(entry.func) + "():" + std::to_string(entry.line);
        }

        std::cout << std::left << std::setw(40) << (entry.name ? entry.name : "")
                  << std::right << std::setw(12) << entry.duration_us
                  << "  " << std::left << location
                  << "\n";
    }

    std::cout << std::string(80, '-') << "\n\n";
}

std::string Profiler::to_json() const {
    // Get a copy of entries while holding the lock
    std::vector<ProfileEntry> entries_copy;
    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        entries_copy = impl_->entries_;
    }

    // Compute statistics from copy (no lock needed)
    std::map<std::string, ProfileStats> stats;
    for (const auto& entry : entries_copy) {
        if (!entry.name) continue;

        std::string name(entry.name);
        auto it = stats.find(name);

        if (it == stats.end()) {
            ProfileStats s;
            s.name = name;
            s.total_us = entry.duration_us;
            s.min_us = entry.duration_us;
            s.max_us = entry.duration_us;
            s.count = 1;
            stats[name] = s;
        } else {
            it->second.total_us += entry.duration_us;
            it->second.min_us = std::min(it->second.min_us, entry.duration_us);
            it->second.max_us = std::max(it->second.max_us, entry.duration_us);
            it->second.count++;
        }
    }

    std::ostringstream oss;
    oss << "{\n  \"entries\": [\n";

    bool first = true;
    for (const auto& entry : entries_copy) {
        if (!first) oss << ",\n";
        first = false;

        oss << "    {"
            << "\"name\":\"" << (entry.name ? entry.name : "") << "\","
            << "\"duration_us\":" << entry.duration_us << ","
            << "\"timestamp_us\":" << entry.timestamp_us << ","
            << "\"func\":\"" << (entry.func ? entry.func : "") << "\","
            << "\"line\":" << entry.line
            << "}";
    }

    oss << "\n  ],\n  \"statistics\": [\n";

    first = true;
    for (const auto& pair : stats) {
        if (!first) oss << ",\n";
        first = false;

        const auto& s = pair.second;
        oss << "    {"
            << "\"name\":\"" << s.name << "\","
            << "\"count\":" << s.count << ","
            << "\"total_us\":" << s.total_us << ","
            << "\"avg_us\":" << std::fixed << std::setprecision(2) << s.avg_us() << ","
            << "\"min_us\":" << s.min_us << ","
            << "\"max_us\":" << s.max_us
            << "}";
    }

    oss << "\n  ]\n}\n";
    return oss.str();
}

void Profiler::set_max_entries(size_t max_entries) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    impl_->max_entries_ = max_entries;

    // Trim if needed
    if (max_entries > 0 && impl_->entries_.size() > max_entries) {
        impl_->entries_.erase(
            impl_->entries_.begin(),
            impl_->entries_.begin() + (impl_->entries_.size() - max_entries));
    }
}

// ============================================================================
// ScopedProfiler Implementation
// ============================================================================

ScopedProfiler::ScopedProfiler(const char* name, const char* file,
                               const char* func, int line)
    : name_(name), file_(file), func_(func), line_(line)
    , start_(std::chrono::high_resolution_clock::now()) {}

ScopedProfiler::~ScopedProfiler() {
    if (g_profiling_enabled) {
        auto end = std::chrono::high_resolution_clock::now();

        ProfileEntry entry;
        entry.name = name_;
        entry.file = file_;
        entry.func = func_;
        entry.line = line_;
        entry.duration_us = std::chrono::duration_cast<std::chrono::microseconds>(
            end - start_).count();
        entry.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
            start_.time_since_epoch()).count();

        Profiler::instance().record(entry);
    }
}

int64_t ScopedProfiler::elapsed_us() const {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
        now - start_).count();
}

void log_message(LogLevel level, const char* file, const char* func,
                 int line, const std::string& message) {
#if defined(LIGHTUSD_PRODUCTION_BUILD)
    // In production builds, suppress Debug level entirely
    if (level == LogLevel::Debug) {
        return;
    }
    // Don't include file path in production
    (void)file;
#endif

    const char* level_str = "";
    switch (level) {
        case LogLevel::Debug:   level_str = "DEBUG"; break;
        case LogLevel::Info:    level_str = "INFO"; break;
        case LogLevel::Warning: level_str = "WARN"; break;
        case LogLevel::Error:   level_str = "ERROR"; break;
    }

    std::ostream& out = (level == LogLevel::Error || level == LogLevel::Warning)
                        ? std::cerr : std::cout;

#if defined(LIGHTUSD_PRODUCTION_BUILD)
    out << "[" << level_str << "] " << func << "():" << line
        << " " << message << "\n";
#else
    out << "[" << level_str << "] " << file << ":" << func << "():" << line
        << " " << message << "\n";
#endif
}

// ============================================================================
// MemoryTracker Implementation
// ============================================================================

struct MemoryTracker::Impl {
    mutable std::mutex mutex_;
    std::vector<MemoryEntry> entries_;
    std::map<void*, size_t> active_allocs_;  // ptr -> size for tracking frees
    std::map<std::string, MemoryStats> tag_stats_;

    size_t current_bytes_ = 0;
    size_t peak_bytes_ = 0;
    size_t limit_bytes_ = 0;  // 0 = unlimited
    size_t max_entries_ = 0;  // 0 = unlimited
    MemoryLimitCallback limit_callback_ = nullptr;

    Impl() = default;

    void update_peak() {
        if (current_bytes_ > peak_bytes_) {
            peak_bytes_ = current_bytes_;
        }
    }
};

MemoryTracker::MemoryTracker() : impl_(new Impl()) {}

MemoryTracker::~MemoryTracker() {
    delete impl_;
}

MemoryTracker& MemoryTracker::instance() {
    static MemoryTracker instance;
    return instance;
}

void MemoryTracker::record_alloc(const char* tag, size_t size, void* ptr,
                                  const char* file, const char* func, int line) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    // Update current bytes
    impl_->current_bytes_ += size;
    impl_->update_peak();

    // Track pointer for later free
    if (ptr) {
        impl_->active_allocs_[ptr] = size;
    }

    // Update tag statistics
    std::string tag_str = tag ? tag : "default";
    auto& stats = impl_->tag_stats_[tag_str];
    stats.tag = tag_str;
    stats.current_bytes += size;
    stats.total_allocated += size;
    stats.alloc_count++;
    if (stats.current_bytes > stats.peak_bytes) {
        stats.peak_bytes = stats.current_bytes;
    }

    // Record entry
    if (impl_->max_entries_ > 0 && impl_->entries_.size() >= impl_->max_entries_) {
        impl_->entries_.erase(impl_->entries_.begin());
    }

    MemoryEntry entry;
    entry.tag = tag;
    entry.file = file;
    entry.func = func;
    entry.line = line;
    entry.size = size;
    entry.ptr = ptr;
    entry.is_allocation = true;
    entry.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    impl_->entries_.push_back(entry);
}

void MemoryTracker::record_free(const char* tag, size_t size, void* ptr,
                                 const char* file, const char* func, int line) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    // Update current bytes
    if (size <= impl_->current_bytes_) {
        impl_->current_bytes_ -= size;
    } else {
        impl_->current_bytes_ = 0;
    }

    // Remove from active allocations
    if (ptr) {
        impl_->active_allocs_.erase(ptr);
    }

    // Update tag statistics
    std::string tag_str = tag ? tag : "default";
    auto it = impl_->tag_stats_.find(tag_str);
    if (it != impl_->tag_stats_.end()) {
        if (size <= it->second.current_bytes) {
            it->second.current_bytes -= size;
        } else {
            it->second.current_bytes = 0;
        }
        it->second.total_freed += size;
        it->second.free_count++;
    }

    // Record entry
    if (impl_->max_entries_ > 0 && impl_->entries_.size() >= impl_->max_entries_) {
        impl_->entries_.erase(impl_->entries_.begin());
    }

    MemoryEntry entry;
    entry.tag = tag;
    entry.file = file;
    entry.func = func;
    entry.line = line;
    entry.size = size;
    entry.ptr = ptr;
    entry.is_allocation = false;
    entry.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now().time_since_epoch()).count();

    impl_->entries_.push_back(entry);
}

void MemoryTracker::record_free(void* ptr) {
    if (!ptr) return;

    std::lock_guard<std::mutex> lock(impl_->mutex_);

    auto it = impl_->active_allocs_.find(ptr);
    if (it != impl_->active_allocs_.end()) {
        size_t size = it->second;

        // Update current bytes
        if (size <= impl_->current_bytes_) {
            impl_->current_bytes_ -= size;
        } else {
            impl_->current_bytes_ = 0;
        }

        impl_->active_allocs_.erase(it);

        // Record entry (without tag since we don't know it)
        if (impl_->max_entries_ == 0 || impl_->entries_.size() < impl_->max_entries_) {
            MemoryEntry entry;
            entry.tag = nullptr;
            entry.size = size;
            entry.ptr = ptr;
            entry.is_allocation = false;
            entry.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now().time_since_epoch()).count();
            impl_->entries_.push_back(entry);
        }
    }
}

bool MemoryTracker::check_limit(size_t size) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    if (impl_->limit_bytes_ == 0) {
        return true;  // No limit
    }

    return (impl_->current_bytes_ + size) <= impl_->limit_bytes_;
}

bool MemoryTracker::try_alloc(const char* tag, size_t size, void* ptr,
                               const char* file, const char* func, int line) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    // Check limit
    if (impl_->limit_bytes_ > 0 && (impl_->current_bytes_ + size) > impl_->limit_bytes_) {
        // Limit would be exceeded
        if (impl_->limit_callback_) {
            // Ask callback if we should allow anyway
            if (!impl_->limit_callback_(size, impl_->current_bytes_, impl_->limit_bytes_)) {
                return false;
            }
        } else {
            return false;
        }
    }

    // Record the allocation (unlock mutex first to avoid recursive lock)
    // Actually, we need to do this within the same lock...

    // Update current bytes
    impl_->current_bytes_ += size;
    impl_->update_peak();

    // Track pointer
    if (ptr) {
        impl_->active_allocs_[ptr] = size;
    }

    // Update tag statistics
    std::string tag_str = tag ? tag : "default";
    auto& stats = impl_->tag_stats_[tag_str];
    stats.tag = tag_str;
    stats.current_bytes += size;
    stats.total_allocated += size;
    stats.alloc_count++;
    if (stats.current_bytes > stats.peak_bytes) {
        stats.peak_bytes = stats.current_bytes;
    }

    // Record entry
    if (impl_->max_entries_ == 0 || impl_->entries_.size() < impl_->max_entries_) {
        MemoryEntry entry;
        entry.tag = tag;
        entry.file = file;
        entry.func = func;
        entry.line = line;
        entry.size = size;
        entry.ptr = ptr;
        entry.is_allocation = true;
        entry.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::high_resolution_clock::now().time_since_epoch()).count();
        impl_->entries_.push_back(entry);
    }

    return true;
}

void MemoryTracker::set_limit(size_t limit_bytes) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    impl_->limit_bytes_ = limit_bytes;
}

void MemoryTracker::set_limit_mb(size_t limit_mb) {
    set_limit(limit_mb * 1024 * 1024);
}

size_t MemoryTracker::limit() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->limit_bytes_;
}

void MemoryTracker::set_limit_callback(MemoryLimitCallback callback) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    impl_->limit_callback_ = callback;
}

size_t MemoryTracker::current_bytes() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->current_bytes_;
}

size_t MemoryTracker::peak_bytes() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->peak_bytes_;
}

size_t MemoryTracker::current_bytes(const char* tag) const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    std::string tag_str = tag ? tag : "default";
    auto it = impl_->tag_stats_.find(tag_str);
    if (it != impl_->tag_stats_.end()) {
        return it->second.current_bytes;
    }
    return 0;
}

size_t MemoryTracker::active_alloc_count() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->active_allocs_.size();
}

bool MemoryTracker::is_limit_exceeded() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    if (impl_->limit_bytes_ == 0) {
        return false;
    }
    return impl_->current_bytes_ > impl_->limit_bytes_;
}

size_t MemoryTracker::available_bytes() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    if (impl_->limit_bytes_ == 0) {
        return SIZE_MAX;
    }
    if (impl_->current_bytes_ >= impl_->limit_bytes_) {
        return 0;
    }
    return impl_->limit_bytes_ - impl_->current_bytes_;
}

std::map<std::string, MemoryStats> MemoryTracker::statistics() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->tag_stats_;
}

MemoryStats MemoryTracker::total_statistics() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    MemoryStats total;
    total.tag = "TOTAL";
    total.current_bytes = impl_->current_bytes_;
    total.peak_bytes = impl_->peak_bytes_;

    for (const auto& pair : impl_->tag_stats_) {
        total.total_allocated += pair.second.total_allocated;
        total.total_freed += pair.second.total_freed;
        total.alloc_count += pair.second.alloc_count;
        total.free_count += pair.second.free_count;
    }

    return total;
}

std::vector<MemoryEntry> MemoryTracker::entries() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    return impl_->entries_;
}

void MemoryTracker::print_summary() const {
    auto stats = statistics();
    auto total = total_statistics();

    std::cout << "\n=== Memory Tracking Summary ===\n";
    std::cout << std::left << std::setw(25) << "Tag"
              << std::right << std::setw(12) << "Current(MB)"
              << std::setw(12) << "Peak(MB)"
              << std::setw(10) << "Allocs"
              << std::setw(10) << "Frees"
              << "\n";
    std::cout << std::string(69, '-') << "\n";

    for (const auto& pair : stats) {
        const auto& s = pair.second;
        std::cout << std::left << std::setw(25) << s.tag
                  << std::right << std::setw(12) << std::fixed << std::setprecision(3) << s.current_mb()
                  << std::setw(12) << std::fixed << std::setprecision(3) << s.peak_mb()
                  << std::setw(10) << s.alloc_count
                  << std::setw(10) << s.free_count
                  << "\n";
    }

    std::cout << std::string(69, '-') << "\n";
    std::cout << std::left << std::setw(25) << "TOTAL"
              << std::right << std::setw(12) << std::fixed << std::setprecision(3) << total.current_mb()
              << std::setw(12) << std::fixed << std::setprecision(3) << total.peak_mb()
              << std::setw(10) << total.alloc_count
              << std::setw(10) << total.free_count
              << "\n";

    size_t lim = limit();
    if (lim > 0) {
        std::cout << "\nMemory Limit: " << (lim / (1024.0 * 1024.0)) << " MB"
                  << " (Available: " << (available_bytes() / (1024.0 * 1024.0)) << " MB)\n";
    }

    std::cout << "Active allocations: " << active_alloc_count() << "\n\n";
}

void MemoryTracker::print_report() const {
    std::lock_guard<std::mutex> lock(impl_->mutex_);

    if (impl_->entries_.empty()) {
        std::cout << "[MemoryTracker] No memory tracking data collected.\n";
        return;
    }

    std::cout << "\n=== Memory Tracking Report ===\n";
    std::cout << std::left << std::setw(8) << "Type"
              << std::setw(20) << "Tag"
              << std::right << std::setw(12) << "Size(bytes)"
              << "  " << std::left << "Location"
              << "\n";
    std::cout << std::string(70, '-') << "\n";

    for (const auto& entry : impl_->entries_) {
        std::string location;
        if (entry.func) {
            location = std::string(entry.func) + "():" + std::to_string(entry.line);
        }

        std::cout << std::left << std::setw(8) << (entry.is_allocation ? "ALLOC" : "FREE")
                  << std::setw(20) << (entry.tag ? entry.tag : "-")
                  << std::right << std::setw(12) << entry.size
                  << "  " << std::left << location
                  << "\n";
    }

    std::cout << std::string(70, '-') << "\n\n";
}

std::string MemoryTracker::to_json() const {
    std::vector<MemoryEntry> entries_copy;
    std::map<std::string, MemoryStats> stats;
    MemoryStats total;
    size_t lim;

    {
        std::lock_guard<std::mutex> lock(impl_->mutex_);
        entries_copy = impl_->entries_;
        stats = impl_->tag_stats_;
        lim = impl_->limit_bytes_;

        total.tag = "TOTAL";
        total.current_bytes = impl_->current_bytes_;
        total.peak_bytes = impl_->peak_bytes_;
        for (const auto& pair : stats) {
            total.total_allocated += pair.second.total_allocated;
            total.total_freed += pair.second.total_freed;
            total.alloc_count += pair.second.alloc_count;
            total.free_count += pair.second.free_count;
        }
    }

    std::ostringstream oss;
    oss << "{\n";
    oss << "  \"limit_bytes\": " << lim << ",\n";
    oss << "  \"current_bytes\": " << total.current_bytes << ",\n";
    oss << "  \"peak_bytes\": " << total.peak_bytes << ",\n";
    oss << "  \"statistics\": [\n";

    bool first = true;
    for (const auto& pair : stats) {
        if (!first) oss << ",\n";
        first = false;

        const auto& s = pair.second;
        oss << "    {"
            << "\"tag\":\"" << s.tag << "\","
            << "\"current_bytes\":" << s.current_bytes << ","
            << "\"peak_bytes\":" << s.peak_bytes << ","
            << "\"total_allocated\":" << s.total_allocated << ","
            << "\"total_freed\":" << s.total_freed << ","
            << "\"alloc_count\":" << s.alloc_count << ","
            << "\"free_count\":" << s.free_count
            << "}";
    }

    oss << "\n  ],\n  \"entries\": [\n";

    first = true;
    for (const auto& entry : entries_copy) {
        if (!first) oss << ",\n";
        first = false;

        oss << "    {"
            << "\"type\":\"" << (entry.is_allocation ? "alloc" : "free") << "\","
            << "\"tag\":\"" << (entry.tag ? entry.tag : "") << "\","
            << "\"size\":" << entry.size << ","
            << "\"timestamp_us\":" << entry.timestamp_us
            << "}";
    }

    oss << "\n  ]\n}\n";
    return oss.str();
}

void MemoryTracker::clear() {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    impl_->entries_.clear();
    impl_->active_allocs_.clear();
    impl_->tag_stats_.clear();
    impl_->current_bytes_ = 0;
    impl_->peak_bytes_ = 0;
}

void MemoryTracker::set_max_entries(size_t max_entries) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    impl_->max_entries_ = max_entries;

    if (max_entries > 0 && impl_->entries_.size() > max_entries) {
        impl_->entries_.erase(
            impl_->entries_.begin(),
            impl_->entries_.begin() + (impl_->entries_.size() - max_entries));
    }
}

void MemoryTracker::reset_peak() {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    impl_->peak_bytes_ = impl_->current_bytes_;
    for (auto& pair : impl_->tag_stats_) {
        pair.second.peak_bytes = pair.second.current_bytes;
    }
}

// ============================================================================
// ScopedMemory Implementation
// ============================================================================

ScopedMemory::ScopedMemory(const char* tag, size_t size,
                           const char* file, const char* func, int line)
    : tag_(tag), size_(size), ptr_(this), file_(file), func_(func), line_(line)
    , tracked_(g_memory_tracking_enabled)
{
    if (tracked_) {
        MemoryTracker::instance().record_alloc(tag_, size_, ptr_, file_, func_, line_);
    }
}

ScopedMemory::~ScopedMemory() {
    if (tracked_) {
        MemoryTracker::instance().record_free(tag_, size_, ptr_, file_, func_, line_);
    }
}

} // namespace v1
} // namespace lightusd
