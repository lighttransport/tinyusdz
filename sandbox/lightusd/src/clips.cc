// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Value Clips implementation

#include "lightusd/clips.hh"
#include "lightusd/debug.hh"
#include <map>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>
#include <iomanip>

namespace lightusd {
namespace v1 {

// ============================================================================
// ClipSet Implementation
// ============================================================================

struct ClipSet::Impl {
    std::string name_;
    std::vector<std::string> asset_paths_;
    Path prim_path_;
    std::vector<TimeCodePair> active_;
    std::vector<TimeCodePair> times_;
    std::string manifest_asset_path_;
    std::string template_asset_path_;
    double template_start_time_ = 0.0;
    double template_end_time_ = 0.0;
    double template_stride_ = 1.0;

    Impl() = default;

    Impl(const Impl& other)
        : name_(other.name_)
        , asset_paths_(other.asset_paths_)
        , prim_path_(other.prim_path_)
        , active_(other.active_)
        , times_(other.times_)
        , manifest_asset_path_(other.manifest_asset_path_)
        , template_asset_path_(other.template_asset_path_)
        , template_start_time_(other.template_start_time_)
        , template_end_time_(other.template_end_time_)
        , template_stride_(other.template_stride_) {}

    Impl(Impl&& other) noexcept
        : name_(std::move(other.name_))
        , asset_paths_(std::move(other.asset_paths_))
        , prim_path_(std::move(other.prim_path_))
        , active_(std::move(other.active_))
        , times_(std::move(other.times_))
        , manifest_asset_path_(std::move(other.manifest_asset_path_))
        , template_asset_path_(std::move(other.template_asset_path_))
        , template_start_time_(other.template_start_time_)
        , template_end_time_(other.template_end_time_)
        , template_stride_(other.template_stride_) {}
};

ClipSet::ClipSet()
    : impl_(new Impl()) {}

ClipSet::~ClipSet() = default;

ClipSet::ClipSet(const ClipSet& other)
    : impl_(other.impl_ ? new Impl(*other.impl_) : new Impl()) {}

ClipSet::ClipSet(ClipSet&& other) noexcept
    : impl_(std::move(other.impl_)) {
    if (!impl_) {
        impl_.reset(new Impl());
    }
}

ClipSet& ClipSet::operator=(const ClipSet& other) {
    if (this != &other) {
        impl_.reset(other.impl_ ? new Impl(*other.impl_) : new Impl());
    }
    return *this;
}

ClipSet& ClipSet::operator=(ClipSet&& other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
        if (!impl_) {
            impl_.reset(new Impl());
        }
    }
    return *this;
}

const std::string& ClipSet::name() const {
    return impl_->name_;
}

void ClipSet::set_name(const std::string& name) {
    impl_->name_ = name;
}

const std::vector<std::string>& ClipSet::asset_paths() const {
    return impl_->asset_paths_;
}

void ClipSet::set_asset_paths(const std::vector<std::string>& paths) {
    impl_->asset_paths_ = paths;
}

void ClipSet::add_asset_path(const std::string& path) {
    impl_->asset_paths_.push_back(path);
}

void ClipSet::clear_asset_paths() {
    impl_->asset_paths_.clear();
}

const Path& ClipSet::prim_path() const {
    return impl_->prim_path_;
}

void ClipSet::set_prim_path(const Path& path) {
    impl_->prim_path_ = path;
}

const std::vector<TimeCodePair>& ClipSet::active() const {
    return impl_->active_;
}

void ClipSet::set_active(const std::vector<TimeCodePair>& active) {
    impl_->active_ = active;
}

void ClipSet::add_active(double stage_time, double clip_index) {
    impl_->active_.push_back(TimeCodePair(stage_time, clip_index));
}

void ClipSet::clear_active() {
    impl_->active_.clear();
}

const std::vector<TimeCodePair>& ClipSet::times() const {
    return impl_->times_;
}

void ClipSet::set_times(const std::vector<TimeCodePair>& times) {
    impl_->times_ = times;
}

void ClipSet::add_time(double stage_time, double clip_time) {
    impl_->times_.push_back(TimeCodePair(stage_time, clip_time));
}

void ClipSet::clear_times() {
    impl_->times_.clear();
}

const std::string& ClipSet::manifest_asset_path() const {
    return impl_->manifest_asset_path_;
}

void ClipSet::set_manifest_asset_path(const std::string& path) {
    impl_->manifest_asset_path_ = path;
}

bool ClipSet::has_manifest() const {
    return !impl_->manifest_asset_path_.empty();
}

const std::string& ClipSet::template_asset_path() const {
    return impl_->template_asset_path_;
}

void ClipSet::set_template_asset_path(const std::string& path) {
    impl_->template_asset_path_ = path;
}

bool ClipSet::has_template() const {
    return !impl_->template_asset_path_.empty();
}

double ClipSet::template_start_time() const {
    return impl_->template_start_time_;
}

void ClipSet::set_template_start_time(double t) {
    impl_->template_start_time_ = t;
}

double ClipSet::template_end_time() const {
    return impl_->template_end_time_;
}

void ClipSet::set_template_end_time(double t) {
    impl_->template_end_time_ = t;
}

double ClipSet::template_stride() const {
    return impl_->template_stride_;
}

void ClipSet::set_template_stride(double s) {
    impl_->template_stride_ = s;
}

bool ClipSet::is_valid() const {
    // Must have either asset paths or template
    bool has_assets = !impl_->asset_paths_.empty() || !impl_->template_asset_path_.empty();
    return has_assets;
}

void ClipSet::clear() {
    impl_->name_.clear();
    impl_->asset_paths_.clear();
    impl_->prim_path_ = Path();
    impl_->active_.clear();
    impl_->times_.clear();
    impl_->manifest_asset_path_.clear();
    impl_->template_asset_path_.clear();
    impl_->template_start_time_ = 0.0;
    impl_->template_end_time_ = 0.0;
    impl_->template_stride_ = 1.0;
}

// ============================================================================
// ClipSets Implementation
// ============================================================================

struct ClipSets::Impl {
    std::map<std::string, ClipSet> clip_sets_;

