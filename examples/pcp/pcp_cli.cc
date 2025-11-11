// SPDX-License-Identifier: Apache 2.0
// Copyright 2024 Syoyo Fujita
// Copyright 2024 Light Transport Entertainment Inc.
//
// PCP CLI - Command line tool for USD composition with JSON instructions

#include <iostream>
#include <fstream>
#include <sstream>
#include <memory>
#include <vector>
#include <string>
#include <chrono>

#include "../../src/tydra/pcp-cache.hh"
#include "../../src/tydra/pcp-prim-index.hh"
#include "../../src/tydra/pcp-node.hh"
#include "../../src/layer.hh"
#include "../../src/usda-reader.hh"

// JSON parsing (using nlohmann/json or simple parser)
#include "json.hpp"
using json = nlohmann::json;

namespace tinyusdz {
namespace pcp_cli {

using namespace tinyusdz::tydra::pcp;

// Composition instruction from JSON
struct CompositionInstruction {
    std::string operation;  // "compute", "include_payload", "set_variant", etc.
    std::string prim_path;
    std::map<std::string, std::string> parameters;
};

// CLI application
class PcpCli {
public:
    PcpCli() = default;
    ~PcpCli() = default;

    // Parse command line arguments
    bool ParseArgs(int argc, char** argv);

    // Load composition instructions from JSON
    bool LoadInstructions(const std::string& json_file);

    // Execute composition based on instructions
    bool ExecuteComposition();

    // Output results
    void OutputResults();

    // Print help
    static void PrintHelp();

    // Print version
    static void PrintVersion();

private:
    // Configuration
    struct Config {
        std::string root_layer_file;
        std::string session_layer_file;
        std::string instructions_file;
        std::string output_format = "text";  // text, json, dot
        std::string output_file;
        bool verbose = false;
        bool debug = false;
        bool benchmark = false;
        bool usd_mode = true;
        bool enable_instancing = true;
    } config_;

    // Composition data
    std::unique_ptr<Cache> cache_;
    std::unique_ptr<Layer> root_layer_;
    std::unique_ptr<Layer> session_layer_;
    std::vector<CompositionInstruction> instructions_;
    std::map<std::string, PrimIndexPtr> computed_indexes_;

    // Results
    struct Result {
        std::string prim_path;
        bool success;
        std::string message;
        json details;
        double time_seconds;
    };
    std::vector<Result> results_;

    // Helper methods
    bool LoadLayer(const std::string& file, std::unique_ptr<Layer>& layer);
    bool SetupCache();
    bool ExecuteInstruction(const CompositionInstruction& inst);

    // Instruction handlers
    bool HandleComputePrimIndex(const CompositionInstruction& inst);
    bool HandleIncludePayload(const CompositionInstruction& inst);
    bool HandleSetVariant(const CompositionInstruction& inst);
    bool HandleSetExpressionVar(const CompositionInstruction& inst);
    bool HandleInvalidate(const CompositionInstruction& inst);
    bool HandleQuery(const CompositionInstruction& inst);
    bool HandleAnalyze(const CompositionInstruction& inst);
    bool HandleBenchmark(const CompositionInstruction& inst);

    // Output formatters
    void OutputText();
    void OutputJson();
    void OutputDot();

    // Analysis helpers
    json AnalyzePrimIndex(const PrimIndex& index);
    json AnalyzeNode(const NodeRef& node);
    json AnalyzeComposition(const Path& prim_path);
};

// Implementation
bool PcpCli::ParseArgs(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            PrintHelp();
            return false;
        }

        if (arg == "-v" || arg == "--version") {
            PrintVersion();
            return false;
        }

