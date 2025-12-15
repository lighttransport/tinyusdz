// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - Layer Registry implementation

#include "lightusd/layer_registry.hh"
#include "lightusd/usda_reader.hh"
#include "lightusd/usdc_reader.hh"

#include <algorithm>
#include <fstream>
#include <cstring>

// Platform-specific path handling
#if defined(_WIN32) || defined(_WIN64)
#include <direct.h>
#define getcwd _getcwd
#define PATH_SEP '\\'
#else
#include <unistd.h>
#define PATH_SEP '/'
#endif

namespace lightusd {
namespace v1 {

// ============================================================================
// Path utilities
// ============================================================================

namespace {

bool is_absolute_path(const std::string& path) {
    if (path.empty()) return false;
#if defined(_WIN32) || defined(_WIN64)
    // Windows: C:\ or \\ (UNC)
    return (path.size() >= 3 && path[1] == ':' && (path[2] == '\\' || path[2] == '/'))
        || (path.size() >= 2 && path[0] == '\\' && path[1] == '\\');
#else
    return path[0] == '/';
#endif
}

std::string get_directory(const std::string& path) {
    size_t pos = path.find_last_of("/\\");
    if (pos == std::string::npos) return ".";
    if (pos == 0) return "/";
    return path.substr(0, pos);
}

std::string join_paths(const std::string& base, const std::string& rel) {
    if (base.empty()) return rel;
    if (rel.empty()) return base;
    if (is_absolute_path(rel)) return rel;

    char last = base[base.size() - 1];
    if (last == '/' || last == '\\') {
        return base + rel;
    }
    return base + PATH_SEP + rel;
}

std::string normalize_path(const std::string& path) {
    if (path.empty()) return path;

    std::vector<std::string> parts;
    std::string current;
    bool is_abs = is_absolute_path(path);

    for (size_t i = 0; i < path.size(); ++i) {
        char c = path[i];
        if (c == '/' || c == '\\') {
            if (!current.empty()) {
                if (current == "..") {
                    if (!parts.empty() && parts.back() != "..") {
                        parts.pop_back();
                    } else if (!is_abs) {
                        parts.push_back("..");
                    }
                } else if (current != ".") {
                    parts.push_back(current);
                }
                current.clear();
            }
        } else {
            current += c;
        }
    }

    // Handle last component
    if (!current.empty()) {
        if (current == "..") {
            if (!parts.empty() && parts.back() != "..") {
                parts.pop_back();
            } else if (!is_abs) {
                parts.push_back("..");
            }
        } else if (current != ".") {
            parts.push_back(current);
        }
    }

    // Reconstruct path
    std::string result;
    if (is_abs) {
#if defined(_WIN32) || defined(_WIN64)
        if (path.size() >= 2 && path[1] == ':') {
            result = path.substr(0, 2);
            result += PATH_SEP;
        } else {
            result = "\\\\";
        }
#else
        result = "/";
#endif
    }

    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) result += PATH_SEP;
        result += parts[i];
    }

    return result.empty() ? "." : result;
}

bool file_exists(const std::string& path) {
    std::ifstream f(path);
    return f.good();
}

std::string get_extension(const std::string& path) {
    size_t dot = path.rfind('.');
    if (dot == std::string::npos) return "";
    size_t sep = path.find_last_of("/\\");
    if (sep != std::string::npos && dot < sep) return "";
    return path.substr(dot);
}

} // anonymous namespace

// ============================================================================
// LayerRegistry::Impl
// ============================================================================

class LayerRegistry::Impl {
public:
    std::map<std::string, std::unique_ptr<Layer>> layers_;
    std::vector<std::string> search_paths_;

    Result<Layer*> load_layer(const std::string& resolved_path) {
        // Check file extension
        std::string ext = get_extension(resolved_path);

        // Convert to lowercase for comparison
        std::string ext_lower = ext;
        for (auto& c : ext_lower) {
            if (c >= 'A' && c <= 'Z') c = c - 'A' + 'a';
        }

        // Create new layer
        auto layer = std::make_unique<Layer>();
        layer->set_identifier(resolved_path);
        layer->set_real_path(resolved_path);

        if (ext_lower == ".usda" || ext_lower == ".usd") {
            // Try USDA first
            auto result = read_usda_file(resolved_path);
            if (result.ok()) {
                // Transfer data from Stage to Layer
                // For now, copy root prims
                const Stage& stage = result.stage;
                for (size_t i = 0; i < stage.root_prim_count(); ++i) {
                    const Prim* prim = stage.root_prim(i);
                    if (prim) {
                        layer->add_root_prim(*prim);
                    }
                }

                // Copy sublayer paths from stage
                for (size_t i = 0; i < stage.sublayer_count(); ++i) {
                    // Stage stores SubLayer with offset, we just need path
                    // For now we can't easily get sublayer paths from Stage
                    // This would need Stage API enhancement
                }

                // Store in registry
                Layer* ptr = layer.get();
                layers_[resolved_path] = std::move(layer);
                return ptr;
            }

            // If .usd, try USDC
            if (ext_lower == ".usd") {
                UsdcReader reader;
                auto usdc_result = reader.read_file(resolved_path);
                if (usdc_result) {
                    // USDC reader doesn't directly produce a Stage/Layer
                    // We'd need to reconstruct prims from specs
                    // For now, return empty layer with basic info
                    Layer* ptr = layer.get();
                    layers_[resolved_path] = std::move(layer);
                    return ptr;
                }
            }

            return Error("Failed to read USDA file: " + resolved_path);
        }
        else if (ext_lower == ".usdc") {
            // Read USDC
            UsdcReader reader;
            auto usdc_result = reader.read_file(resolved_path);
            if (!usdc_result) {
                return Error("Failed to read USDC file: " + reader.error());
            }

            // For now, store basic layer info
            // Full USDC → Layer conversion would require spec reconstruction
            Layer* ptr = layer.get();
            layers_[resolved_path] = std::move(layer);
            return ptr;
        }
        else {
            return Error("Unknown file extension: " + ext);
        }
    }
};