    Impl() = default;

    Impl(const Impl& other)
        : clip_sets_(other.clip_sets_) {}

    Impl(Impl&& other) noexcept
        : clip_sets_(std::move(other.clip_sets_)) {}
};

ClipSets::ClipSets()
    : impl_(new Impl()) {}

ClipSets::~ClipSets() = default;

ClipSets::ClipSets(const ClipSets& other)
    : impl_(other.impl_ ? new Impl(*other.impl_) : new Impl()) {}

ClipSets::ClipSets(ClipSets&& other) noexcept
    : impl_(std::move(other.impl_)) {
    if (!impl_) {
        impl_.reset(new Impl());
    }
}

ClipSets& ClipSets::operator=(const ClipSets& other) {
    if (this != &other) {
        impl_.reset(other.impl_ ? new Impl(*other.impl_) : new Impl());
    }
    return *this;
}

ClipSets& ClipSets::operator=(ClipSets&& other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
        if (!impl_) {
            impl_.reset(new Impl());
        }
    }
    return *this;
}

bool ClipSets::empty() const {
    return impl_->clip_sets_.empty();
}

size_t ClipSets::size() const {
    return impl_->clip_sets_.size();
}

bool ClipSets::has(const std::string& name) const {
    return impl_->clip_sets_.find(name) != impl_->clip_sets_.end();
}

const ClipSet* ClipSets::get(const std::string& name) const {
    auto it = impl_->clip_sets_.find(name);
    if (it == impl_->clip_sets_.end()) {
        return nullptr;
    }
    return &it->second;
}

ClipSet* ClipSets::get_mutable(const std::string& name) {
    auto it = impl_->clip_sets_.find(name);
    if (it == impl_->clip_sets_.end()) {
        return nullptr;
    }
    return &it->second;
}

void ClipSets::set(const std::string& name, ClipSet clip_set) {
    clip_set.set_name(name);
    impl_->clip_sets_[name] = std::move(clip_set);
}

bool ClipSets::remove(const std::string& name) {
    return impl_->clip_sets_.erase(name) > 0;
}

std::vector<std::string> ClipSets::names() const {
    std::vector<std::string> result;
    result.reserve(impl_->clip_sets_.size());
    for (const auto& pair : impl_->clip_sets_) {
        result.push_back(pair.first);
    }
    return result;
}

void ClipSets::clear() {
    impl_->clip_sets_.clear();
}

// ============================================================================
// Utility Functions
// ============================================================================

double lerp_time(double a, double b, double alpha) {
    return a + alpha * (b - a);
}

double inverse_lerp(double a, double b, double t) {
    if (std::abs(b - a) < 1e-12) {
        return 0.0;  // Avoid division by zero
    }
    double result = (t - a) / (b - a);
    // Clamp to [0, 1]
    if (result < 0.0) return 0.0;
    if (result > 1.0) return 1.0;
    return result;
}

