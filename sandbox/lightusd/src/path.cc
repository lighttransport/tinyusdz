// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Path implementation

#include "lightusd/path.hh"
#include <algorithm>
#include <functional>

namespace lightusd {
namespace v1 {

Path::Path()
    : valid_(false) {
}

Path::Path(const char* path_str)
    : valid_(false) {
    if (path_str) {
        parse_impl(path_str);
    }
}

Path::Path(const std::string& path_str)
    : valid_(false) {
    parse_impl(path_str);
}

Path::Path(const std::string& prim_path, const std::string& prop_name)
    : prim_part_(prim_path)
    , prop_part_(prop_name)
    , valid_(true) {
    // Validate prim path
    if (!prim_part_.empty() && prim_part_[0] != '/') {
        // Relative path is ok
    }
}

Path::Path(const Path& other)
    : prim_part_(other.prim_part_)
    , prop_part_(other.prop_part_)
    , valid_(other.valid_) {
}

Path::Path(Path&& other) noexcept
    : prim_part_(std::move(other.prim_part_))
    , prop_part_(std::move(other.prop_part_))
    , valid_(other.valid_) {
    other.valid_ = false;
}

Path& Path::operator=(const Path& other) {
    if (this != &other) {
        prim_part_ = other.prim_part_;
        prop_part_ = other.prop_part_;
        valid_ = other.valid_;
    }
    return *this;
}

Path& Path::operator=(Path&& other) noexcept {
    if (this != &other) {
        prim_part_ = std::move(other.prim_part_);
        prop_part_ = std::move(other.prop_part_);
        valid_ = other.valid_;
        other.valid_ = false;
    }
    return *this;
}

Path::~Path() = default;

void Path::parse_impl(const std::string& path_str) {
    valid_ = false;
    prim_part_.clear();
    prop_part_.clear();

    if (path_str.empty()) {
        return;
    }

    // Find property separator "."
    size_t dot_pos = path_str.rfind('.');

    // Check if dot is part of path or property separator
    // Property separator should not be at start and should have valid identifier after
    if (dot_pos != std::string::npos && dot_pos > 0) {
        // Check if there's a "/" after the dot (not a property separator)
        size_t slash_after_dot = path_str.find('/', dot_pos);
        if (slash_after_dot == std::string::npos) {
            // This is a property path
            prim_part_ = path_str.substr(0, dot_pos);
            prop_part_ = path_str.substr(dot_pos + 1);
            valid_ = true;
            return;
        }
    }

    // No property part - entire string is prim path
    prim_part_ = path_str;
    valid_ = true;
}

Result<Path> Path::parse(const std::string& path_str) {
    Path p;
    p.parse_impl(path_str);

    if (!p.valid_) {
        return Error("Invalid path: " + path_str);
    }

    return p;
}

Path Path::root() {
    Path p;
    p.prim_part_ = "/";
    p.valid_ = true;
    return p;
}

Path Path::empty_path() {
    return Path();
}

bool Path::is_valid() const {
    return valid_;
}

bool Path::is_empty() const {
    return prim_part_.empty() && prop_part_.empty();
}

bool Path::is_absolute() const {
    return valid_ && !prim_part_.empty() && prim_part_[0] == '/';
}

bool Path::is_relative() const {
    return valid_ && !prim_part_.empty() && prim_part_[0] != '/';
}

bool Path::is_root() const {
    return valid_ && prim_part_ == "/" && prop_part_.empty();
}

bool Path::is_prim_path() const {
    return valid_ && prop_part_.empty();
}

bool Path::is_property_path() const {
    return valid_ && !prop_part_.empty();
}

const std::string& Path::prim_part() const {
    return prim_part_;
}

const std::string& Path::prop_part() const {
    return prop_part_;
}

std::string Path::full_path() const {
    if (!valid_) {
        return "";
    }

    if (prop_part_.empty()) {
        return prim_part_;
    }

    return prim_part_ + "." + prop_part_;
}

std::string Path::element_name() const {
    if (!valid_ || prim_part_.empty()) {
        return "";
    }

    // Handle root path
    if (prim_part_ == "/") {
        return "";
    }

    // Find last "/"
    size_t last_slash = prim_part_.rfind('/');
    if (last_slash == std::string::npos) {
        return prim_part_;  // Relative path with single element
    }

    return prim_part_.substr(last_slash + 1);
}

Path Path::parent() const {
    if (!valid_ || prim_part_.empty() || prim_part_ == "/") {
        return Path();
    }

    // If has property, return prim path
    if (!prop_part_.empty()) {
        Path p;
        p.prim_part_ = prim_part_;
        p.valid_ = true;
        return p;
    }

    // Find last "/"
    size_t last_slash = prim_part_.rfind('/');
    if (last_slash == std::string::npos) {
        return Path();  // Relative path with single element has no parent
    }

    if (last_slash == 0) {
        return root();  // Parent is root
    }

    Path p;
    p.prim_part_ = prim_part_.substr(0, last_slash);
    p.valid_ = true;
    return p;
}

Path Path::parent_prim_path() const {
    // First strip property if present
    Path prim_only;
    prim_only.prim_part_ = prim_part_;
    prim_only.valid_ = valid_;

    return prim_only.parent();
}

Path Path::append_child(const std::string& name) const {
    if (!valid_ || name.empty()) {
        return Path();
    }

    // Cannot append child to property path
    if (!prop_part_.empty()) {
        return Path();
    }

    Path p;
    if (prim_part_.empty() || prim_part_ == "/") {
        p.prim_part_ = "/" + name;
    } else {
        p.prim_part_ = prim_part_ + "/" + name;
    }
    p.valid_ = true;
    return p;
}

Path Path::append_property(const std::string& name) const {
    if (!valid_ || name.empty()) {
        return Path();
    }

    // Cannot append property to property path
    if (!prop_part_.empty()) {
        return Path();
    }

    Path p;
    p.prim_part_ = prim_part_;
    p.prop_part_ = name;
    p.valid_ = true;
    return p;
}

Path Path::make_relative() const {
    if (!valid_ || prim_part_.empty()) {
        return Path();
    }

    if (prim_part_[0] != '/') {
        return *this;  // Already relative
    }

    Path p;
    p.prim_part_ = (prim_part_.size() > 1) ? prim_part_.substr(1) : "";
    p.prop_part_ = prop_part_;
    p.valid_ = !p.prim_part_.empty() || !p.prop_part_.empty();
    return p;
}

Path Path::make_absolute() const {
    if (!valid_ || prim_part_.empty()) {
        return Path();
    }

    if (prim_part_[0] == '/') {
        return *this;  // Already absolute
    }

    Path p;
    p.prim_part_ = "/" + prim_part_;
    p.prop_part_ = prop_part_;
    p.valid_ = true;
    return p;
}

bool Path::has_prefix(const Path& prefix) const {
    if (!valid_ || !prefix.valid_) {
        return false;
    }

    // Property paths don't match prim prefixes unless properties also match
    if (!prefix.prop_part_.empty()) {
        if (prop_part_ != prefix.prop_part_) {
            return false;
        }
    }

    // Check prim path prefix
    if (prefix.prim_part_.empty()) {
        return true;
    }

    if (prim_part_.size() < prefix.prim_part_.size()) {
        return false;
    }

    // Check prefix match
    if (prim_part_.compare(0, prefix.prim_part_.size(), prefix.prim_part_) != 0) {
        return false;
    }

    // Ensure prefix ends at path boundary
    if (prim_part_.size() > prefix.prim_part_.size()) {
        char next_char = prim_part_[prefix.prim_part_.size()];
        if (next_char != '/' && next_char != '.') {
            return false;
        }
    }

    return true;
}

Path Path::replace_prefix(const Path& old_prefix, const Path& new_prefix) const {
    if (!has_prefix(old_prefix)) {
        return *this;
    }

    Path p;
    p.prim_part_ = new_prefix.prim_part_ + prim_part_.substr(old_prefix.prim_part_.size());
    p.prop_part_ = prop_part_;
    p.valid_ = true;
    return p;
}

bool Path::operator==(const Path& other) const {
    return valid_ == other.valid_ &&
           prim_part_ == other.prim_part_ &&
           prop_part_ == other.prop_part_;
}

bool Path::operator!=(const Path& other) const {
    return !(*this == other);
}

bool Path::operator<(const Path& other) const {
    if (prim_part_ != other.prim_part_) {
        return prim_part_ < other.prim_part_;
    }
    return prop_part_ < other.prop_part_;
}

size_t Path::hash() const {
    std::hash<std::string> hasher;
    size_t h1 = hasher(prim_part_);
    size_t h2 = hasher(prop_part_);
    return h1 ^ (h2 << 1);
}

void Path::swap(Path& other) noexcept {
    std::swap(prim_part_, other.prim_part_);
    std::swap(prop_part_, other.prop_part_);
    std::swap(valid_, other.valid_);
}

} // namespace v1
} // namespace lightusd