// ============================================================================
// LayerRegistry public interface
// ============================================================================

LayerRegistry::LayerRegistry() : impl_(new Impl()) {}

LayerRegistry::~LayerRegistry() = default;

LayerRegistry::LayerRegistry(LayerRegistry&&) noexcept = default;
LayerRegistry& LayerRegistry::operator=(LayerRegistry&&) noexcept = default;

Result<Layer*> LayerRegistry::find_or_open(const std::string& identifier) {
    // Check if already loaded
    auto it = impl_->layers_.find(identifier);
    if (it != impl_->layers_.end()) {
        return it->second.get();
    }

    // Try to resolve and load
    std::string resolved = identifier;
    if (!is_absolute_path(identifier)) {
        // Try search paths
        for (const auto& search_path : impl_->search_paths_) {
            std::string candidate = join_paths(search_path, identifier);
            candidate = normalize_path(candidate);
            if (file_exists(candidate)) {
                resolved = candidate;
                break;
            }
        }
    }

    resolved = normalize_path(resolved);

    // Check again with resolved path
    it = impl_->layers_.find(resolved);
    if (it != impl_->layers_.end()) {
        return it->second.get();
    }

    // Load the layer
    return impl_->load_layer(resolved);
}

Layer* LayerRegistry::find(const std::string& identifier) const {
    auto it = impl_->layers_.find(identifier);
    if (it != impl_->layers_.end()) {
        return it->second.get();
    }
    return nullptr;
}

bool LayerRegistry::contains(const std::string& identifier) const {
    return impl_->layers_.count(identifier) > 0;
}

std::string LayerRegistry::resolve_asset_path(const std::string& asset_path,
                                               const Layer* anchor_layer) const {
    if (asset_path.empty()) return "";

    // If absolute, normalize and return
    if (is_absolute_path(asset_path)) {
        return normalize_path(asset_path);
    }

    // If we have an anchor layer, resolve relative to it
    if (anchor_layer && !anchor_layer->real_path().empty()) {
        std::string anchor_dir = get_directory(anchor_layer->real_path());
        std::string candidate = join_paths(anchor_dir, asset_path);
        candidate = normalize_path(candidate);
        if (file_exists(candidate)) {
            return candidate;
        }
    }

    // Try search paths
    for (const auto& search_path : impl_->search_paths_) {
        std::string candidate = join_paths(search_path, asset_path);
        candidate = normalize_path(candidate);
        if (file_exists(candidate)) {
            return candidate;
        }
    }

    // Return normalized original if nothing found
    return normalize_path(asset_path);
}

Result<void> LayerRegistry::reload(const std::string& identifier) {
    // Remove existing
    auto it = impl_->layers_.find(identifier);
    if (it == impl_->layers_.end()) {
        return Error("Layer not found: " + identifier);
    }

    impl_->layers_.erase(it);

    // Reload
    auto result = find_or_open(identifier);
    if (!result) {
        return result.error();
    }

    return {};
}

bool LayerRegistry::remove(const std::string& identifier) {
    return impl_->layers_.erase(identifier) > 0;
}

std::vector<std::string> LayerRegistry::get_all_identifiers() const {
    std::vector<std::string> result;
    result.reserve(impl_->layers_.size());
    for (const auto& pair : impl_->layers_) {
        result.push_back(pair.first);
    }
    return result;
}

std::vector<Layer*> LayerRegistry::get_all_layers() const {
    std::vector<Layer*> result;
    result.reserve(impl_->layers_.size());
    for (const auto& pair : impl_->layers_) {
        result.push_back(pair.second.get());
    }
    return result;
}

size_t LayerRegistry::size() const {
    return impl_->layers_.size();
}

void LayerRegistry::clear() {
    impl_->layers_.clear();
}

void LayerRegistry::add_search_paths(const std::vector<std::string>& paths) {
    for (const auto& path : paths) {
        impl_->search_paths_.push_back(normalize_path(path));
    }
}

void LayerRegistry::set_search_paths(const std::vector<std::string>& paths) {
    impl_->search_paths_.clear();
    for (const auto& path : paths) {
        impl_->search_paths_.push_back(normalize_path(path));
    }
}

const std::vector<std::string>& LayerRegistry::search_paths() const {
    return impl_->search_paths_;
}

} // namespace v1
} // namespace lightusd