std::string expand_template_path(const std::string& template_path, double time) {
    DCOUT("expand_template_path: template=" << template_path << ", time=" << time);

    std::string result = template_path;
    int frame = static_cast<int>(std::round(time));

    // Handle ### style placeholders
    size_t hash_start = result.find('#');
    if (hash_start != std::string::npos) {
        size_t hash_end = hash_start;
        while (hash_end < result.size() && result[hash_end] == '#') {
            ++hash_end;
        }
        size_t num_hashes = hash_end - hash_start;

        std::ostringstream oss;
        oss << std::setfill('0') << std::setw(static_cast<int>(num_hashes)) << frame;
        result = result.substr(0, hash_start) + oss.str() + result.substr(hash_end);
        return result;
    }

    // Handle printf-style %0Nd placeholders
    size_t percent_pos = result.find('%');
    if (percent_pos != std::string::npos) {
        // Find the format specifier
        size_t spec_start = percent_pos;
        size_t spec_end = spec_start + 1;

        // Skip leading 0 for padding
        bool zero_pad = false;
        if (spec_end < result.size() && result[spec_end] == '0') {
            zero_pad = true;
            ++spec_end;
        }

        // Get width
        int width = 0;
        while (spec_end < result.size() && result[spec_end] >= '0' && result[spec_end] <= '9') {
            width = width * 10 + (result[spec_end] - '0');
            ++spec_end;
        }

        // Check for 'd' specifier
        if (spec_end < result.size() && result[spec_end] == 'd') {
            std::ostringstream oss;
            if (zero_pad && width > 0) {
                oss << std::setfill('0') << std::setw(width);
            } else if (width > 0) {
                oss << std::setw(width);
            }
            oss << frame;
            result = result.substr(0, spec_start) + oss.str() + result.substr(spec_end + 1);
        }
    }

    return result;
}

std::vector<std::string> generate_template_paths(
    const std::string& template_path,
    double start_time, double end_time, double stride)
{
    std::vector<std::string> paths;

    if (stride <= 0.0 || start_time > end_time) {
        return paths;
    }

    for (double t = start_time; t <= end_time + 1e-9; t += stride) {
        paths.push_back(expand_template_path(template_path, t));
    }

    return paths;
}

// ============================================================================
// ClipResolver Implementation
// ============================================================================

struct ClipResolver::Impl {
    ClipSet clip_set_;
    bool has_clip_set_ = false;

    Impl() = default;

    Impl(const Impl& other)
        : clip_set_(other.clip_set_)
        , has_clip_set_(other.has_clip_set_) {}

    Impl(Impl&& other) noexcept
        : clip_set_(std::move(other.clip_set_))
        , has_clip_set_(other.has_clip_set_) {}

    // Find the active clip index at a given stage time using the 'active' array
    int find_active_index(double stage_time) const {
        const auto& active = clip_set_.active();

        if (active.empty()) {
            // If no active array, use time mapping to determine clip
            return find_clip_from_times(stage_time);
        }

        // Find the last entry where stage_time >= entry.stage_time
        int result = -1;
        for (size_t i = 0; i < active.size(); ++i) {
            if (stage_time >= active[i].stage_time) {
                result = static_cast<int>(active[i].value);
            } else {
                break;  // active array is sorted by stage_time
            }
        }

        return result;
    }

    // Find clip index from times array when no active array exists
    int find_clip_from_times(double stage_time) const {
        const auto& times = clip_set_.times();
        const auto& asset_paths = get_asset_paths();

        if (times.empty() || asset_paths.empty()) {
            return -1;
        }

        // When no active array, each clip covers one stride period
        // Determine which clip based on time
        if (clip_set_.has_template()) {
            double start = clip_set_.template_start_time();
            double stride = clip_set_.template_stride();

            if (stride <= 0.0) return 0;

            int index = static_cast<int>((stage_time - start) / stride);
            if (index < 0) index = 0;
            if (index >= static_cast<int>(asset_paths.size())) {
                index = static_cast<int>(asset_paths.size()) - 1;
            }
            return index;
        }

        // Default: use first clip
        return 0;
    }

    // Map stage time to clip time using the 'times' array
    double map_stage_to_clip_time(double stage_time) const {
        const auto& times = clip_set_.times();

        if (times.empty()) {
            return stage_time;  // Identity mapping
        }

        // Find bracketing times
        if (stage_time <= times.front().stage_time) {
            return times.front().value;
        }

        if (stage_time >= times.back().stage_time) {
            return times.back().value;
        }

        // Find bracket and interpolate
        for (size_t i = 0; i < times.size() - 1; ++i) {
            if (stage_time >= times[i].stage_time &&
                stage_time < times[i + 1].stage_time) {
                double alpha = inverse_lerp(
                    times[i].stage_time,
                    times[i + 1].stage_time,
                    stage_time);
                return lerp_time(times[i].value, times[i + 1].value, alpha);
            }
        }

        return stage_time;
    }

    // Get asset paths (expanded from template if necessary)
    std::vector<std::string> get_asset_paths() const {
        if (!clip_set_.asset_paths().empty()) {
            return clip_set_.asset_paths();
        }

        if (clip_set_.has_template()) {
            return generate_template_paths(
                clip_set_.template_asset_path(),
                clip_set_.template_start_time(),
                clip_set_.template_end_time(),
                clip_set_.template_stride());
        }

        return {};
    }
};