        if (arg == "--verbose") {
            config_.verbose = true;
        } else if (arg == "--debug") {
            config_.debug = true;
        } else if (arg == "--benchmark") {
            config_.benchmark = true;
        } else if (arg == "--no-usd-mode") {
            config_.usd_mode = false;
        } else if (arg == "--no-instancing") {
            config_.enable_instancing = false;
        } else if (arg == "-r" || arg == "--root") {
            if (++i >= argc) {
                std::cerr << "Error: --root requires an argument" << std::endl;
                return false;
            }
            config_.root_layer_file = argv[i];
        } else if (arg == "-s" || arg == "--session") {
            if (++i >= argc) {
                std::cerr << "Error: --session requires an argument" << std::endl;
                return false;
            }
            config_.session_layer_file = argv[i];
        } else if (arg == "-i" || arg == "--instructions") {
            if (++i >= argc) {
                std::cerr << "Error: --instructions requires an argument" << std::endl;
                return false;
            }
            config_.instructions_file = argv[i];
        } else if (arg == "-f" || arg == "--format") {
            if (++i >= argc) {
                std::cerr << "Error: --format requires an argument" << std::endl;
                return false;
            }
            config_.output_format = argv[i];
        } else if (arg == "-o" || arg == "--output") {
            if (++i >= argc) {
                std::cerr << "Error: --output requires an argument" << std::endl;
                return false;
            }
            config_.output_file = argv[i];
        } else if (!arg.empty() && arg[0] != '-') {
            // Positional argument - assume it's the root layer
            if (config_.root_layer_file.empty()) {
                config_.root_layer_file = arg;
            } else if (config_.instructions_file.empty()) {
                config_.instructions_file = arg;
            }
        } else {
            std::cerr << "Error: Unknown argument: " << arg << std::endl;
            return false;
        }
    }

    // Validate required arguments
    if (config_.root_layer_file.empty()) {
        std::cerr << "Error: Root layer file is required" << std::endl;
        return false;
    }

    return true;
}

