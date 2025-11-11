// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 Syoyo Fujita
// Copyright 2024 Light Transport Entertainment Inc.
//
// PCP Compose Site Implementation - Site-specific composition helpers

#include "pcp-compose-site.hh"
#include "pcp-layer-stack.hh"
#include "layer.hh"
#include "prim-types.hh"
#include <algorithm>
#include <regex>
#include <filesystem>

namespace tinyusdz {
namespace tydra {
namespace pcp {

// Helper to apply list editing
template<typename T>
static std::vector<T> ApplyListEditingInternal(
    const std::vector<std::pair<ListEditOp, std::vector<T>>>& edits) {

    std::vector<T> result;
    std::unordered_set<T> deleted;

    for (const auto& [op, items] : edits) {
        switch (op) {
            case ListEditOp::Explicit:
                // Replace entire list
                result = items;
                deleted.clear();
                break;

            case ListEditOp::Prepend:
                // Add items to beginning (in reverse order to maintain order)
                for (auto it = items.rbegin(); it != items.rend(); ++it) {
                    // Remove if already exists
                    auto existing = std::find(result.begin(), result.end(), *it);
                    if (existing != result.end()) {
                        result.erase(existing);
                    }
                    // Skip if deleted
                    if (deleted.find(*it) == deleted.end()) {
                        result.insert(result.begin(), *it);
                    }
                }
                break;

            case ListEditOp::Append:
                // Add items to end
                for (const auto& item : items) {
                    // Remove if already exists
                    auto existing = std::find(result.begin(), result.end(), item);
                    if (existing != result.end()) {
                        result.erase(existing);
                    }
                    // Skip if deleted
                    if (deleted.find(item) == deleted.end()) {
                        result.push_back(item);
                    }
                }
                break;

            case ListEditOp::Delete:
                // Mark items as deleted and remove from result
                for (const auto& item : items) {
                    deleted.insert(item);
                    result.erase(
                        std::remove(result.begin(), result.end(), item),
                        result.end());
                }
                break;

            case ListEditOp::Order:
                // Reorder items - keep only those in the new order
                std::vector<T> reordered;
                std::unordered_set<T> seen;

                // Add items from order list that exist in result
                for (const auto& item : items) {
                    if (std::find(result.begin(), result.end(), item) != result.end()) {
                        reordered.push_back(item);
                        seen.insert(item);
                    }
                }

                // Add remaining items not in order list
                for (const auto& item : result) {
                    if (seen.find(item) == seen.end()) {
                        reordered.push_back(item);
                    }
                }

                result = std::move(reordered);
                break;
        }
    }

    return result;
}

// Compose references at a site
std::vector<Reference> ComposeSiteReferences(
    const LayerStackPtr& layer_stack,
    const Path& path,
    std::unordered_set<std::string>* expr_vars_used) {

    if (!layer_stack) {
        return {};
    }

    std::vector<std::pair<ListEditOp, std::vector<Reference>>> edits;

    // Collect edits from weakest to strongest
    auto layers = layer_stack->GetLayers();
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
        if (!it->layer) continue;

        auto prim = it->layer->GetPrim(path);
        if (!prim) continue;

        // Check for explicit references
        if (prim->HasField("references")) {
            auto refs = prim->GetField<std::vector<Reference>>("references");
            if (refs.has_value()) {
                edits.push_back({ListEditOp::Explicit, refs.value()});
            }
        }

        // Check for list edits
        if (prim->HasField("references:prepend")) {
            auto refs = prim->GetField<std::vector<Reference>>("references:prepend");
            if (refs.has_value()) {
                edits.push_back({ListEditOp::Prepend, refs.value()});
            }
        }

        if (prim->HasField("references:append")) {
            auto refs = prim->GetField<std::vector<Reference>>("references:append");
            if (refs.has_value()) {
                edits.push_back({ListEditOp::Append, refs.value()});
            }
        }

        if (prim->HasField("references:delete")) {
            auto refs = prim->GetField<std::vector<Reference>>("references:delete");
            if (refs.has_value()) {
                edits.push_back({ListEditOp::Delete, refs.value()});
            }
        }
    }