ClipResolver::ClipResolver()
    : impl_(new Impl()) {}

ClipResolver::~ClipResolver() = default;

ClipResolver::ClipResolver(const ClipResolver& other)
    : impl_(other.impl_ ? new Impl(*other.impl_) : new Impl()) {}

ClipResolver::ClipResolver(ClipResolver&& other) noexcept
    : impl_(std::move(other.impl_)) {
    if (!impl_) {
        impl_.reset(new Impl());
    }
}

ClipResolver& ClipResolver::operator=(const ClipResolver& other) {
    if (this != &other) {
        impl_.reset(other.impl_ ? new Impl(*other.impl_) : new Impl());
    }
    return *this;
}

ClipResolver& ClipResolver::operator=(ClipResolver&& other) noexcept {
    if (this != &other) {
        impl_ = std::move(other.impl_);
        if (!impl_) {
            impl_.reset(new Impl());
        }
    }
    return *this;
}

void ClipResolver::set_clip_set(const ClipSet& clip_set) {
    impl_->clip_set_ = clip_set;
    impl_->has_clip_set_ = true;
}

const ClipSet* ClipResolver::clip_set() const {
    return impl_->has_clip_set_ ? &impl_->clip_set_ : nullptr;
}

ClipInfo ClipResolver::resolve(double stage_time) const {
    PROFILE_FUNCTION();
    DCOUT("resolve: stage_time=" << stage_time);

    ClipInfo info;

    if (!impl_->has_clip_set_) {
        DCOUT("resolve: no clip set configured");
        return info;
    }

    int clip_idx = impl_->find_active_index(stage_time);
    if (clip_idx < 0) {
        DCOUT("resolve: no active clip at time " << stage_time);
        return info;
    }

    auto paths = impl_->get_asset_paths();
    if (clip_idx >= static_cast<int>(paths.size())) {
        DCOUT("resolve: clip index " << clip_idx << " out of range");
        return info;
    }

    info.clip_index = static_cast<size_t>(clip_idx);
    info.asset_path = paths[clip_idx];
    info.prim_path = impl_->clip_set_.prim_path();
    info.clip_time = impl_->map_stage_to_clip_time(stage_time);
    info.valid = true;

    DCOUT("resolve: clip_index=" << info.clip_index
          << ", asset=" << info.asset_path
          << ", clip_time=" << info.clip_time);

    return info;
}

int ClipResolver::active_clip_index(double stage_time) const {
    if (!impl_->has_clip_set_) {
        return -1;
    }
    return impl_->find_active_index(stage_time);
}

double ClipResolver::map_time(double stage_time) const {
    if (!impl_->has_clip_set_) {
        return stage_time;
    }
    return impl_->map_stage_to_clip_time(stage_time);
}

std::vector<std::string> ClipResolver::expanded_asset_paths() const {
    if (!impl_->has_clip_set_) {
        return {};
    }
    return impl_->get_asset_paths();
}

std::pair<double, double> ClipResolver::stage_time_range() const {
    if (!impl_->has_clip_set_) {
        return {0.0, 0.0};
    }

    const auto& times = impl_->clip_set_.times();
    const auto& active = impl_->clip_set_.active();

    double min_time = 0.0;
    double max_time = 0.0;
    bool has_range = false;

    // Check times array
    if (!times.empty()) {
        min_time = times.front().stage_time;
        max_time = times.back().stage_time;
        has_range = true;
    }

    // Check active array
    if (!active.empty()) {
        if (!has_range) {
            min_time = active.front().stage_time;
            max_time = active.back().stage_time;
            has_range = true;
        } else {
            min_time = std::min(min_time, active.front().stage_time);
            max_time = std::max(max_time, active.back().stage_time);
        }
    }

    // Check template
    if (impl_->clip_set_.has_template() && !has_range) {
        min_time = impl_->clip_set_.template_start_time();
        max_time = impl_->clip_set_.template_end_time();
    }

    return {min_time, max_time};
}

std::vector<double> ClipResolver::clip_boundaries() const {
    std::vector<double> boundaries;

    if (!impl_->has_clip_set_) {
        return boundaries;
    }

    const auto& active = impl_->clip_set_.active();
    for (const auto& entry : active) {
        boundaries.push_back(entry.stage_time);
    }

    // Remove duplicates and sort
    std::sort(boundaries.begin(), boundaries.end());
    boundaries.erase(
        std::unique(boundaries.begin(), boundaries.end()),
        boundaries.end());

    return boundaries;
}

bool ClipResolver::is_valid() const {
    return impl_->has_clip_set_ && impl_->clip_set_.is_valid();
}

} // namespace v1
} // namespace lightusd
