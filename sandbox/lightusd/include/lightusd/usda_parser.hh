// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - USDA parser (recursive descent)

#pragma once

#include <string>
#include <memory>
#include <functional>

#include "lightusd/result.hh"
#include "lightusd/diagnostic.hh"
#include "lightusd/usda_lexer.hh"
#include "lightusd/token.hh"
#include "lightusd/stage.hh"
#include "lightusd/value.hh"
#include "lightusd/composition.hh"

namespace lightusd {
namespace v1 {

/// Parser options
struct ParserOptions {
    size_t max_depth = 256;              // Maximum nesting depth
    size_t max_array_size = 10000000;    // Maximum array elements (10M)
    size_t max_string_length = 100000000; // Maximum string length (100MB)
    bool allow_unknown_types = true;     // Allow unknown prim/attribute types
    bool strict_mode = false;            // Strict validation
};

/// Parser - recursive descent parser for USDA format
class Parser {
public:
    /// Construct parser with lexer
    explicit Parser(Lexer& lexer);

    /// Set parser options
    void set_options(const ParserOptions& opts) { options_ = opts; }

    /// Set filename for error reporting
    void set_filename(const std::string& filename) { filename_ = filename; }

    /// Parse entire USDA file into Stage
    Result<Stage> parse();

    /// Get diagnostics
    const DiagnosticList& diagnostics() const { return diagnostics_; }

    /// Check if has errors
    bool has_errors() const { return diagnostics_.has_errors(); }

private:
    Lexer& lexer_;
    LexToken current_;
    ParserOptions options_;
    std::string filename_;
    DiagnosticList diagnostics_;
    size_t depth_ = 0;

    // Token management
    void advance();
    bool check(TokenType type) const;
    bool match(TokenType type);
    bool expect(TokenType type, const char* message);
    LexToken consume(TokenType type, const char* message);

    // Error reporting
    void error(const std::string& message);
    void error_at(const Cursor& cursor, const std::string& message);

    // Depth tracking
    bool push_depth();
    void pop_depth();

    // ========== Grammar Rules ==========

    // Top-level
    bool parse_magic_header();
    bool parse_stage_metadata(Stage& stage);

    // Prims
    bool parse_prim(Prim& prim);
    bool parse_prim_metadata(Prim& prim);
    bool parse_prim_contents(Prim& prim);

    // Properties
    bool parse_property(const std::string& type_name, bool is_custom,
                        bool is_uniform, Prim& prim);
    bool parse_attribute(const std::string& type_name, const std::string& attr_name,
                         bool is_custom, bool is_uniform, Prim& prim);
    bool parse_attribute_metadata(Attribute& attr);
    bool parse_relationship(const std::string& rel_name, Prim& prim);
    bool parse_relationship_metadata(Relationship& rel);

    // Values
    Result<Value> parse_value(TypeId expected_type);
    Result<Value> parse_scalar_value(TypeId type);
    Result<Value> parse_tuple_value(TypeId type);
    Result<Value> parse_array_value(TypeId element_type);
    Result<Value> parse_dict_value();

    // Time samples
    bool parse_time_samples(TypeId type, TimeSamples& ts);

    // Connections
    bool parse_connections(std::vector<Path>& paths);

    // Helpers
    TypeId lookup_type(const std::string& type_name, bool* is_array);
    Specifier token_to_specifier(TokenType type);
    bool parse_metadata_dict(std::function<void(const std::string&, const Value&)> handler);
    bool parse_dict_entries(std::function<void(const std::string&, const Value&)> handler);
    void skip_value();

    // Composition arc parsing
    bool parse_reference_list(Prim& prim, ListEditOp op);
    bool parse_payload_list(Prim& prim, ListEditOp op);
    bool parse_path_list(PathList& path_list, ListEditOp op);
    bool parse_variant_set(Prim& prim);
};

} // namespace v1
} // namespace lightusd