    auto result = ApplyListEditingInternal(edits);

    // Resolve expression variables in asset paths
    if (expr_vars_used) {
        for (auto& ref : result) {
            auto resolved = ResolveExpressionVariables(
                ref.asset_path,
                layer_stack->GetExpressionVariables(),
                expr_vars_used);
            ref.asset_path = resolved;
        }
    }

    return result;
}

// Compose payloads at a site
std::vector<Payload> ComposeSitePayloads(
    const LayerStackPtr& layer_stack,
    const Path& path,
    std::unordered_set<std::string>* expr_vars_used) {

    if (!layer_stack) {
        return {};
    }

    std::vector<std::pair<ListEditOp, std::vector<Payload>>> edits;

    // Collect edits from weakest to strongest
    auto layers = layer_stack->GetLayers();
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
        if (!it->layer) continue;

        auto prim = it->layer->GetPrim(path);
        if (!prim) continue;

        // Check for payload field (can be single or list)
        if (prim->HasField("payload")) {
            // Try as single payload first
            auto single = prim->GetField<Payload>("payload");
            if (single.has_value()) {
                edits.push_back({ListEditOp::Explicit, {single.value()}});
            } else {
                // Try as list
                auto list = prim->GetField<std::vector<Payload>>("payload");
                if (list.has_value()) {
                    edits.push_back({ListEditOp::Explicit, list.value()});
                }
            }
        }

        // Check for list edits
        if (prim->HasField("payload:prepend")) {
            auto payloads = prim->GetField<std::vector<Payload>>("payload:prepend");
            if (payloads.has_value()) {
                edits.push_back({ListEditOp::Prepend, payloads.value()});
            }
        }

        if (prim->HasField("payload:append")) {
            auto payloads = prim->GetField<std::vector<Payload>>("payload:append");
            if (payloads.has_value()) {
                edits.push_back({ListEditOp::Append, payloads.value()});
            }
        }

        if (prim->HasField("payload:delete")) {
            auto payloads = prim->GetField<std::vector<Payload>>("payload:delete");
            if (payloads.has_value()) {
                edits.push_back({ListEditOp::Delete, payloads.value()});
            }
        }
    }

    auto result = ApplyListEditingInternal(edits);

    // Resolve expression variables
    if (expr_vars_used) {
        for (auto& payload : result) {
            auto resolved = ResolveExpressionVariables(
                payload.asset_path,
                layer_stack->GetExpressionVariables(),
                expr_vars_used);
            payload.asset_path = resolved;
        }
    }

    return result;
}

// Compose inherits at a site
std::vector<Path> ComposeSiteInherits(
    const LayerStackPtr& layer_stack,
    const Path& path) {

    if (!layer_stack) {
        return {};
    }

    std::vector<std::pair<ListEditOp, std::vector<Path>>> edits;

    // Collect edits from weakest to strongest
    auto layers = layer_stack->GetLayers();
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
        if (!it->layer) continue;

        auto prim = it->layer->GetPrim(path);
        if (!prim) continue;

        // Check for inherits
        if (prim->HasField("inherits")) {
            auto inherits = prim->GetField<std::vector<Path>>("inherits");
            if (inherits.has_value()) {
                edits.push_back({ListEditOp::Explicit, inherits.value()});
            }
        }

        // Check for list edits
        if (prim->HasField("inherits:prepend")) {
            auto inherits = prim->GetField<std::vector<Path>>("inherits:prepend");
            if (inherits.has_value()) {
                edits.push_back({ListEditOp::Prepend, inherits.value()});
            }
        }

        if (prim->HasField("inherits:append")) {
            auto inherits = prim->GetField<std::vector<Path>>("inherits:append");
            if (inherits.has_value()) {
                edits.push_back({ListEditOp::Append, inherits.value()});
            }
        }

        if (prim->HasField("inherits:delete")) {
            auto inherits = prim->GetField<std::vector<Path>>("inherits:delete");
            if (inherits.has_value()) {
                edits.push_back({ListEditOp::Delete, inherits.value()});
            }
        }
    }