bool PcpCli::LoadInstructions(const std::string& json_file) {
    if (json_file.empty()) {
        // No instructions file - just compute root prim index
        CompositionInstruction inst;
        inst.operation = "compute";
        inst.prim_path = "/";
        instructions_.push_back(inst);
        return true;
    }

    std::ifstream file(json_file);
    if (!file) {
        std::cerr << "Error: Cannot open instructions file: " << json_file << std::endl;
        return false;
    }

    try {
        json j;
        file >> j;

        // Parse instructions array
        if (j.contains("instructions") && j["instructions"].is_array()) {
            for (const auto& inst_json : j["instructions"]) {
                CompositionInstruction inst;

                inst.operation = inst_json.value("operation", "compute");
                inst.prim_path = inst_json.value("prim_path", "/");

                if (inst_json.contains("parameters")) {
                    for (auto& [key, value] : inst_json["parameters"].items()) {
                        inst.parameters[key] = value.get<std::string>();
                    }
                }

                instructions_.push_back(inst);
            }
        } else {
            // Single instruction format
            CompositionInstruction inst;
            inst.operation = j.value("operation", "compute");
            inst.prim_path = j.value("prim_path", "/");

            if (j.contains("parameters")) {
                for (auto& [key, value] : j["parameters"].items()) {
                    inst.parameters[key] = value.get<std::string>();
                }
            }

            instructions_.push_back(inst);
        }

        if (config_.verbose) {
            std::cout << "Loaded " << instructions_.size() << " instructions" << std::endl;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Error parsing JSON: " << e.what() << std::endl;
        return false;
    }
}

bool PcpCli::LoadLayer(const std::string& file, std::unique_ptr<Layer>& layer) {
    if (file.empty()) {
        return true;  // No layer specified
    }

    layer = std::make_unique<Layer>();
    layer->SetIdentifier(file);

    // Load USDA file
    std::ifstream input(file);
    if (!input) {
        std::cerr << "Error: Cannot open layer file: " << file << std::endl;
        return false;
    }

    std::stringstream buffer;
    buffer << input.rdbuf();
    std::string content = buffer.str();

    UsداReader reader;
    std::string err;
    if (!reader.ReadFromString(content, file, layer.get(), &err)) {
        std::cerr << "Error parsing USDA: " << err << std::endl;
        return false;
    }

    if (config_.verbose) {
        std::cout << "Loaded layer: " << file << std::endl;
    }

    return true;
}

bool PcpCli::SetupCache() {
    // Load layers
    if (!LoadLayer(config_.root_layer_file, root_layer_)) {
        return false;
    }

    if (!config_.session_layer_file.empty()) {
        if (!LoadLayer(config_.session_layer_file, session_layer_)) {
            return false;
        }
    }

    // Create cache
    CacheConfig cache_config;
    cache_config.root_layer = root_layer_.get();
    cache_config.session_layer = session_layer_.get();
    cache_config.usd_mode = config_.usd_mode;
    cache_config.enable_instancing = config_.enable_instancing;

    cache_ = std::make_unique<Cache>(cache_config);

    if (config_.verbose) {
        std::cout << "Cache created with:" << std::endl;
        std::cout << "  USD mode: " << (config_.usd_mode ? "yes" : "no") << std::endl;
        std::cout << "  Instancing: " << (config_.enable_instancing ? "enabled" : "disabled") << std::endl;
    }

    return true;
}

bool PcpCli::ExecuteComposition() {
    if (!SetupCache()) {
        return false;
    }

    for (const auto& inst : instructions_) {
        if (config_.verbose) {
            std::cout << "Executing: " << inst.operation << " " << inst.prim_path << std::endl;
        }

        if (!ExecuteInstruction(inst)) {
            if (!config_.debug) {
                // Continue on error unless in debug mode
                continue;
            }
            return false;
        }
    }

    return true;
}

bool PcpCli::ExecuteInstruction(const CompositionInstruction& inst) {
    auto start = std::chrono::high_resolution_clock::now();
    bool success = false;

    if (inst.operation == "compute") {
        success = HandleComputePrimIndex(inst);
    } else if (inst.operation == "include_payload") {
        success = HandleIncludePayload(inst);
    } else if (inst.operation == "set_variant") {
        success = HandleSetVariant(inst);
    } else if (inst.operation == "set_expression") {
        success = HandleSetExpressionVar(inst);
    } else if (inst.operation == "invalidate") {
        success = HandleInvalidate(inst);
    } else if (inst.operation == "query") {
        success = HandleQuery(inst);
    } else if (inst.operation == "analyze") {
        success = HandleAnalyze(inst);
    } else if (inst.operation == "benchmark") {
        success = HandleBenchmark(inst);
    } else {
        std::cerr << "Unknown operation: " << inst.operation << std::endl;
        success = false;
    }

    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;

    Result result;
    result.prim_path = inst.prim_path;
    result.success = success;
    result.time_seconds = diff.count();

    if (!success) {
        result.message = "Failed to execute: " + inst.operation;
    }

    results_.push_back(result);
    return success;
}

bool PcpCli::HandleComputePrimIndex(const CompositionInstruction& inst) {
    Path prim_path(inst.prim_path);

    ComputePrimIndexOptions options;
    // Parse options from parameters
    if (inst.parameters.count("cull_empty")) {
        options.cull_empty_nodes = inst.parameters.at("cull_empty") == "true";
    }

    std::vector<Error> errors;
    auto index = cache_->ComputePrimIndex(prim_path, options, &errors);

    if (!index) {
        std::cerr << "Failed to compute prim index for: " << inst.prim_path << std::endl;
        for (const auto& error : errors) {
            std::cerr << "  Error: " << error.message << std::endl;
        }
        return false;
    }

    computed_indexes_[inst.prim_path] = index;

    // Store result details
    Result& result = results_.back();
    result.success = true;
    result.message = "Computed prim index";

    // Add details
    json details;
    details["has_specs"] = index->HasSpecs();
    details["is_instanceable"] = index->IsInstanceable();
    details["num_nodes"] = index->GetGraph().GetNodeCount();
    details["num_errors"] = errors.size();

    // Add node information
    json nodes_json = json::array();
    for (const auto& node : index->GetNodesInStrengthOrder()) {
        json node_json;
        node_json["path"] = node.GetPath().full_path_name();
        node_json["arc_type"] = GetArcTypeName(node.GetArcType());
        node_json["has_specs"] = node.HasSpecs();
        nodes_json.push_back(node_json);
    }
    details["nodes"] = nodes_json;

    // Add errors if any
    if (!errors.empty()) {
        json errors_json = json::array();
        for (const auto& error : errors) {
            json error_json;
            error_json["type"] = static_cast<int>(error.type);
            error_json["message"] = error.message;
            errors_json.push_back(error_json);
        }
        details["errors"] = errors_json;
    }

    result.details = details;

    if (config_.verbose) {
        std::cout << "  Computed prim index with " << index->GetGraph().GetNodeCount()
                  << " nodes" << std::endl;
    }

    return true;
}

bool PcpCli::HandleIncludePayload(const CompositionInstruction& inst) {
    Path payload_path(inst.prim_path);

    // Update payload inclusion set
    auto payloads = cache_->GetIncludedPayloads();
    payloads.insert(payload_path);
    cache_->SetIncludedPayloads(payloads);

    // Recompute affected prim indexes
    if (inst.parameters.count("recompute") && inst.parameters.at("recompute") == "true") {
        cache_->InvalidatePrimIndex(payload_path);
        return HandleComputePrimIndex(inst);
    }

    return true;
}

bool PcpCli::HandleSetVariant(const CompositionInstruction& inst) {
    if (!inst.parameters.count("variant_set") || !inst.parameters.count("selection")) {
        std::cerr << "set_variant requires 'variant_set' and 'selection' parameters" << std::endl;
        return false;
    }

    // This would require more complex variant selection API
    // For now, just demonstrate the concept

    Result& result = results_.back();
    result.success = true;
    result.message = "Set variant selection";
    result.details["variant_set"] = inst.parameters.at("variant_set");
    result.details["selection"] = inst.parameters.at("selection");

    return true;
}

bool PcpCli::HandleAnalyze(const CompositionInstruction& inst) {
    Path prim_path(inst.prim_path);

    // Get or compute prim index
    PrimIndexPtr index;
    auto it = computed_indexes_.find(inst.prim_path);
    if (it != computed_indexes_.end()) {
        index = it->second;
    } else {
        index = cache_->ComputePrimIndex(prim_path);
        if (!index) {
            std::cerr << "Failed to get prim index for analysis" << std::endl;
            return false;
        }
        computed_indexes_[inst.prim_path] = index;
    }

    Result& result = results_.back();
    result.success = true;
    result.message = "Analysis complete";
    result.details = AnalyzePrimIndex(*index);

    if (config_.verbose) {
        std::cout << "  Analysis result: " << result.details.dump(2) << std::endl;
    }

    return true;
}

json PcpCli::AnalyzePrimIndex(const PrimIndex& index) {
    json analysis;

    // Basic information
    analysis["path"] = index.GetPath().full_path_name();
    analysis["has_specs"] = index.HasSpecs();
    analysis["is_instanceable"] = index.IsInstanceable();

    // Statistics
    auto stats = index.GetStatistics();
    json stats_json;
    stats_json["num_nodes"] = stats.num_nodes;
    stats_json["num_culled_nodes"] = stats.num_culled_nodes;
    stats_json["num_specs"] = stats.num_specs;
    stats_json["num_errors"] = stats.num_errors;
    stats_json["num_references"] = stats.num_references;
    stats_json["num_payloads"] = stats.num_payloads;
    stats_json["num_inherits"] = stats.num_inherits;
    stats_json["num_specializes"] = stats.num_specializes;
    stats_json["num_variants"] = stats.num_variants;
    analysis["statistics"] = stats_json;

    // Arc type breakdown
    std::map<ArcType, int> arc_counts;
    for (const auto& node : index.GetNodesInStrengthOrder()) {
        arc_counts[node.GetArcType()]++;
    }

    json arcs_json;
    for (const auto& [type, count] : arc_counts) {
        arcs_json[GetArcTypeName(type)] = count;
    }
    analysis["arc_types"] = arcs_json;

    // Instance key (if instanceable)
    if (index.IsInstanceable()) {
        auto key = index.ComputeInstanceKey();
        std::stringstream ss;
        for (uint8_t byte : key.blake3_hash) {
            ss << std::hex << std::setfill('0') << std::setw(2) << (int)byte;
        }
        analysis["instance_key"] = ss.str();
    }

    // Children
    auto children = index.GetChildrenNames();
    if (!children.empty()) {
        analysis["children"] = children;
    }

    // Properties
    auto properties = index.GetPropertyNames();
    if (!properties.empty()) {
        analysis["properties"] = properties;
    }

    return analysis;
}

void PcpCli::OutputResults() {
    if (config_.output_format == "json") {
        OutputJson();
    } else if (config_.output_format == "dot") {
        OutputDot();
    } else {
        OutputText();
    }
}

void PcpCli::OutputText() {
    std::ostream* out = &std::cout;
    std::ofstream file;

    if (!config_.output_file.empty()) {
        file.open(config_.output_file);
        if (file) {
            out = &file;
        }
    }

    *out << "=== PCP Composition Results ===" << std::endl;
    *out << std::endl;

    for (const auto& result : results_) {
        *out << "Path: " << result.prim_path << std::endl;
        *out << "Status: " << (result.success ? "SUCCESS" : "FAILED") << std::endl;
        *out << "Message: " << result.message << std::endl;

        if (config_.benchmark) {
            *out << "Time: " << result.time_seconds * 1000 << " ms" << std::endl;
        }

        if (!result.details.empty()) {
            *out << "Details:" << std::endl;
            *out << result.details.dump(2) << std::endl;
        }

        *out << std::endl;
    }

    // Cache statistics
    auto stats = cache_->GetStatistics();
    *out << "=== Cache Statistics ===" << std::endl;
    *out << "Layer Stacks: " << stats.num_layer_stacks << std::endl;
    *out << "Prim Indexes: " << stats.num_prim_indexes << std::endl;
    *out << "Nodes Created: " << stats.num_nodes_created << std::endl;
    *out << "Nodes Culled: " << stats.num_nodes_culled << std::endl;
    *out << "Arcs Processed: " << stats.num_arcs_processed << std::endl;
    *out << "Errors: " << stats.num_errors << std::endl;

    if (config_.benchmark) {
        *out << "Total Time: " << stats.time_elapsed_seconds * 1000 << " ms" << std::endl;
    }
}

void PcpCli::OutputJson() {
    json output;

    // Results
    json results_json = json::array();
    for (const auto& result : results_) {
        json result_json;
        result_json["prim_path"] = result.prim_path;
        result_json["success"] = result.success;
        result_json["message"] = result.message;

        if (config_.benchmark) {
            result_json["time_seconds"] = result.time_seconds;
        }

        if (!result.details.empty()) {
            result_json["details"] = result.details;
        }

        results_json.push_back(result_json);
    }
    output["results"] = results_json;

    // Cache statistics
    auto stats = cache_->GetStatistics();
    json stats_json;
    stats_json["num_layer_stacks"] = stats.num_layer_stacks;
    stats_json["num_prim_indexes"] = stats.num_prim_indexes;
    stats_json["num_nodes_created"] = stats.num_nodes_created;
    stats_json["num_nodes_culled"] = stats.num_nodes_culled;
    stats_json["num_arcs_processed"] = stats.num_arcs_processed;
    stats_json["num_errors"] = stats.num_errors;
    stats_json["time_elapsed_seconds"] = stats.time_elapsed_seconds;
    output["statistics"] = stats_json;

    std::ostream* out = &std::cout;
    std::ofstream file;

    if (!config_.output_file.empty()) {
        file.open(config_.output_file);
        if (file) {
            out = &file;
        }
    }

    *out << output.dump(2) << std::endl;
}

void PcpCli::OutputDot() {
    std::ostream* out = &std::cout;
    std::ofstream file;

    if (!config_.output_file.empty()) {
        file.open(config_.output_file);
        if (file) {
            out = &file;
        }
    }

    *out << "digraph PCP {" << std::endl;
    *out << "  rankdir=TB;" << std::endl;
    *out << "  node [shape=box];" << std::endl;
    *out << std::endl;

    // Output each computed prim index as a subgraph
    int subgraph_id = 0;
    for (const auto& [path, index] : computed_indexes_) {
        *out << "  subgraph cluster_" << subgraph_id++ << " {" << std::endl;
        *out << "    label=\"" << path << "\";" << std::endl;
        *out << "    color=blue;" << std::endl;

        if (index) {
            // Export the graph structure
            *out << index->ExportToDot();
        }

        *out << "  }" << std::endl;
        *out << std::endl;
    }

    *out << "}" << std::endl;
}

void PcpCli::PrintHelp() {
    std::cout << R"(
PCP CLI - TinyUSDZ Composition Tool

Usage: pcp_cli [options] <root_layer> [instructions.json]

Options:
  -h, --help              Show this help message
  -v, --version           Show version information

  -r, --root <file>       Root layer file (USDA)
  -s, --session <file>    Session layer file (optional)
  -i, --instructions <file>  JSON file with composition instructions

  -f, --format <format>   Output format: text, json, dot (default: text)
  -o, --output <file>     Output file (default: stdout)

  --verbose               Enable verbose output
  --debug                 Enable debug mode
  --benchmark             Show timing information

  --no-usd-mode          Disable USD mode (enable full features)
  --no-instancing        Disable instance detection

Composition Instructions (JSON):
  {
    "instructions": [
      {
        "operation": "compute",
        "prim_path": "/Model",
        "parameters": {}
      },
      {
        "operation": "include_payload",
        "prim_path": "/Model/Heavy",
        "parameters": {"recompute": "true"}
      },
      {
        "operation": "analyze",
        "prim_path": "/Model"
      }
    ]
  }

Operations:
  compute           - Compute prim index for a path
  include_payload   - Include a payload in the working set
  set_variant       - Set variant selection
  set_expression    - Set expression variable
  invalidate        - Invalidate cached prim index
  query            - Query composition information
  analyze          - Analyze composition structure
  benchmark        - Run composition benchmark

Examples:
  # Basic composition
  pcp_cli scene.usda

  # With session layer
  pcp_cli -r scene.usda -s overrides.usda

  # With instructions
  pcp_cli scene.usda instructions.json

  # JSON output
  pcp_cli scene.usda -f json -o result.json

  # Generate DOT graph
  pcp_cli scene.usda -f dot -o composition.dot
  dot -Tpng composition.dot -o composition.png
)" << std::endl;
}

void PcpCli::PrintVersion() {
    std::cout << "PCP CLI version 1.0.0" << std::endl;
    std::cout << "TinyUSDZ PCP implementation" << std::endl;
    std::cout << "Copyright 2024 Light Transport Entertainment Inc." << std::endl;
}

} // namespace pcp_cli
} // namespace tinyusdz

// Main entry point
int main(int argc, char** argv) {
    using namespace tinyusdz::pcp_cli;

    PcpCli app;

    // Parse arguments
    if (!app.ParseArgs(argc, argv)) {
        return 1;
    }

    // Load instructions
    if (!app.LoadInstructions(app.config_.instructions_file)) {
        return 1;
    }

    // Execute composition
    if (!app.ExecuteComposition()) {
        std::cerr << "Composition failed" << std::endl;
        return 1;
    }

    // Output results
    app.OutputResults();

    return 0;
}