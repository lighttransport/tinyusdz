// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 Syoyo Fujita
// Copyright 2024 Light Transport Entertainment Inc.
//
// PCP Map Function Implementation - Path and value translation

#include "pcp-map-function.hh"
#include <algorithm>
#include <sstream>
#include <functional>

namespace tinyusdz {
namespace tydra {
namespace pcp {

// MapFunction implementation

MapFunction::MapFunction()
    : is_identity_(true)
    , is_null_(false)
    , is_invertible_(true) {
}

MapFunction::~MapFunction() = default;

MapFunctionPtr MapFunction::CreateIdentity() {
    return std::make_shared<MapFunction>();
}

MapFunctionPtr MapFunction::CreatePathMap(
    const Path& source_path,
    const Path& target_path) {

    auto map = std::make_shared<MapFunction>();
    map->is_identity_ = false;

    // Add the path mapping
    map->AddPathMapping(source_path, target_path);
    map->BuildMappingTables();

    return map;
}

MapFunctionPtr MapFunction::CreateWithRelocates(
    const Relocates& relocates) {

    if (relocates.empty()) {
        return CreateIdentity();
    }

    auto map = std::make_shared<MapFunction>();
    map->is_identity_ = false;
    map->relocates_ = relocates;

    // Build mapping tables from relocates
    for (const auto& relocate : relocates) {
        map->AddPathMapping(relocate.source, relocate.target);
    }
    map->BuildMappingTables();

    return map;
}

MapFunctionPtr MapFunction::CreateWithLayerOffset(
    const LayerOffset& offset) {

    if (offset.IsIdentity()) {
        return CreateIdentity();
    }

    auto map = std::make_shared<MapFunction>();
    map->time_offset_ = offset;
    map->is_identity_ = false;

    return map;
}

MapFunctionPtr MapFunction::CreateComposite(
    MapFunctionPtr outer,
    MapFunctionPtr inner) {

    if (!outer || !inner) {
        return CreateIdentity();
    }

    if (outer->IsIdentity()) {
        return inner;
    }

    if (inner->IsIdentity()) {
        return outer;
    }

    return outer->Compose(inner);
}

bool MapFunction::IsIdentity() const {
    return is_identity_ &&
           path_mappings_.empty() &&
           relocates_.empty() &&
           time_offset_.IsIdentity();
}

bool MapFunction::IsNull() const {
    return is_null_;
}

Path MapFunction::MapPath(const Path& path) const {
    if (IsIdentity()) {
        return path;
    }

    if (IsNull()) {
        return Path();
    }

    // First check direct mapping
    auto it = forward_map_.find(path);
    if (it != forward_map_.end()) {
        return it->second;
    }

    // Then check prefix mapping
    for (const auto& mapping : path_mappings_) {
        if (path.HasPrefix(mapping.source)) {
            // Compute relative path
            std::string path_str = path.full_path_name();
            std::string source_str = mapping.source.full_path_name();
            std::string target_str = mapping.target.full_path_name();

            // Replace source prefix with target
            if (path_str.find(source_str) == 0) {
                path_str = target_str + path_str.substr(source_str.length());
                return Path(path_str);
            }
        }
    }

    // Apply relocates if any
    Path result = ApplyRelocates(path);

    return result;
}

Path MapFunction::MapPathReverse(const Path& path) const {
    if (IsIdentity()) {
        return path;
    }

    if (IsNull() || !is_invertible_) {
        return Path();
    }

    // First check direct reverse mapping
    auto it = reverse_map_.find(path);
    if (it != reverse_map_.end()) {
        return it->second;
    }

    // Then check prefix mapping in reverse
    for (const auto& mapping : path_mappings_) {
        if (path.HasPrefix(mapping.target)) {
            // Compute relative path
            std::string path_str = path.full_path_name();
            std::string target_str = mapping.target.full_path_name();
            std::string source_str = mapping.source.full_path_name();

            // Replace target prefix with source
            if (path_str.find(target_str) == 0) {
                path_str = source_str + path_str.substr(target_str.length());
                return Path(path_str);
            }
        }
    }

    // Apply relocates in reverse
    Path result = ApplyRelocatesReverse(path);

    return result;
}

bool MapFunction::IsInDomain(const Path& path) const {
    if (IsIdentity()) {
        return true;
    }

    if (IsNull()) {
        return false;
    }

    // Check if path can be mapped
    for (const auto& mapping : path_mappings_) {
        if (path == mapping.source || path.HasPrefix(mapping.source)) {
            return true;
        }
    }

    // Check relocates
    for (const auto& relocate : relocates_) {
        if (path == relocate.source || path.HasPrefix(relocate.source)) {
            return true;
        }
    }

    return path_mappings_.empty() && relocates_.empty();
}

bool MapFunction::IsInRange(const Path& path) const {
    if (IsIdentity()) {
        return true;
    }

    if (IsNull()) {
        return false;
    }

    // Check if path is in range of mapping
    for (const auto& mapping : path_mappings_) {
        if (path == mapping.target || path.HasPrefix(mapping.target)) {
            return true;
        }
    }

    // Check relocates
    for (const auto& relocate : relocates_) {
        if (path == relocate.target || path.HasPrefix(relocate.target)) {
            return true;
        }
    }

    return path_mappings_.empty() && relocates_.empty();
}

std::vector<Path> MapFunction::GetDomain() const {
    std::vector<Path> domain;

    for (const auto& mapping : path_mappings_) {
        domain.push_back(mapping.source);
    }

    for (const auto& relocate : relocates_) {
        domain.push_back(relocate.source);
    }

    return domain;
}

std::vector<Path> MapFunction::GetRange() const {
    std::vector<Path> range;

    for (const auto& mapping : path_mappings_) {
        range.push_back(mapping.target);
    }

    for (const auto& relocate : relocates_) {
        range.push_back(relocate.target);
    }

    return range;
}

double MapFunction::MapTime(double time) const {
    return time_offset_.Transform(time);
}

double MapFunction::MapTimeReverse(double time) const {
    if (!is_invertible_) {
        return time;
    }

    LayerOffset inverse = time_offset_.GetInverse();
    return inverse.Transform(time);
}

value::Value MapFunction::MapValue(const value::Value& value) const {
    // For now, just return the value
    // Could apply transformations based on type
    return value;
}

value::Value MapFunction::MapValueReverse(const value::Value& value) const {
    // For now, just return the value
    return value;
}

MapFunctionPtr MapFunction::Compose(MapFunctionPtr other) const {
    if (!other) {
        return std::const_pointer_cast<MapFunction>(shared_from_this());
    }

    if (IsIdentity()) {
        return other;
    }

    if (other->IsIdentity()) {
        return std::const_pointer_cast<MapFunction>(shared_from_this());
    }

    auto result = std::make_shared<MapFunction>();
    result->is_identity_ = false;

    // Compose path mappings: result(x) = this(other(x))
    // For each mapping in other, apply this mapping to its target
    for (const auto& other_mapping : other->path_mappings_) {
        Path composed_target = MapPath(other_mapping.target);
        result->AddPathMapping(other_mapping.source, composed_target);
    }

    // Also need to handle our mappings that aren't covered by other
    for (const auto& our_mapping : path_mappings_) {
        bool covered = false;
        for (const auto& other_mapping : other->path_mappings_) {
            if (our_mapping.source == other_mapping.target ||
                our_mapping.source.HasPrefix(other_mapping.target)) {
                covered = true;
                break;
            }
        }

        if (!covered) {
            result->AddPathMapping(our_mapping.source, our_mapping.target);
        }
    }

    // Compose time offsets
    result->time_offset_ = time_offset_.ComposeWith(other->time_offset_);

    // Compose relocates
    for (const auto& relocate : other->relocates_) {
        Relocate composed;
        composed.source = relocate.source;
        composed.target = MapPath(relocate.target);
        result->relocates_.push_back(composed);
    }

    for (const auto& relocate : relocates_) {
        result->relocates_.push_back(relocate);
    }

    result->is_invertible_ = is_invertible_ && other->is_invertible_;
    result->BuildMappingTables();

    return result;
}

MapFunctionPtr MapFunction::GetInverse() const {
    if (!is_invertible_) {
        return nullptr;
    }

    if (IsIdentity()) {
        return std::const_pointer_cast<MapFunction>(shared_from_this());
    }

    auto inverse = std::make_shared<MapFunction>();
    inverse->is_identity_ = false;

    // Invert path mappings
    for (const auto& mapping : path_mappings_) {
        inverse->AddPathMapping(mapping.target, mapping.source);
    }

    // Invert time offset
    inverse->time_offset_ = time_offset_.GetInverse();

    // Invert relocates
    for (const auto& relocate : relocates_) {
        Relocate inv;
        inv.source = relocate.target;
        inv.target = relocate.source;
        inverse->relocates_.push_back(inv);
    }

    inverse->BuildMappingTables();

    return inverse;
}

bool MapFunction::IsInvertible() const {
    if (is_null_) {
        return false;
    }

    if (time_offset_.scale == 0.0) {
        return false;
    }

    // Check for one-to-one mappings
    std::unordered_set<Path> seen_targets;
    for (const auto& mapping : path_mappings_) {
        if (seen_targets.count(mapping.target) > 0) {
            return false;  // Multiple sources map to same target
        }
        seen_targets.insert(mapping.target);
    }

    return true;
}

bool MapFunction::IsEquivalentTo(const MapFunction& other) const {
    if (IsIdentity() && other.IsIdentity()) {
        return true;
    }

    if (IsNull() && other.IsNull()) {
        return true;
    }

    if (is_identity_ != other.is_identity_ ||
        is_null_ != other.is_null_ ||
        is_invertible_ != other.is_invertible_) {
        return false;
    }

    if (path_mappings_.size() != other.path_mappings_.size() ||
        relocates_.size() != other.relocates_.size()) {
        return false;
    }

    // Check path mappings
    for (size_t i = 0; i < path_mappings_.size(); ++i) {
        if (path_mappings_[i].source != other.path_mappings_[i].source ||
            path_mappings_[i].target != other.path_mappings_[i].target) {
            return false;
        }
    }

    // Check time offset
    if (time_offset_.offset != other.time_offset_.offset ||
        time_offset_.scale != other.time_offset_.scale) {
        return false;
    }

    return true;
}

size_t MapFunction::GetHash() const {
    if (cached_hash_.has_value()) {
        return cached_hash_.value();
    }

    size_t hash = 0;

    // Hash identity/null flags
    hash ^= std::hash<bool>{}(is_identity_) << 1;
    hash ^= std::hash<bool>{}(is_null_) << 2;

    // Hash path mappings
    for (const auto& mapping : path_mappings_) {
        hash ^= std::hash<std::string>{}(mapping.source.full_path_name());
        hash ^= std::hash<std::string>{}(mapping.target.full_path_name()) << 1;
    }

    // Hash time offset
    hash ^= std::hash<double>{}(time_offset_.offset) << 3;
    hash ^= std::hash<double>{}(time_offset_.scale) << 4;

    cached_hash_ = hash;
    return hash;
}

std::string MapFunction::DumpToString() const {
    std::ostringstream ss;

    if (IsIdentity()) {
        ss << "Identity";
    } else if (IsNull()) {
        ss << "Null";
    } else {
        ss << "MapFunction {\n";

        if (!path_mappings_.empty()) {
            ss << "  Path mappings:\n";
            for (const auto& mapping : path_mappings_) {
                ss << "    " << mapping.source.full_path_name()
                   << " -> " << mapping.target.full_path_name() << "\n";
            }
        }

        if (!relocates_.empty()) {
            ss << "  Relocates:\n";
            for (const auto& relocate : relocates_) {
                ss << "    " << relocate.source.full_path_name()
                   << " -> " << relocate.target.full_path_name() << "\n";
            }
        }

        if (!time_offset_.IsIdentity()) {
            ss << "  Time offset: scale=" << time_offset_.scale
               << ", offset=" << time_offset_.offset << "\n";
        }

        ss << "  Invertible: " << (is_invertible_ ? "yes" : "no") << "\n";
        ss << "}";
    }

    return ss.str();
}

// Private helpers

void MapFunction::AddPathMapping(const Path& source, const Path& target) {
    PathMapping mapping{source, target};
    path_mappings_.push_back(mapping);
}

void MapFunction::BuildMappingTables() {
    forward_map_.clear();
    reverse_map_.clear();

    for (const auto& mapping : path_mappings_) {
        forward_map_[mapping.source] = mapping.target;
        reverse_map_[mapping.target] = mapping.source;
    }
}

Path MapFunction::ApplyRelocates(const Path& path) const {
    for (const auto& relocate : relocates_) {
        if (path == relocate.source) {
            return relocate.target;
        }

        if (path.HasPrefix(relocate.source)) {
            // Apply relocation to path
            std::string path_str = path.full_path_name();
            std::string source_str = relocate.source.full_path_name();
            std::string target_str = relocate.target.full_path_name();

            path_str = target_str + path_str.substr(source_str.length());
            return Path(path_str);
        }
    }

    return path;
}

Path MapFunction::ApplyRelocatesReverse(const Path& path) const {
    for (const auto& relocate : relocates_) {
        if (path == relocate.target) {
            return relocate.source;
        }

        if (path.HasPrefix(relocate.target)) {
            // Apply inverse relocation
            std::string path_str = path.full_path_name();
            std::string target_str = relocate.target.full_path_name();
            std::string source_str = relocate.source.full_path_name();

            path_str = source_str + path_str.substr(target_str.length());
            return Path(path_str);
        }
    }

    return path;
}

// MapExpression implementation

MapExpression::MapExpression()
    : type_(Type::Identity) {
}

MapExpression::~MapExpression() = default;

std::unique_ptr<MapExpression> MapExpression::CreateIdentity() {
    return std::make_unique<MapExpression>();
}

std::unique_ptr<MapExpression> MapExpression::CreateFromMapFunction(
    MapFunctionPtr map) {

    auto expr = std::make_unique<MapExpression>();
    expr->type_ = Type::MapFunction;
    expr->map_function_ = map;
    return expr;
}

std::unique_ptr<MapExpression> MapExpression::CreateConstant(
    const Path& target_path) {

    auto expr = std::make_unique<MapExpression>();
    expr->type_ = Type::Constant;
    expr->constant_path_ = target_path;
    return expr;
}

bool MapExpression::IsIdentity() const {
    return type_ == Type::Identity ||
           (type_ == Type::MapFunction && map_function_ && map_function_->IsIdentity());
}

Path MapExpression::Evaluate(const Path& path) const {
    switch (type_) {
        case Type::Identity:
            return path;

        case Type::MapFunction:
            return map_function_ ? map_function_->MapPath(path) : path;

        case Type::Constant:
            return constant_path_;

        case Type::Variable:
            // Would need variable resolution context
            return path;

        case Type::Composite:
            // Evaluate sub-expressions
            Path result = path;
            for (const auto& sub_expr : sub_expressions_) {
                result = sub_expr->Evaluate(result);
            }
            return result;
    }

    return path;
}

Path MapExpression::EvaluateReverse(const Path& path) const {
    switch (type_) {
        case Type::Identity:
            return path;

        case Type::MapFunction:
            return map_function_ ? map_function_->MapPathReverse(path) : path;

        case Type::Constant:
            // Constant mapping is not reversible
            return Path();

        case Type::Variable:
            return path;

        case Type::Composite:
            // Evaluate sub-expressions in reverse order
            Path result = path;
            for (auto it = sub_expressions_.rbegin(); it != sub_expressions_.rend(); ++it) {
                result = (*it)->EvaluateReverse(result);
            }
            return result;
    }

    return path;
}

std::unique_ptr<MapExpression> MapExpression::Compose(
    const MapExpression& other) const {

    if (IsIdentity()) {
        return other.type_ == Type::MapFunction ?
            CreateFromMapFunction(other.map_function_) :
            CreateConstant(other.constant_path_);
    }

    if (other.IsIdentity()) {
        return type_ == Type::MapFunction ?
            CreateFromMapFunction(map_function_) :
            CreateConstant(constant_path_);
    }

    auto result = std::make_unique<MapExpression>();
    result->type_ = Type::Composite;

    // Add other first (evaluated first)
    if (other.type_ == Type::Composite) {
        for (const auto& sub : other.sub_expressions_) {
            result->sub_expressions_.push_back(
                sub->type_ == Type::MapFunction ?
                CreateFromMapFunction(sub->map_function_) :
                CreateConstant(sub->constant_path_));
        }
    } else {
        result->sub_expressions_.push_back(
            other.type_ == Type::MapFunction ?
            CreateFromMapFunction(other.map_function_) :
            CreateConstant(other.constant_path_));
    }

    // Add this (evaluated second)
    if (type_ == Type::Composite) {
        for (const auto& sub : sub_expressions_) {
            result->sub_expressions_.push_back(
                sub->type_ == Type::MapFunction ?
                CreateFromMapFunction(sub->map_function_) :
                CreateConstant(sub->constant_path_));
        }
    } else {
        result->sub_expressions_.push_back(
            type_ == Type::MapFunction ?
            CreateFromMapFunction(map_function_) :
            CreateConstant(constant_path_));
    }

    return result;
}

MapFunctionPtr MapExpression::GetMapFunction() const {
    if (type_ == Type::MapFunction) {
        return map_function_;
    }

    if (type_ == Type::Identity) {
        return MapFunction::CreateIdentity();
    }

    // Other types can't be represented as simple map functions
    return nullptr;
}

bool MapExpression::IsConstant() const {
    return type_ == Type::Constant;
}

Path MapExpression::GetConstantPath() const {
    return constant_path_;
}

std::vector<std::string> MapExpression::GetVariableDependencies() const {
    std::vector<std::string> deps;

    if (type_ == Type::Variable) {
        deps.push_back(variable_name_);
    } else if (type_ == Type::Composite) {
        for (const auto& sub : sub_expressions_) {
            auto sub_deps = sub->GetVariableDependencies();
            deps.insert(deps.end(), sub_deps.begin(), sub_deps.end());
        }
    }

    return deps;
}

std::string MapExpression::DumpToString() const {
    std::ostringstream ss;

    switch (type_) {
        case Type::Identity:
            ss << "Identity";
            break;

        case Type::MapFunction:
            ss << "MapFunction: " << (map_function_ ? map_function_->DumpToString() : "null");
            break;

        case Type::Constant:
            ss << "Constant: " << constant_path_.full_path_name();
            break;

        case Type::Variable:
            ss << "Variable: " << variable_name_;
            break;

        case Type::Composite:
            ss << "Composite [\n";
            for (const auto& sub : sub_expressions_) {
                ss << "  " << sub->DumpToString() << "\n";
            }
            ss << "]";
            break;
    }

    return ss.str();
}

} // namespace pcp
} // namespace tydra
} // namespace tinyusdz