    return ApplyListEditingInternal(edits);
}

// Compose specializes at a site
std::vector<Path> ComposeSiteSpecializes(
    const LayerStackPtr& layer_stack,
    const Path& path) {

    if (!layer_stack) {
        return {};
    }

    std::vector<std::pair<ListEditOp, std::vector<Path>>> edits;

    // Collect edits from weakest to strongest
    auto layers = layer_stack->GetLayers();
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
        if (!it->layer) continue;

        auto prim = it->layer->GetPrim(path);
        if (!prim) continue;

        // Check for specializes
        if (prim->HasField("specializes")) {
            auto specs = prim->GetField<std::vector<Path>>("specializes");
            if (specs.has_value()) {
                edits.push_back({ListEditOp::Explicit, specs.value()});
            }
        }

        // Check for list edits
        if (prim->HasField("specializes:prepend")) {
            auto specs = prim->GetField<std::vector<Path>>("specializes:prepend");
            if (specs.has_value()) {
                edits.push_back({ListEditOp::Prepend, specs.value()});
            }
        }

        if (prim->HasField("specializes:append")) {
            auto specs = prim->GetField<std::vector<Path>>("specializes:append");
            if (specs.has_value()) {
                edits.push_back({ListEditOp::Append, specs.value()});
            }
        }

        if (prim->HasField("specializes:delete")) {
            auto specs = prim->GetField<std::vector<Path>>("specializes:delete");
            if (specs.has_value()) {
                edits.push_back({ListEditOp::Delete, specs.value()});
            }
        }
    }

    return ApplyListEditingInternal(edits);
}

// Compose variant sets at a site
std::vector<std::string> ComposeSiteVariantSets(
    const LayerStackPtr& layer_stack,
    const Path& path) {

    if (!layer_stack) {
        return {};
    }

    std::unordered_set<std::string> variant_sets;

    // Collect from all layers
    for (const auto& entry : layer_stack->GetLayers()) {
        if (!entry.layer) continue;

        auto prim = entry.layer->GetPrim(path);
        if (!prim) continue;

        auto sets = prim->GetVariantSetNames();
        variant_sets.insert(sets.begin(), sets.end());
    }

    return std::vector<std::string>(variant_sets.begin(), variant_sets.end());
}

// Compose variant selection for a specific set
std::optional<std::string> ComposeSiteVariantSelection(
    const LayerStackPtr& layer_stack,
    const Path& path,
    const std::string& variant_set_name) {

    if (!layer_stack) {
        return std::nullopt;
    }

    // Check from strongest to weakest
    for (const auto& entry : layer_stack->GetLayers()) {
        if (!entry.layer) continue;

        auto prim = entry.layer->GetPrim(path);
        if (!prim) continue;

        auto selections = prim->GetVariantSelections();
        auto it = selections.find(variant_set_name);
        if (it != selections.end()) {
            return it->second;
        }
    }

    return std::nullopt;
}

// Compose all variant selections
VariantSelectionMap ComposeSiteVariantSelections(
    const LayerStackPtr& layer_stack,
    const Path& path) {

    if (!layer_stack) {
        return {};
    }

    VariantSelectionMap result;

    // Compose from weakest to strongest
    auto layers = layer_stack->GetLayers();
    for (auto it = layers.rbegin(); it != layers.rend(); ++it) {
        if (!it->layer) continue;

        auto prim = it->layer->GetPrim(path);
        if (!prim) continue;

        auto selections = prim->GetVariantSelections();
        for (const auto& [set_name, selection] : selections) {
            result[set_name] = selection;
        }
    }

    return result;
}

// Compose relocates at a site
Relocates ComposeSiteRelocates(
    const LayerStackPtr& layer_stack,
    const Path& path) {

    if (!layer_stack) {
        return {};
    }

    return layer_stack->GetRelocates();
}

