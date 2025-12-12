// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - USDA parser implementation

#include "lightusd/usda_parser.hh"
#include "lightusd/token.hh"
#include "lightusd/composition.hh"
#include "lightusd/variant.hh"
#include <cstring>
#include <cmath>

namespace lightusd {
namespace v1 {

// ============================================================================
// Constructor
// ============================================================================

Parser::Parser(Lexer& lexer)
    : lexer_(lexer) {
    advance(); // Prime the parser with first token
}

// ============================================================================
// Token Management
// ============================================================================

void Parser::advance() {
    current_ = lexer_.next();
}

bool Parser::check(TokenType type) const {
    return current_.type == type;
}

bool Parser::match(TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::expect(TokenType type, const char* message) {
    if (check(type)) {
        advance();
        return true;
    }
    error(std::string(message) + ", got '" + current_.to_string() + "'");
    return false;
}

LexToken Parser::consume(TokenType type, const char* message) {
    LexToken tok = current_;
    if (!expect(type, message)) {
        tok.type = TokenType::Error;
    }
    return tok;
}

// ============================================================================
// Error Reporting
// ============================================================================

void Parser::error(const std::string& message) {
    error_at(current_.start, message);
}

void Parser::error_at(const Cursor& cursor, const std::string& message) {
    SourceLocation loc(filename_, cursor);
    diagnostics_.error(loc, message);
}

// ============================================================================
// Depth Tracking
// ============================================================================

bool Parser::push_depth() {
    if (depth_ >= options_.max_depth) {
        error("Maximum nesting depth exceeded");
        return false;
    }
    depth_++;
    return true;
}

void Parser::pop_depth() {
    if (depth_ > 0) {
        depth_--;
    }
}

// ============================================================================
// Type Lookup
// ============================================================================

TypeId Parser::lookup_type(const std::string& type_name, bool* is_array) {
    *is_array = false;

    // Check for array suffix
    std::string base_name = type_name;
    if (type_name.size() > 2 &&
        type_name[type_name.size() - 2] == '[' &&
        type_name[type_name.size() - 1] == ']') {
        base_name = type_name.substr(0, type_name.size() - 2);
        *is_array = true;
    }

    // Look up type by name
    TypeId id = get_type_id(base_name.c_str());
    if (id == TypeId::Invalid && options_.allow_unknown_types) {
        // Return String as fallback for unknown types
        return TypeId::String;
    }
    return id;
}

Specifier Parser::token_to_specifier(TokenType type) {
    switch (type) {
        case TokenType::Kw_def:   return Specifier::Def;
        case TokenType::Kw_over:  return Specifier::Over;
        case TokenType::Kw_class: return Specifier::Class;
        default:                  return Specifier::Def;
    }
}

// ============================================================================
// Top-level Parsing
// ============================================================================

Result<Stage> Parser::parse() {
    Stage stage = Stage::create();

    // Parse magic header
    if (!parse_magic_header()) {
        return Error("Invalid USDA file: missing or invalid header");
    }

    // Parse optional stage metadata
    if (check(TokenType::LParen)) {
        if (!parse_stage_metadata(stage)) {
            return Error("Failed to parse stage metadata");
        }
    }

    // Parse root prims
    while (!check(TokenType::Eof) && !has_errors()) {
        if (!current_.is_specifier()) {
            error("Expected prim specifier (def, over, class)");
            break;
        }

        Prim prim;
        if (!parse_prim(prim)) {
            break;
        }
        stage.add_root_prim(std::move(prim));
    }

    // Copy lexer diagnostics
    for (const auto& d : lexer_.diagnostics().diagnostics()) {
        diagnostics_.add(d);
    }

    if (has_errors()) {
        return Error(diagnostics_.diagnostics()[0].message);
    }

    stage.commit();
    return stage;
}

bool Parser::parse_magic_header() {
    // Expect "#usda 1.0" (lexer returns this as an identifier token)
    if (current_.type == TokenType::Identifier &&
        current_.str_value.find("#usda") == 0) {
        advance();
        return true;
    }

    error("Expected USDA header '#usda 1.0'");
    return false;
}

bool Parser::parse_stage_metadata(Stage& stage) {
    if (!expect(TokenType::LParen, "Expected '('")) {
        return false;
    }

    while (!check(TokenType::RParen) && !check(TokenType::Eof)) {
        // Parse key = value pairs
        if (!check(TokenType::Identifier)) {
            error("Expected metadata key");
            return false;
        }

        std::string key = current_.str_value;
        advance();

        if (!expect(TokenType::Equals, "Expected '='")) {
            return false;
        }

        // Handle known stage metadata
        if (key == "defaultPrim") {
            if (!check(TokenType::String)) {
                error("Expected string value for defaultPrim");
                return false;
            }
            stage.set_default_prim(current_.str_value);
            advance();
        } else if (key == "startTimeCode") {
            if (check(TokenType::Integer)) {
                stage.set_start_time_code(static_cast<double>(current_.int_value));
            } else if (check(TokenType::Float)) {
                stage.set_start_time_code(current_.float_value);
            } else {
                error("Expected number for startTimeCode");
                return false;
            }
            advance();
        } else if (key == "endTimeCode") {
            if (check(TokenType::Integer)) {
                stage.set_end_time_code(static_cast<double>(current_.int_value));
            } else if (check(TokenType::Float)) {
                stage.set_end_time_code(current_.float_value);
            } else {
                error("Expected number for endTimeCode");
                return false;
            }
            advance();
        } else if (key == "framesPerSecond") {
            if (check(TokenType::Integer)) {
                stage.set_frames_per_second(static_cast<double>(current_.int_value));
            } else if (check(TokenType::Float)) {
                stage.set_frames_per_second(current_.float_value);
            } else {
                error("Expected number for framesPerSecond");
                return false;
            }
            advance();
        } else if (key == "timeCodesPerSecond") {
            if (check(TokenType::Integer)) {
                stage.set_time_codes_per_second(static_cast<double>(current_.int_value));
            } else if (check(TokenType::Float)) {
                stage.set_time_codes_per_second(current_.float_value);
            } else {
                error("Expected number for timeCodesPerSecond");
                return false;
            }
            advance();
        } else if (key == "metersPerUnit") {
            if (check(TokenType::Integer)) {
                stage.set_meters_per_unit(static_cast<double>(current_.int_value));
            } else if (check(TokenType::Float)) {
                stage.set_meters_per_unit(current_.float_value);
            } else {
                error("Expected number for metersPerUnit");
                return false;
            }
            advance();
        } else if (key == "upAxis") {
            if (!check(TokenType::String)) {
                error("Expected string for upAxis");
                return false;
            }
            stage.set_up_axis(current_.str_value);
            advance();
        } else if (key == "documentation" || key == "doc") {
            if (!check(TokenType::String)) {
                error("Expected string for documentation");
                return false;
            }
            stage.set_documentation(current_.str_value);
            advance();
        } else if (key == "comment") {
            if (!check(TokenType::String)) {
                error("Expected string for comment");
                return false;
            }
            stage.set_comment(current_.str_value);
            advance();
        } else if (key == "owner") {
            if (!check(TokenType::String)) {
                error("Expected string for owner");
                return false;
            }
            stage.set_owner(current_.str_value);
            advance();
        } else if (key == "subLayers") {
            // Parse sublayers list: subLayers = [@./base.usd@, @./anim.usd@ (offset=10; scale=1)]
            if (!expect(TokenType::LBracket, "Expected '[' for subLayers")) {
                return false;
            }
            while (!check(TokenType::RBracket) && !check(TokenType::Eof)) {
                // Parse @asset_path@
                if (!check(TokenType::AssetPath)) {
                    error("Expected asset path in subLayers");
                    return false;
                }
                std::string asset_path = current_.str_value;
                advance();

                // Parse optional layer offset: (offset = 10; scale = 1)
                LayerOffset layer_offset;
                if (check(TokenType::LParen)) {
                    advance();
                    while (!check(TokenType::RParen) && !check(TokenType::Eof)) {
                        if (check(TokenType::Identifier)) {
                            std::string offset_key = current_.str_value;
                            advance();
                            if (!expect(TokenType::Equals, "Expected '='")) {
                                return false;
                            }
                            if (offset_key == "offset") {
                                if (check(TokenType::Integer)) {
                                    layer_offset.offset = static_cast<double>(current_.int_value);
                                } else if (check(TokenType::Float)) {
                                    layer_offset.offset = current_.float_value;
                                }
                                advance();
                            } else if (offset_key == "scale") {
                                if (check(TokenType::Integer)) {
                                    layer_offset.scale = static_cast<double>(current_.int_value);
                                } else if (check(TokenType::Float)) {
                                    layer_offset.scale = current_.float_value;
                                }
                                advance();
                            }
                        }
                        if (check(TokenType::Semicolon)) {
                            advance();
                        }
                    }
                    if (!expect(TokenType::RParen, "Expected ')'")) {
                        return false;
                    }
                }

                stage.add_sublayer(asset_path, layer_offset);

                if (check(TokenType::Comma)) {
                    advance();
                }
            }
            if (!expect(TokenType::RBracket, "Expected ']'")) {
                return false;
            }
        } else if (key == "customLayerData") {
            // Parse dictionary
            if (!expect(TokenType::LBrace, "Expected '{' for customLayerData")) {
                return false;
            }
            while (!check(TokenType::RBrace) && !check(TokenType::Eof)) {
                // Parse key
                std::string dict_key;
                // Check for type prefix (e.g., "string creator")
                if (check(TokenType::Identifier)) {
                    // Skip type prefix if present
                    std::string type_name = current_.str_value;
                    advance();
                    if (check(TokenType::Identifier)) {
                        // We had a type prefix, now get the actual key
                        dict_key = current_.str_value;
                        advance();
                    } else {
                        // No type prefix, first identifier was the key
                        dict_key = type_name;
                    }
                } else if (check(TokenType::String)) {
                    dict_key = current_.str_value;
                    advance();
                } else {
                    error("Expected key in customLayerData");
                    return false;
                }

                if (!expect(TokenType::Equals, "Expected '='")) {
                    return false;
                }

                // Parse value
                Value val;
                if (check(TokenType::String)) {
                    val = Value::from_string(current_.str_value);
                    advance();
                } else if (check(TokenType::Integer)) {
                    val = Value::from_int64(current_.int_value);
                    advance();
                } else if (check(TokenType::Float)) {
                    val = Value::from_double(current_.float_value);
                    advance();
                } else if (check(TokenType::Kw_true)) {
                    val = Value::from_bool(true);
                    advance();
                } else if (check(TokenType::Kw_false)) {
                    val = Value::from_bool(false);
                    advance();
                } else {
                    // Skip complex values for now
                    int depth = 0;
                    do {
                        if (check(TokenType::LParen) || check(TokenType::LBracket) ||
                            check(TokenType::LBrace)) {
                            depth++;
                        } else if (check(TokenType::RParen) || check(TokenType::RBracket) ||
                                   check(TokenType::RBrace)) {
                            if (depth == 0) break;
                            depth--;
                        }
                        advance();
                    } while (depth > 0 && !check(TokenType::Eof));
                    continue;
                }
                stage.set_custom_layer_data(dict_key, val);
            }
            if (!expect(TokenType::RBrace, "Expected '}'")) {
                return false;
            }
        } else {
            // Store unknown metadata in generic dictionary
            Value val;
            if (check(TokenType::String)) {
                val = Value::from_string(current_.str_value);
                advance();
                stage.set_metadata(key, val);
            } else if (check(TokenType::Integer)) {
                val = Value::from_int64(current_.int_value);
                advance();
                stage.set_metadata(key, val);
            } else if (check(TokenType::Float)) {
                val = Value::from_double(current_.float_value);
                advance();
                stage.set_metadata(key, val);
            } else if (check(TokenType::Kw_true)) {
                val = Value::from_bool(true);
                advance();
                stage.set_metadata(key, val);
            } else if (check(TokenType::Kw_false)) {
                val = Value::from_bool(false);
                advance();
                stage.set_metadata(key, val);
            } else {
                // Skip complex unknown metadata
                int depth = 0;
                do {
                    if (check(TokenType::LParen) || check(TokenType::LBracket) ||
                        check(TokenType::LBrace)) {
                        depth++;
                    } else if (check(TokenType::RParen) || check(TokenType::RBracket) ||
                               check(TokenType::RBrace)) {
                        if (depth == 0) break;
                        depth--;
                    }
                    advance();
                } while (depth > 0 && !check(TokenType::Eof));
            }
        }
    }

    return expect(TokenType::RParen, "Expected ')'");
}

// ============================================================================
// Prim Parsing
// ============================================================================

bool Parser::parse_prim(Prim& prim) {
    if (!push_depth()) {
        return false;
    }

    // Specifier: def/over/class
    Specifier spec = token_to_specifier(current_.type);
    prim.set_specifier(spec);
    advance();

    // Optional type name
    if (check(TokenType::Identifier)) {
        prim.set_type_name(current_.str_value);
        advance();
    }

    // Prim name (string)
    if (!check(TokenType::String)) {
        error("Expected prim name");
        pop_depth();
        return false;
    }
    prim.set_name(current_.str_value);
    advance();

    // Optional metadata
    if (check(TokenType::LParen)) {
        if (!parse_prim_metadata(prim)) {
            pop_depth();
            return false;
        }
    }

    // Prim body
    if (!expect(TokenType::LBrace, "Expected '{'")) {
        pop_depth();
        return false;
    }

    if (!parse_prim_contents(prim)) {
        pop_depth();
        return false;
    }

    if (!expect(TokenType::RBrace, "Expected '}'")) {
        pop_depth();
        return false;
    }

    pop_depth();
    return true;
}

bool Parser::parse_prim_metadata(Prim& prim) {
    if (!expect(TokenType::LParen, "Expected '('")) {
        return false;
    }

    while (!check(TokenType::RParen) && !check(TokenType::Eof)) {
        // Parse key = value pairs
        // Note: list edit qualifiers like prepend/append/delete are keywords, not identifiers
        bool is_list_edit_keyword = (check(TokenType::Kw_prepend) || check(TokenType::Kw_append) ||
                                     check(TokenType::Kw_delete) || check(TokenType::Kw_add) ||
                                     check(TokenType::Kw_reorder));

        if (!check(TokenType::Identifier) && !is_list_edit_keyword) {
            error("Expected metadata key");
            return false;
        }

        std::string key = current_.str_value;
        advance();

        // Check if key is a list edit qualifier (prepend, append, delete)
        // In that case, the format is: prepend references = [...], no "=" after prepend
        bool is_list_edit_qualifier = is_list_edit_keyword;

        if (!is_list_edit_qualifier) {
            if (!expect(TokenType::Equals, "Expected '='")) {
                return false;
            }
        }

        // Handle known prim metadata
        if (key == "active") {
            if (check(TokenType::Kw_true)) {
                prim.set_active(true);
            } else if (check(TokenType::Kw_false)) {
                prim.set_active(false);
            } else {
                error("Expected true or false for 'active'");
                return false;
            }
            advance();
        }
        // kind = "component"
        else if (key == "kind") {
            if (check(TokenType::String)) {
                prim.set_kind(current_.str_value);
                advance();
            } else {
                error("Expected string for 'kind'");
                return false;
            }
        }
        // purpose = "render"
        else if (key == "purpose") {
            if (check(TokenType::String)) {
                prim.set_purpose(current_.str_value);
                advance();
            } else {
                error("Expected string for 'purpose'");
                return false;
            }
        }
        // hidden = true
        else if (key == "hidden") {
            if (check(TokenType::Kw_true)) {
                prim.set_hidden(true);
            } else if (check(TokenType::Kw_false)) {
                prim.set_hidden(false);
            } else {
                error("Expected true or false for 'hidden'");
                return false;
            }
            advance();
        }
        // documentation = "..."
        else if (key == "documentation" || key == "doc") {
            if (check(TokenType::String)) {
                prim.set_documentation(current_.str_value);
                advance();
            } else {
                error("Expected string for 'documentation'");
                return false;
            }
        }
        // apiSchemas = ["SkelBindingAPI", ...]
        else if (key == "apiSchemas") {
            if (check(TokenType::LBracket)) {
                advance();
                std::vector<std::string> schemas;
                while (!check(TokenType::RBracket) && !check(TokenType::Eof)) {
                    if (check(TokenType::String) || check(TokenType::Identifier)) {
                        schemas.push_back(current_.str_value);
                        advance();
                    }
                    if (check(TokenType::Comma)) {
                        advance();
                    }
                }
                if (!expect(TokenType::RBracket, "Expected ']'")) {
                    return false;
                }
                prim.set_api_schemas(schemas);
            } else {
                error("Expected '[' for 'apiSchemas'");
                return false;
            }
        }
        // customData = { ... }
        else if (key == "customData") {
            if (check(TokenType::LBrace)) {
                advance();
                // Parse dictionary entries
                while (!check(TokenType::RBrace) && !check(TokenType::Eof)) {
                    // type key = value
                    std::string type_name;
                    if (check(TokenType::Identifier)) {
                        type_name = current_.str_value;
                        advance();
                    }

                    std::string custom_key;
                    if (check(TokenType::Identifier) || check(TokenType::String)) {
                        custom_key = current_.str_value;
                        advance();
                    }

                    if (!expect(TokenType::Equals, "Expected '='")) {
                        return false;
                    }

                    // Parse the value based on type
                    bool is_array = false;
                    TypeId type_id = lookup_type(type_name, &is_array);
                    auto value_result = parse_value(type_id);
                    if (value_result.ok()) {
                        prim.set_custom_data(custom_key, value_result.value());
                    }
                }
                if (!expect(TokenType::RBrace, "Expected '}'")) {
                    return false;
                }
            } else {
                error("Expected '{' for 'customData'");
                return false;
            }
        }
        // assetInfo = { ... }
        else if (key == "assetInfo") {
            if (check(TokenType::LBrace)) {
                advance();
                // Parse dictionary entries
                while (!check(TokenType::RBrace) && !check(TokenType::Eof)) {
                    // type key = value (e.g., string name = "Model")
                    std::string type_name;
                    if (check(TokenType::Identifier)) {
                        type_name = current_.str_value;
                        advance();
                    }

                    std::string asset_key;
                    if (check(TokenType::Identifier) || check(TokenType::String)) {
                        asset_key = current_.str_value;
                        advance();
                    }

                    if (!expect(TokenType::Equals, "Expected '='")) {
                        return false;
                    }

                    // Parse the value based on type
                    bool is_array = false;
                    TypeId type_id = lookup_type(type_name, &is_array);
                    auto value_result = parse_value(type_id);
                    if (value_result.ok()) {
                        prim.set_asset_info(asset_key, value_result.value());
                    }
                }
                if (!expect(TokenType::RBrace, "Expected '}'")) {
                    return false;
                }
            } else {
                error("Expected '{' for 'assetInfo'");
                return false;
            }
        }
        // instanceable = true/false
        else if (key == "instanceable") {
            if (check(TokenType::Kw_true)) {
                prim.set_instanceable(true);
            } else if (check(TokenType::Kw_false)) {
                prim.set_instanceable(false);
            } else {
                error("Expected true or false for 'instanceable'");
                return false;
            }
            advance();
        }
        // references = [...] or prepend references = [...] or append references = [...]
        else if (key == "references" || is_list_edit_qualifier) {
            ListEditOp op = ListEditOp::Explicit;
            std::string target_key = key;

            if (is_list_edit_qualifier) {
                op = (key == "prepend") ? ListEditOp::Prepend :
                     (key == "append") ? ListEditOp::Append :
                     (key == "delete") ? ListEditOp::Delete :
                     (key == "add") ? ListEditOp::Add : ListEditOp::Reorder;
                // Get the actual metadata key (like "references", "inherits", etc.)
                if (!check(TokenType::Identifier)) {
                    error("Expected metadata key after list edit qualifier");
                    return false;
                }
                target_key = current_.str_value;
                advance();
                // Now expect "="
                if (!expect(TokenType::Equals, "Expected '='")) {
                    return false;
                }
            }

            if (target_key == "references") {
                // Parse reference list
                if (!parse_reference_list(prim, op)) {
                    return false;
                }
            } else if (target_key == "payloads" || target_key == "payload") {
                // Parse payload list
                if (!parse_payload_list(prim, op)) {
                    return false;
                }
            } else if (target_key == "inherits") {
                // Parse inherits list
                if (!parse_path_list(prim.inherits_mutable(), op)) {
                    return false;
                }
            } else if (target_key == "specializes") {
                // Parse specializes list
                if (!parse_path_list(prim.specializes_mutable(), op)) {
                    return false;
                }
            } else if (target_key == "variantSets") {
                // Parse variantSets list (just names)
                if (check(TokenType::LBracket)) {
                    advance();
                    while (!check(TokenType::RBracket) && !check(TokenType::Eof)) {
                        if (check(TokenType::String)) {
                            // Store variant set name for later
                            advance();
                        }
                        if (check(TokenType::Comma)) advance();
                    }
                    if (!expect(TokenType::RBracket, "Expected ']'")) {
                        return false;
                    }
                }
            } else {
                // Unknown list edit target - skip value
                skip_value();
            }
        }
        // variants = { variantSetName = "variantName" }
        else if (key == "variants") {
            if (check(TokenType::LBrace)) {
                advance();
                while (!check(TokenType::RBrace) && !check(TokenType::Eof)) {
                    std::string vs_name;
                    if (check(TokenType::Identifier) || check(TokenType::String)) {
                        vs_name = current_.str_value;
                        advance();
                    }
                    if (!expect(TokenType::Equals, "Expected '='")) {
                        return false;
                    }
                    if (check(TokenType::String)) {
                        prim.set_variant_selection(vs_name, current_.str_value);
                        advance();
                    }
                }
                if (!expect(TokenType::RBrace, "Expected '}'")) {
                    return false;
                }
            }
        }
        // Generic metadata - store in metadata dictionary
        else {
            // Try to parse a simple value
            if (check(TokenType::String)) {
                prim.set_metadata(key, Value::from_string(current_.str_value));
                advance();
            } else if (check(TokenType::Kw_true)) {
                prim.set_metadata(key, Value::from_bool(true));
                advance();
            } else if (check(TokenType::Kw_false)) {
                prim.set_metadata(key, Value::from_bool(false));
                advance();
            } else if (check(TokenType::Integer)) {
                prim.set_metadata(key, Value::from_int64(current_.int_value));
                advance();
            } else if (check(TokenType::Float)) {
                prim.set_metadata(key, Value::from_double(current_.float_value));
                advance();
            } else {
                // Skip complex metadata value (brackets, braces, etc.)
                int depth = 0;
                do {
                    if (check(TokenType::LParen) || check(TokenType::LBracket) ||
                        check(TokenType::LBrace)) {
                        depth++;
                    } else if (check(TokenType::RParen) || check(TokenType::RBracket) ||
                               check(TokenType::RBrace)) {
                        if (depth == 0) break;
                        depth--;
                    }
                    advance();
                } while (depth > 0 && !check(TokenType::Eof));
            }
        }
    }

    return expect(TokenType::RParen, "Expected ')'");
}

bool Parser::parse_prim_contents(Prim& prim) {
    while (!check(TokenType::RBrace) && !check(TokenType::Eof) && !has_errors()) {
        // Nested prim
        if (current_.is_specifier()) {
            Prim child;
            if (!parse_prim(child)) {
                return false;
            }
            prim.add_child(std::move(child));
            continue;
        }

        // variantSet
        if (check(TokenType::Kw_variantSet)) {
            if (!parse_variant_set(prim)) {
                return false;
            }
            continue;
        }

        // Property modifiers
        bool is_custom = false;
        bool is_uniform = false;

        while (check(TokenType::Kw_custom) || check(TokenType::Kw_uniform) ||
               current_.is_list_edit_qual()) {
            if (check(TokenType::Kw_custom)) {
                is_custom = true;
                advance();
            } else if (check(TokenType::Kw_uniform)) {
                is_uniform = true;
                advance();
            } else {
                // Skip list edit qualifiers
                advance();
            }
        }

        // Relationship
        if (check(TokenType::Kw_rel)) {
            advance();
            if (!check(TokenType::Identifier)) {
                error("Expected relationship name");
                return false;
            }
            std::string rel_name = current_.str_value;
            advance();

            if (!parse_relationship(rel_name, prim)) {
                return false;
            }
            continue;
        }

        // Attribute: type name = value
        if (!check(TokenType::Identifier)) {
            error("Expected type name or property");
            return false;
        }

        std::string type_name = current_.str_value;
        advance();

        // Check for array type suffix []
        if (check(TokenType::LBracket)) {
            advance();
            if (!expect(TokenType::RBracket, "Expected ']'")) {
                return false;
            }
            type_name += "[]";
        }

        if (!parse_property(type_name, is_custom, is_uniform, prim)) {
            return false;
        }
    }

    return true;
}

// ============================================================================
// Property Parsing
// ============================================================================

bool Parser::parse_property(const std::string& type_name, bool is_custom,
                            bool is_uniform, Prim& prim) {
    // Attribute name
    if (!check(TokenType::Identifier)) {
        error("Expected attribute name");
        return false;
    }

    std::string attr_name = current_.str_value;
    advance();

    return parse_attribute(type_name, attr_name, is_custom, is_uniform, prim);
}

bool Parser::parse_attribute(const std::string& type_name, const std::string& attr_name,
                             bool is_custom, bool is_uniform, Prim& prim) {
    (void)is_custom; // Currently unused

    bool is_array = false;
    TypeId type_id = lookup_type(type_name, &is_array);

    Attribute attr(type_id);
    if (is_uniform) {
        attr.set_variability(Variability::Uniform);
    }

    // Check for .timeSamples or .connect suffix
    if (check(TokenType::Dot)) {
        advance();

        if (check(TokenType::Kw_timeSamples)) {
            advance();
            if (!expect(TokenType::Equals, "Expected '='")) {
                return false;
            }
            TimeSamples ts;
            if (!parse_time_samples(type_id, ts)) {
                return false;
            }
            attr.set_timesamples(std::move(ts));
            prim.set_attribute(attr_name, std::move(attr));
            return true;
        } else if (check(TokenType::Kw_connect)) {
            advance();
            if (!expect(TokenType::Equals, "Expected '='")) {
                return false;
            }
            std::vector<Path> paths;
            if (!parse_connections(paths)) {
                return false;
            }
            for (const auto& path : paths) {
                attr.add_connection(path);
            }
            prim.set_attribute(attr_name, std::move(attr));
            return true;
        } else {
            error("Expected 'timeSamples' or 'connect'");
            return false;
        }
    }

    // Default value
    if (!check(TokenType::Equals)) {
        // Attribute declaration without value
        prim.set_attribute(attr_name, std::move(attr));
        return true;
    }

    advance(); // =

    // Parse value
    TypeId value_type = is_array ? make_array_type(type_id) : type_id;
    auto value_result = parse_value(value_type);
    if (!value_result.ok()) {
        error(value_result.error().message);
        return false;
    }

    attr.set_default(value_result.value());
    prim.set_attribute(attr_name, std::move(attr));
    return true;
}

bool Parser::parse_relationship(const std::string& rel_name, Prim& prim) {
    Relationship rel;

    if (check(TokenType::Equals)) {
        advance();

        std::vector<Path> paths;
        if (!parse_connections(paths)) {
            return false;
        }
        rel.set_targets(paths);
    }

    prim.set_relationship(rel_name, std::move(rel));
    return true;
}

// ============================================================================
// Value Parsing
// ============================================================================

Result<Value> Parser::parse_value(TypeId expected_type) {
    // None
    if (check(TokenType::Kw_None)) {
        advance();
        return Value::make_none();
    }

    // Array
    if (check(TokenType::LBracket)) {
        TypeId element_type = get_base_type(expected_type);
        return parse_array_value(element_type);
    }

    // Tuple (vector/matrix)
    if (check(TokenType::LParen)) {
        return parse_tuple_value(expected_type);
    }

    // Dictionary
    if (check(TokenType::LBrace)) {
        return parse_dict_value();
    }

    // Scalar
    return parse_scalar_value(expected_type);
}

Result<Value> Parser::parse_scalar_value(TypeId type) {
    // Boolean
    if (check(TokenType::Kw_true)) {
        advance();
        return Value::from_bool(true);
    }
    if (check(TokenType::Kw_false)) {
        advance();
        return Value::from_bool(false);
    }

    // Integer
    if (check(TokenType::Integer)) {
        int64_t val = current_.int_value;
        advance();
        switch (type) {
            case TypeId::Int32:  return Value::from_int32(static_cast<int32_t>(val));
            case TypeId::Int64:  return Value::from_int64(val);
            case TypeId::UInt32: return Value::from_uint32(static_cast<uint32_t>(val));
            case TypeId::UInt64: return Value::from_uint64(static_cast<uint64_t>(val));
            case TypeId::Float:  return Value::from_float(static_cast<float>(val));
            case TypeId::Double: return Value::from_double(static_cast<double>(val));
            case TypeId::TimeCode: return Value::from_timecode(static_cast<double>(val));
            default: return Value::from_int64(val);
        }
    }

    // Float
    if (check(TokenType::Float)) {
        double val = current_.float_value;
        advance();
        switch (type) {
            case TypeId::Float:  return Value::from_float(static_cast<float>(val));
            case TypeId::Double: return Value::from_double(val);
            case TypeId::TimeCode: return Value::from_timecode(val);
            default: return Value::from_double(val);
        }
    }

    // String
    if (check(TokenType::String)) {
        std::string val = current_.str_value;
        advance();
        switch (type) {
            case TypeId::Token: return Value::from_token(Token(val));
            case TypeId::AssetPath: return Value::from_asset_path(val);
            case TypeId::Path: return Value::from_path(Path(val));
            default: return Value::from_string(val);
        }
    }

    // Identifier (could be token value)
    if (check(TokenType::Identifier)) {
        std::string val = current_.str_value;
        advance();
        if (type == TypeId::Token) {
            return Value::from_token(Token(val));
        }
        return Value::from_string(val);
    }

    return Error("Expected scalar value");
}

Result<Value> Parser::parse_tuple_value(TypeId type) {
    if (!expect(TokenType::LParen, "Expected '('")) {
        return Error("Expected '('");
    }

    std::vector<double> components;

    // Parse first component
    if (!check(TokenType::RParen)) {
        // Check for nested tuple (matrix)
        if (check(TokenType::LParen)) {
            // Matrix - parse row by row
            while (!check(TokenType::RParen) && !check(TokenType::Eof)) {
                auto row = parse_tuple_value(TypeId::Double4);
                if (!row.ok()) {
                    return row;
                }
                // Extract row components
                const double* row_data = row.value().as_double4();
                if (row_data) {
                    for (int i = 0; i < 4; ++i) {
                        components.push_back(row_data[i]);
                    }
                } else {
                    const double* d3 = row.value().as_double3();
                    if (d3) {
                        for (int i = 0; i < 3; ++i) {
                            components.push_back(d3[i]);
                        }
                    } else {
                        const double* d2 = row.value().as_double2();
                        if (d2) {
                            components.push_back(d2[0]);
                            components.push_back(d2[1]);
                        }
                    }
                }
                match(TokenType::Comma);
            }
        } else {
            // Vector - parse components
            while (!check(TokenType::RParen) && !check(TokenType::Eof)) {
                if (check(TokenType::Integer)) {
                    components.push_back(static_cast<double>(current_.int_value));
                    advance();
                } else if (check(TokenType::Float)) {
                    components.push_back(current_.float_value);
                    advance();
                } else {
                    return Error("Expected number in tuple");
                }
                match(TokenType::Comma);
            }
        }
    }

    if (!expect(TokenType::RParen, "Expected ')'")) {
        return Error("Expected ')'");
    }

    // Create appropriate value based on component count and type
    size_t n = components.size();

    // Integer vectors
    if (type == TypeId::Int2 || type == TypeId::Int3 || type == TypeId::Int4) {
        if (n >= 2 && type == TypeId::Int2) {
            return Value::from_int2(static_cast<int32_t>(components[0]),
                                     static_cast<int32_t>(components[1]));
        }
        if (n >= 3 && type == TypeId::Int3) {
            return Value::from_int3(static_cast<int32_t>(components[0]),
                                     static_cast<int32_t>(components[1]),
                                     static_cast<int32_t>(components[2]));
        }
        if (n >= 4) {
            return Value::from_int4(static_cast<int32_t>(components[0]),
                                     static_cast<int32_t>(components[1]),
                                     static_cast<int32_t>(components[2]),
                                     static_cast<int32_t>(components[3]));
        }
    }

    // Float vectors
    if (type == TypeId::Float2 || type == TypeId::Half2 || type == TypeId::TexCoord2f) {
        if (n >= 2) {
            return Value::from_float2(static_cast<float>(components[0]),
                                       static_cast<float>(components[1]));
        }
    }
    if (type == TypeId::Float3 || type == TypeId::Half3 ||
        type == TypeId::Color3f || type == TypeId::Point3f ||
        type == TypeId::Vector3f || type == TypeId::Normal3f) {
        if (n >= 3) {
            return Value::from_float3(static_cast<float>(components[0]),
                                       static_cast<float>(components[1]),
                                       static_cast<float>(components[2]));
        }
    }
    if (type == TypeId::Float4 || type == TypeId::Half4 ||
        type == TypeId::Color4f || type == TypeId::Quatf) {
        if (n >= 4) {
            return Value::from_float4(static_cast<float>(components[0]),
                                       static_cast<float>(components[1]),
                                       static_cast<float>(components[2]),
                                       static_cast<float>(components[3]));
        }
    }

    // Double vectors
    if (type == TypeId::Double2 || type == TypeId::TexCoord2d) {
        if (n >= 2) {
            return Value::from_double2(components[0], components[1]);
        }
    }
    if (type == TypeId::Double3 || type == TypeId::Color3d ||
        type == TypeId::Point3d || type == TypeId::Vector3d || type == TypeId::Normal3d) {
        if (n >= 3) {
            return Value::from_double3(components[0], components[1], components[2]);
        }
    }
    if (type == TypeId::Double4 || type == TypeId::Color4d || type == TypeId::Quatd) {
        if (n >= 4) {
            return Value::from_double4(components[0], components[1],
                                        components[2], components[3]);
        }
    }

    // Matrices
    if (type == TypeId::Matrix4d && n >= 16) {
        return Value::from_matrix4d(components.data());
    }
    if (type == TypeId::Matrix3d && n >= 9) {
        return Value::from_matrix3d(components.data());
    }
    if (type == TypeId::Matrix2d && n >= 4) {
        return Value::from_matrix2d(components.data());
    }
    if (type == TypeId::Matrix4f && n >= 16) {
        float f[16];
        for (int i = 0; i < 16; ++i) f[i] = static_cast<float>(components[i]);
        return Value::from_matrix4f(f);
    }
    if (type == TypeId::Matrix3f && n >= 9) {
        float f[9];
        for (int i = 0; i < 9; ++i) f[i] = static_cast<float>(components[i]);
        return Value::from_matrix3f(f);
    }
    if (type == TypeId::Matrix2f && n >= 4) {
        float f[4];
        for (int i = 0; i < 4; ++i) f[i] = static_cast<float>(components[i]);
        return Value::from_matrix2f(f);
    }

    // Default: create based on component count
    if (n == 2) {
        return Value::from_double2(components[0], components[1]);
    }
    if (n == 3) {
        return Value::from_double3(components[0], components[1], components[2]);
    }
    if (n == 4) {
        return Value::from_double4(components[0], components[1],
                                    components[2], components[3]);
    }
    if (n >= 9) {
        // Assume matrix
        return Value::from_matrix4d(components.data());
    }

    return Error("Invalid tuple");
}

Result<Value> Parser::parse_array_value(TypeId element_type) {
    if (!expect(TokenType::LBracket, "Expected '['")) {
        return Error("Expected '['");
    }

    std::vector<float> float_values;
    std::vector<int32_t> int_values;
    std::vector<std::string> string_values;

    bool is_float = (element_type == TypeId::Float || element_type == TypeId::Float2 ||
                     element_type == TypeId::Float3 || element_type == TypeId::Float4 ||
                     element_type == TypeId::Double || element_type == TypeId::Double2 ||
                     element_type == TypeId::Double3 || element_type == TypeId::Double4 ||
                     element_type == TypeId::Color3f || element_type == TypeId::Point3f ||
                     element_type == TypeId::Vector3f || element_type == TypeId::Normal3f ||
                     element_type == TypeId::TexCoord2f);
    bool is_int = (element_type == TypeId::Int32 || element_type == TypeId::Int2 ||
                   element_type == TypeId::Int3 || element_type == TypeId::Int4);
    bool is_string = (element_type == TypeId::String || element_type == TypeId::Token);

    while (!check(TokenType::RBracket) && !check(TokenType::Eof)) {
        if (float_values.size() >= options_.max_array_size ||
            int_values.size() >= options_.max_array_size ||
            string_values.size() >= options_.max_array_size) {
            return Error("Array too large");
        }

        // Tuple (vector element)
        if (check(TokenType::LParen)) {
            auto val = parse_tuple_value(element_type);
            if (!val.ok()) {
                return val;
            }
            // Extract float components
            if (element_type == TypeId::Float2 || element_type == TypeId::TexCoord2f) {
                const float* f = val.value().as_float2();
                if (f) {
                    float_values.push_back(f[0]);
                    float_values.push_back(f[1]);
                }
            } else if (element_type == TypeId::Float3 || element_type == TypeId::Color3f ||
                       element_type == TypeId::Point3f || element_type == TypeId::Vector3f ||
                       element_type == TypeId::Normal3f) {
                const float* f = val.value().as_float3();
                if (f) {
                    float_values.push_back(f[0]);
                    float_values.push_back(f[1]);
                    float_values.push_back(f[2]);
                }
            } else if (element_type == TypeId::Float4) {
                const float* f = val.value().as_float4();
                if (f) {
                    float_values.push_back(f[0]);
                    float_values.push_back(f[1]);
                    float_values.push_back(f[2]);
                    float_values.push_back(f[3]);
                }
            } else if (element_type == TypeId::Int2) {
                const int32_t* i = val.value().as_int2();
                if (i) {
                    int_values.push_back(i[0]);
                    int_values.push_back(i[1]);
                }
            } else if (element_type == TypeId::Int3) {
                const int32_t* i = val.value().as_int3();
                if (i) {
                    int_values.push_back(i[0]);
                    int_values.push_back(i[1]);
                    int_values.push_back(i[2]);
                }
            }
        }
        // Scalar
        else if (check(TokenType::Integer)) {
            if (is_float) {
                float_values.push_back(static_cast<float>(current_.int_value));
            } else {
                int_values.push_back(static_cast<int32_t>(current_.int_value));
            }
            advance();
        } else if (check(TokenType::Float)) {
            float_values.push_back(static_cast<float>(current_.float_value));
            advance();
        } else if (check(TokenType::String)) {
            string_values.push_back(current_.str_value);
            advance();
        } else if (check(TokenType::Identifier)) {
            string_values.push_back(current_.str_value);
            advance();
        } else {
            return Error("Expected array element");
        }

        match(TokenType::Comma);
    }

    if (!expect(TokenType::RBracket, "Expected ']'")) {
        return Error("Expected ']'");
    }

    // Create appropriate array value
    if (!float_values.empty()) {
        if (element_type == TypeId::Float3 || element_type == TypeId::Color3f ||
            element_type == TypeId::Point3f || element_type == TypeId::Vector3f ||
            element_type == TypeId::Normal3f) {
            return Value::from_float3_array(float_values.data(), float_values.size() / 3);
        }
        if (element_type == TypeId::Float2 || element_type == TypeId::TexCoord2f) {
            return Value::from_float2_array(float_values.data(), float_values.size() / 2);
        }
        return Value::from_float_array(float_values.data(), float_values.size());
    }
    if (!int_values.empty()) {
        return Value::from_int32_array(int_values.data(), int_values.size());
    }
    if (!string_values.empty()) {
        return Value::from_string_array(string_values);
    }

    // Empty array
    return Value::make_null();
}

Result<Value> Parser::parse_dict_value() {
    if (!expect(TokenType::LBrace, "Expected '{'")) {
        return Error("Expected '{'");
    }

    // For now, skip dictionary contents
    int depth = 1;
    while (depth > 0 && !check(TokenType::Eof)) {
        if (check(TokenType::LBrace)) depth++;
        else if (check(TokenType::RBrace)) depth--;
        advance();
    }

    return Value::make_null();
}

// ============================================================================
// Time Samples
// ============================================================================

bool Parser::parse_time_samples(TypeId type, TimeSamples& ts) {
    if (!expect(TokenType::LBrace, "Expected '{'")) {
        return false;
    }

    while (!check(TokenType::RBrace) && !check(TokenType::Eof) && !has_errors()) {
        // Time value
        double time = 0.0;
        if (check(TokenType::Integer)) {
            time = static_cast<double>(current_.int_value);
            advance();
        } else if (check(TokenType::Float)) {
            time = current_.float_value;
            advance();
        } else {
            error("Expected time value");
            return false;
        }

        if (!expect(TokenType::Colon, "Expected ':'")) {
            return false;
        }

        // Sample value
        auto value_result = parse_value(type);
        if (!value_result.ok()) {
            error(value_result.error().message);
            return false;
        }

        ts.add_sample(time, value_result.value());

        match(TokenType::Comma);
    }

    return expect(TokenType::RBrace, "Expected '}'");
}

// ============================================================================
// Connections
// ============================================================================

bool Parser::parse_connections(std::vector<Path>& paths) {
    // None
    if (check(TokenType::Kw_None)) {
        advance();
        return true;
    }

    // Array of paths
    if (check(TokenType::LBracket)) {
        advance();
        while (!check(TokenType::RBracket) && !check(TokenType::Eof)) {
            if (check(TokenType::String) || check(TokenType::PathRef)) {
                paths.push_back(Path(current_.str_value));
                advance();
            } else {
                error("Expected path");
                return false;
            }
            match(TokenType::Comma);
        }
        return expect(TokenType::RBracket, "Expected ']'");
    }

    // Single path (can be string "/path" or pathref </path>)
    if (check(TokenType::String) || check(TokenType::PathRef)) {
        paths.push_back(Path(current_.str_value));
        advance();
        return true;
    }

    error("Expected path or [paths]");
    return false;
}

// ============================================================================
// Skip Value (for unknown metadata)
// ============================================================================

void Parser::skip_value() {
    // Skip a complex value (brackets, braces, etc.)
    int depth = 0;

    // Handle opening brackets
    if (check(TokenType::LParen) || check(TokenType::LBracket) ||
        check(TokenType::LBrace)) {
        depth = 1;
        advance();
        while (depth > 0 && !check(TokenType::Eof)) {
            if (check(TokenType::LParen) || check(TokenType::LBracket) ||
                check(TokenType::LBrace)) {
                depth++;
            } else if (check(TokenType::RParen) || check(TokenType::RBracket) ||
                       check(TokenType::RBrace)) {
                depth--;
            }
            if (depth > 0) {
                advance();
            }
        }
        if (depth == 0) {
            advance();  // consume closing bracket
        }
        return;
    }

    // Handle simple values
    if (check(TokenType::String) || check(TokenType::Integer) ||
        check(TokenType::Float) || check(TokenType::Identifier) ||
        check(TokenType::Kw_true) || check(TokenType::Kw_false) ||
        check(TokenType::Kw_None)) {
        advance();
    }
}

// ============================================================================
// Reference List Parsing
// ============================================================================

// Helper to parse a single reference: @asset@</path>(offset=...; scale=...)
static bool parse_single_reference(Parser* p, LexToken& current, Reference& ref,
                                   std::function<bool(TokenType)> check_fn,
                                   std::function<void()> advance_fn,
                                   std::function<bool(TokenType, const char*)> expect_fn) {
    // Parse @asset@ or </path> or @asset@</path>
    std::string asset_path;
    std::string prim_path_str;

    // Asset path: @...@
    if (current.type == TokenType::AssetPath) {
        asset_path = current.str_value;
        advance_fn();
    }

    // Prim path: </...> or <...>
    if (current.type == TokenType::PathRef) {
        prim_path_str = current.str_value;
        advance_fn();
    }

    // Optional layer offset: (offset = 10; scale = 2)
    LayerOffset layer_offset;
    if (check_fn(TokenType::LParen)) {
        advance_fn();
        while (!check_fn(TokenType::RParen) && !check_fn(TokenType::Eof)) {
            if (current.type == TokenType::Identifier) {
                std::string key = current.str_value;
                advance_fn();
                if (!expect_fn(TokenType::Equals, "Expected '='")) {
                    return false;
                }
                if (key == "offset") {
                    if (current.type == TokenType::Integer) {
                        layer_offset.offset = static_cast<double>(current.int_value);
                    } else if (current.type == TokenType::Float) {
                        layer_offset.offset = current.float_value;
                    }
                    advance_fn();
                } else if (key == "scale") {
                    if (current.type == TokenType::Integer) {
                        layer_offset.scale = static_cast<double>(current.int_value);
                    } else if (current.type == TokenType::Float) {
                        layer_offset.scale = current.float_value;
                    }
                    advance_fn();
                }
            }
            // Skip semicolons between properties
            if (check_fn(TokenType::Semicolon)) {
                advance_fn();
            }
        }
        if (!expect_fn(TokenType::RParen, "Expected ')'")) {
            return false;
        }
    }

    ref.asset_path = asset_path;
    ref.prim_path = prim_path_str.empty() ? Path() : Path(prim_path_str);
    ref.layer_offset = layer_offset;
    return true;
}

bool Parser::parse_reference_list(Prim& prim, ListEditOp op) {
    std::vector<Reference> refs;

    // None
    if (check(TokenType::Kw_None)) {
        advance();
        if (op == ListEditOp::Explicit) {
            prim.references_mutable().set_explicit(refs);
        }
        return true;
    }

    // Array of references
    if (check(TokenType::LBracket)) {
        advance();
        while (!check(TokenType::RBracket) && !check(TokenType::Eof)) {
            Reference ref;
            if (!parse_single_reference(this, current_, ref,
                    [this](TokenType t) { return check(t); },
                    [this]() { advance(); },
                    [this](TokenType t, const char* msg) { return expect(t, msg); })) {
                return false;
            }
            refs.push_back(ref);
            match(TokenType::Comma);
        }
        if (!expect(TokenType::RBracket, "Expected ']'")) {
            return false;
        }
    }
    // Single reference
    else if (check(TokenType::AssetPath) || check(TokenType::PathRef)) {
        Reference ref;
        if (!parse_single_reference(this, current_, ref,
                [this](TokenType t) { return check(t); },
                [this]() { advance(); },
                [this](TokenType t, const char* msg) { return expect(t, msg); })) {
            return false;
        }
        refs.push_back(ref);
    }

    // Apply based on list edit operation
    switch (op) {
        case ListEditOp::Explicit:
            prim.references_mutable().set_explicit(refs);
            break;
        case ListEditOp::Prepend:
            prim.references_mutable().set_prepended(refs);
            break;
        case ListEditOp::Append:
            prim.references_mutable().set_appended(refs);
            break;
        case ListEditOp::Delete:
            for (const auto& ref : refs) {
                prim.references_mutable().add_delete(ref);
            }
            break;
        default:
            break;
    }

    return true;
}

// ============================================================================
// Payload List Parsing
// ============================================================================

bool Parser::parse_payload_list(Prim& prim, ListEditOp op) {
    std::vector<Payload> payloads;

    // None
    if (check(TokenType::Kw_None)) {
        advance();
        if (op == ListEditOp::Explicit) {
            prim.payloads_mutable().set_explicit(payloads);
        }
        return true;
    }

    // Array of payloads
    if (check(TokenType::LBracket)) {
        advance();
        while (!check(TokenType::RBracket) && !check(TokenType::Eof)) {
            Payload payload;
            std::string asset_path;
            std::string prim_path_str;

            // Asset path
            if (check(TokenType::AssetPath)) {
                asset_path = current_.str_value;
                advance();
            }

            // Prim path
            if (check(TokenType::PathRef)) {
                prim_path_str = current_.str_value;
                advance();
            }

            // Layer offset
            LayerOffset layer_offset;
            if (check(TokenType::LParen)) {
                advance();
                while (!check(TokenType::RParen) && !check(TokenType::Eof)) {
                    if (check(TokenType::Identifier)) {
                        std::string key = current_.str_value;
                        advance();
                        if (!expect(TokenType::Equals, "Expected '='")) return false;
                        if (key == "offset") {
                            if (check(TokenType::Integer)) {
                                layer_offset.offset = static_cast<double>(current_.int_value);
                            } else if (check(TokenType::Float)) {
                                layer_offset.offset = current_.float_value;
                            }
                            advance();
                        } else if (key == "scale") {
                            if (check(TokenType::Integer)) {
                                layer_offset.scale = static_cast<double>(current_.int_value);
                            } else if (check(TokenType::Float)) {
                                layer_offset.scale = current_.float_value;
                            }
                            advance();
                        }
                    }
                    if (check(TokenType::Semicolon)) advance();
                }
                if (!expect(TokenType::RParen, "Expected ')'")) return false;
            }

            payload.asset_path = asset_path;
            payload.prim_path = prim_path_str.empty() ? Path() : Path(prim_path_str);
            payload.layer_offset = layer_offset;
            payloads.push_back(payload);
            match(TokenType::Comma);
        }
        if (!expect(TokenType::RBracket, "Expected ']'")) {
            return false;
        }
    }
    // Single payload
    else if (check(TokenType::AssetPath) || check(TokenType::PathRef)) {
        Payload payload;
        if (check(TokenType::AssetPath)) {
            payload.asset_path = current_.str_value;
            advance();
        }
        if (check(TokenType::PathRef)) {
            payload.prim_path = Path(current_.str_value);
            advance();
        }
        payloads.push_back(payload);
    }

    // Apply based on list edit operation
    switch (op) {
        case ListEditOp::Explicit:
            prim.payloads_mutable().set_explicit(payloads);
            break;
        case ListEditOp::Prepend:
            prim.payloads_mutable().set_prepended(payloads);
            break;
        case ListEditOp::Append:
            prim.payloads_mutable().set_appended(payloads);
            break;
        default:
            break;
    }

    return true;
}

// ============================================================================
// Path List Parsing (for inherits/specializes)
// ============================================================================

bool Parser::parse_path_list(PathList& path_list, ListEditOp op) {
    std::vector<Path> paths;

    // None
    if (check(TokenType::Kw_None)) {
        advance();
        if (op == ListEditOp::Explicit) {
            path_list.set_explicit(paths);
        }
        return true;
    }

    // Array of paths
    if (check(TokenType::LBracket)) {
        advance();
        while (!check(TokenType::RBracket) && !check(TokenType::Eof)) {
            if (check(TokenType::PathRef)) {
                paths.push_back(Path(current_.str_value));
                advance();
            } else if (check(TokenType::String)) {
                paths.push_back(Path(current_.str_value));
                advance();
            } else {
                error("Expected path");
                return false;
            }
            match(TokenType::Comma);
        }
        if (!expect(TokenType::RBracket, "Expected ']'")) {
            return false;
        }
    }
    // Single path
    else if (check(TokenType::PathRef)) {
        paths.push_back(Path(current_.str_value));
        advance();
    } else if (check(TokenType::String)) {
        paths.push_back(Path(current_.str_value));
        advance();
    }

    // Apply based on list edit operation
    switch (op) {
        case ListEditOp::Explicit:
            path_list.set_explicit(paths);
            break;
        case ListEditOp::Prepend:
            path_list.set_prepended(paths);
            break;
        case ListEditOp::Append:
            path_list.set_appended(paths);
            break;
        default:
            break;
    }

    return true;
}

// ============================================================================
// Variant Set Parsing
// ============================================================================

bool Parser::parse_variant_set(Prim& prim) {
    // variantSet "name" = { "variant1" { ... } "variant2" { ... } }

    // Skip keyword (already checked)
    advance();

    // Variant set name
    if (!check(TokenType::String)) {
        error("Expected variant set name");
        return false;
    }
    std::string vs_name = current_.str_value;
    advance();

    if (!expect(TokenType::Equals, "Expected '='")) {
        return false;
    }

    if (!expect(TokenType::LBrace, "Expected '{'")) {
        return false;
    }

    VariantSet vs;
    vs.set_name(vs_name);

    // Parse variants
    while (!check(TokenType::RBrace) && !check(TokenType::Eof)) {
        // Variant name
        if (!check(TokenType::String)) {
            error("Expected variant name");
            return false;
        }
        std::string variant_name = current_.str_value;
        advance();

        Variant var;
        var.set_name(variant_name);

        // Variant content (prim-like)
        if (check(TokenType::LBrace)) {
            advance();

            // Create a temporary prim to hold variant content
            Prim content_prim;
            content_prim.set_name(variant_name);

            if (!parse_prim_contents(content_prim)) {
                return false;
            }

            if (!expect(TokenType::RBrace, "Expected '}'")) {
                return false;
            }

            var.set_content(std::move(content_prim));
        }

        vs.add_variant(std::move(var));
    }

    if (!expect(TokenType::RBrace, "Expected '}'")) {
        return false;
    }

    prim.add_variant_set(std::move(vs));
    return true;
}

} // namespace v1
} // namespace lightusd