// Compose permission at a site
std::optional<Permission> ComposeSitePermission(
    const LayerStackPtr& layer_stack,
    const Path& path) {

    if (!layer_stack) {
        return std::nullopt;
    }

    for (const auto& entry : layer_stack->GetLayers()) {
        if (!entry.layer) continue;

        auto prim = entry.layer->GetPrim(path);
        if (!prim) continue;

        if (prim->HasField("permission")) {
            auto perm = prim->GetField<std::string>("permission");
            if (perm.has_value()) {
                if (perm.value() == "public") {
                    return Permission::Public;
                } else if (perm.value() == "private") {
                    return Permission::Private;
                } else if (perm.value() == "protected") {
                    return Permission::Protected;
                }
            }
        }
    }

    return std::nullopt;
}

// Compose active state
std::optional<bool> ComposeSiteActive(
    const LayerStackPtr& layer_stack,
    const Path& path) {

    if (!layer_stack) {
        return std::nullopt;
    }

    for (const auto& entry : layer_stack->GetLayers()) {
        if (!entry.layer) continue;

        auto prim = entry.layer->GetPrim(path);
        if (!prim) continue;

        if (prim->HasField("active")) {
            auto active = prim->GetField<bool>("active");
            if (active.has_value()) {
                return active.value();
            }
        }
    }

    return std::nullopt;
}

// Compose instanceable state
std::optional<bool> ComposeSiteInstanceable(
    const LayerStackPtr& layer_stack,
    const Path& path) {

    if (!layer_stack) {
        return std::nullopt;
    }

    for (const auto& entry : layer_stack->GetLayers()) {
        if (!entry.layer) continue;

        auto prim = entry.layer->GetPrim(path);
        if (!prim) continue;

        if (prim->HasField("instanceable")) {
            auto inst = prim->GetField<bool>("instanceable");
            if (inst.has_value()) {
                return inst.value();
            }
        }
    }

    return std::nullopt;
}

// Compose kind
std::optional<Kind> ComposeSiteKind(
    const LayerStackPtr& layer_stack,
    const Path& path) {

    if (!layer_stack) {
        return std::nullopt;
    }

    for (const auto& entry : layer_stack->GetLayers()) {
        if (!entry.layer) continue;

        auto prim = entry.layer->GetPrim(path);
        if (!prim) continue;

        if (prim->HasField("kind")) {
            auto kind = prim->GetField<std::string>("kind");
            if (kind.has_value()) {
                return ParseKind(kind.value());
            }
        }
    }

    return std::nullopt;
}

// Compose purpose
std::optional<Purpose> ComposeSitePurpose(
    const LayerStackPtr& layer_stack,
    const Path& path) {

    if (!layer_stack) {
        return std::nullopt;
    }

    for (const auto& entry : layer_stack->GetLayers()) {
        if (!entry.layer) continue;

        auto prim = entry.layer->GetPrim(path);
        if (!prim) continue;

        if (prim->HasField("purpose")) {
            auto purpose = prim->GetField<std::string>("purpose");
            if (purpose.has_value()) {
                return ParsePurpose(purpose.value());
            }
        }
    }

    return std::nullopt;
}

// Compose child names
std::vector<std::string> ComposeSiteChildNames(
    const LayerStackPtr& layer_stack,
    const Path& path,
    bool include_deleted) {

    if (!layer_stack) {
        return {};
    }

    std::vector<std::pair<ListEditOp, std::vector<std::string>>> edits;
    std::unordered_set<std::string> all_children;

    // First collect all children that exist
    for (const auto& entry : layer_stack->GetLayers()) {
        if (!entry.layer) continue;

        auto prim = entry.layer->GetPrim(path);
        if (!prim) continue;

        auto children = prim->GetChildrenNames();
        all_children.insert(children.begin(), children.end());
    }

    // Now collect reorder operations
    for (auto it = layer_stack->GetLayers().rbegin();
         it != layer_stack->GetLayers().rend(); ++it) {

        if (!it->layer) continue;

        auto prim = it->layer->GetPrim(path);
        if (!prim) continue;

        if (prim->HasField("primChildren")) {
            auto order = prim->GetField<std::vector<std::string>>("primChildren");
            if (order.has_value()) {
                edits.push_back({ListEditOp::Order, order.value()});
            }
        }

        if (!include_deleted && prim->HasField("primChildren:delete")) {
            auto deleted = prim->GetField<std::vector<std::string>>("primChildren:delete");
            if (deleted.has_value()) {
                edits.push_back({ListEditOp::Delete, deleted.value()});
            }
        }
    }

    // Start with all children
    std::vector<std::string> result(all_children.begin(), all_children.end());

    // Apply list editing operations
    if (!edits.empty()) {
        std::vector<std::pair<ListEditOp, std::vector<std::string>>> full_edits;
        full_edits.push_back({ListEditOp::Explicit, result});
        full_edits.insert(full_edits.end(), edits.begin(), edits.end());
        result = ApplyListEditingInternal(full_edits);
    }

    return result;
}

// Compose property names
std::vector<std::string> ComposeSitePropertyNames(
    const LayerStackPtr& layer_stack,
    const Path& path,
    bool include_deleted) {

    if (!layer_stack) {
        return {};
    }

    std::unordered_set<std::string> properties;

    for (const auto& entry : layer_stack->GetLayers()) {
        if (!entry.layer) continue;

        auto prim = entry.layer->GetPrim(path);
        if (!prim) continue;

        auto props = prim->GetPropertyNames();
        properties.insert(props.begin(), props.end());
    }

    // TODO: Handle property deletion if needed

    return std::vector<std::string>(properties.begin(), properties.end());
}

// Check if site has any specs
bool SiteHasSpecs(
    const LayerStackPtr& layer_stack,
    const Path& path) {

    if (!layer_stack) {
        return false;
    }

    return layer_stack->HasSpecs(path);
}

// Check if site has primSpec
bool SiteHasPrimSpec(
    const LayerStackPtr& layer_stack,
    const Path& path) {

    return SiteHasSpecs(layer_stack, path);
}

// Get prim spec type at site
std::optional<SpecType> GetSitePrimSpecType(
    const LayerStackPtr& layer_stack,
    const Path& path) {

    if (!layer_stack) {
        return std::nullopt;
    }

    for (const auto& entry : layer_stack->GetLayers()) {
        if (!entry.layer) continue;

        auto prim = entry.layer->GetPrim(path);
        if (!prim) continue;

        return prim->GetSpecType();
    }

    return std::nullopt;
}

// Check if prim is defined at site
bool IsPrimDefinedAtSite(
    const LayerStackPtr& layer_stack,
    const Path& path) {

    auto spec_type = GetSitePrimSpecType(layer_stack, path);
    return spec_type.has_value() && spec_type.value() == SpecType::Def;
}

// Check if prim is abstract at site
bool IsPrimAbstractAtSite(
    const LayerStackPtr& layer_stack,
    const Path& path) {

    auto spec_type = GetSitePrimSpecType(layer_stack, path);
    return spec_type.has_value() && spec_type.value() == SpecType::Class;
}

// Compose arguments for dynamic file formats
FileFormatArgs ComposeSiteFileFormatArgs(
    const LayerStackPtr& layer_stack,
    const Path& path,
    const std::string& field_prefix) {

    FileFormatArgs args;

    if (!layer_stack) {
        return args;
    }

    // Look for fields like "payload:args:*" or "reference:args:*"
    std::string prefix = field_prefix + ":args:";

    for (const auto& entry : layer_stack->GetLayers()) {
        if (!entry.layer) continue;

        auto prim = entry.layer->GetPrim(path);
        if (!prim) continue;

        // Get all fields and filter by prefix
        auto fields = prim->GetAllFieldNames();
        for (const auto& field : fields) {
            if (field.find(prefix) == 0) {
                std::string arg_name = field.substr(prefix.length());
                args[arg_name] = prim->GetField(field);
            }
        }
    }

    return args;
}

// Check if payload uses dynamic file format
bool IsPayloadDynamic(const Payload& payload) {
    // Check if asset path has file format arguments syntax
    // e.g., "@procedural.dll:main@"
    return payload.asset_path.find(':') != std::string::npos;
}

// Resolve dynamic payload
Payload ResolveDynamicPayload(
    const Payload& payload,
    const FileFormatArgs& args) {

    Payload resolved = payload;

    // TODO: Apply file format arguments to generate content
    // This would involve calling into the dynamic file format plugin

    return resolved;
}

// Resolve expression variables in string
std::string ResolveExpressionVariables(
    const std::string& str,
    const std::unordered_map<std::string, std::string>& variables,
    std::unordered_set<std::string>* vars_used) {

    if (!ContainsExpressionVariables(str)) {
        return str;
    }

    std::string result = str;
    std::regex var_regex(R"(\{([^}]+)\})");
    std::smatch match;

    std::string temp = str;
    while (std::regex_search(temp, match, var_regex)) {
        std::string var_name = match[1];

        auto it = variables.find(var_name);
        if (it != variables.end()) {
            // Replace variable with value
            result = std::regex_replace(result,
                                      std::regex("\\{" + var_name + "\\}"),
                                      it->second);

            if (vars_used) {
                vars_used->insert(var_name);
            }
        }

        temp = match.suffix();
    }

    return result;
}

// Check if string contains expression variables
bool ContainsExpressionVariables(const std::string& str) {
    return str.find('{') != std::string::npos &&
           str.find('}') != std::string::npos;
}

// Extract expression variable names from string
std::unordered_set<std::string> ExtractExpressionVariables(
    const std::string& str) {

    std::unordered_set<std::string> vars;

    if (!ContainsExpressionVariables(str)) {
        return vars;
    }

    std::regex var_regex(R"(\{([^}]+)\})");
    std::smatch match;

    std::string temp = str;
    while (std::regex_search(temp, match, var_regex)) {
        vars.insert(match[1]);
        temp = match.suffix();
    }

    return vars;
}

// Resolve asset path
std::string ResolveAssetPath(
    const std::string& asset_path,
    const Layer* anchor_layer,
    const std::vector<std::string>& search_paths) {

    if (asset_path.empty()) {
        return "";
    }

    // Check if it's an absolute path
    std::filesystem::path path(asset_path);
    if (path.is_absolute() && std::filesystem::exists(path)) {
        return asset_path;
    }

    // Try relative to anchor layer
    if (anchor_layer) {
        std::filesystem::path anchor_dir =
            std::filesystem::path(anchor_layer->GetIdentifier()).parent_path();
        std::filesystem::path resolved = anchor_dir / asset_path;

        if (std::filesystem::exists(resolved)) {
            return resolved.string();
        }
    }

    // Try search paths
    for (const auto& search_path : search_paths) {
        std::filesystem::path resolved =
            std::filesystem::path(search_path) / asset_path;

        if (std::filesystem::exists(resolved)) {
            return resolved.string();
        }
    }

    // Return as-is if not found
    return asset_path;
}

// Check if asset path is resolvable
bool IsAssetPathResolvable(
    const std::string& asset_path,
    const Layer* anchor_layer,
    const std::vector<std::string>& search_paths) {

    std::string resolved = ResolveAssetPath(asset_path, anchor_layer, search_paths);
    return std::filesystem::exists(resolved);
}

// PrimSpec field access
bool PrimSpec::HasField(const std::string& field_name) const {
    if (!layer) return false;

    auto prim = layer->GetPrim(path);
    return prim && prim->HasField(field_name);
}

value::Value PrimSpec::GetField(const std::string& field_name) const {
    if (!layer) return value::Value();

    auto prim = layer->GetPrim(path);
    return prim ? prim->GetField(field_name) : value::Value();
}

} // namespace pcp
} // namespace tydra
} // namespace tinyusdz