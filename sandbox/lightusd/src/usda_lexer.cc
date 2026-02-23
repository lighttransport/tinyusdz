// SPDX-License-Identifier: Apache-2.0
// Copyright 2024 Light Transport Entertainment Inc.
//
// LightUSD - USDA lexer implementation

#include "lightusd/usda_lexer.hh"
#include <cstring>
#include <cstdlib>

namespace lightusd {
namespace v1 {

// ============================================================================
// LexToken type names
// ============================================================================

const char* token_type_name(TokenType type) {
    switch (type) {
        case TokenType::Eof:         return "EOF";
        case TokenType::Error:       return "Error";
        case TokenType::Integer:     return "Integer";
        case TokenType::Float:       return "Float";
        case TokenType::String:      return "String";
        case TokenType::Identifier:  return "Identifier";
        case TokenType::LParen:      return "(";
        case TokenType::RParen:      return ")";
        case TokenType::LBracket:    return "[";
        case TokenType::RBracket:    return "]";
        case TokenType::LBrace:      return "{";
        case TokenType::RBrace:      return "}";
        case TokenType::Equals:      return "=";
        case TokenType::Colon:       return ":";
        case TokenType::Comma:       return ",";
        case TokenType::Dot:         return ".";
        case TokenType::Semicolon:   return ";";
        case TokenType::At:          return "@";
        case TokenType::LAngle:      return "<";
        case TokenType::RAngle:      return ">";
        case TokenType::AssetPath:   return "AssetPath";
        case TokenType::PathRef:     return "PathRef";
        case TokenType::Kw_def:      return "def";
        case TokenType::Kw_over:     return "over";
        case TokenType::Kw_class:    return "class";
        case TokenType::Kw_rel:      return "rel";
        case TokenType::Kw_uniform:  return "uniform";
        case TokenType::Kw_varying:  return "varying";
        case TokenType::Kw_custom:   return "custom";
        case TokenType::Kw_add:      return "add";
        case TokenType::Kw_delete:   return "delete";
        case TokenType::Kw_append:   return "append";
        case TokenType::Kw_prepend:  return "prepend";
        case TokenType::Kw_reorder:  return "reorder";
        case TokenType::Kw_variantSet: return "variantSet";
        case TokenType::Kw_None:     return "None";
        case TokenType::Kw_true:     return "true";
        case TokenType::Kw_false:    return "false";
        case TokenType::Kw_timeSamples: return "timeSamples";
        case TokenType::Kw_connect:  return "connect";
        case TokenType::Kw_dictionary: return "dictionary";
    }
    return "Unknown";
}

std::string LexToken::to_string() const {
    switch (type) {
        case TokenType::String:
        case TokenType::Identifier:
        case TokenType::AssetPath:
        case TokenType::PathRef:
            return str_value;
        case TokenType::Integer:
            return std::to_string(int_value);
        case TokenType::Float: {
            char buf[32];
            snprintf(buf, sizeof(buf), "%g", float_value);
            return buf;
        }
        default:
            return token_type_name(type);
    }
}

// ============================================================================
// Lexer implementation
// ============================================================================

Lexer::Lexer(const char* source, size_t length)
    : source_(source)
    , length_(length) {
}

Lexer::Lexer(const std::string& source)
    : source_(source.data())
    , length_(source.size()) {
}

char Lexer::current() const {
    if (pos_ >= length_) return '\0';
    return source_[pos_];
}

char Lexer::peek_char(size_t offset) const {
    size_t idx = pos_ + offset;
    if (idx >= length_) return '\0';
    return source_[idx];
}

void Lexer::advance() {
    if (pos_ < length_) {
        if (source_[pos_] == '\n') {
            cursor_.line++;
            cursor_.column = 0;
        } else {
            cursor_.column++;
        }
        cursor_.offset++;
        pos_++;
    }
}

void Lexer::advance_n(size_t n) {
    for (size_t i = 0; i < n; ++i) {
        advance();
    }
}

bool Lexer::match(char c) {
    if (current() == c) {
        advance();
        return true;
    }
    return false;
}

void Lexer::skip_whitespace_and_comments() {
    while (!at_end()) {
        char c = current();

        // Whitespace
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            advance();
            continue;
        }

        // Comment (# to end of line)
        if (c == '#') {
            // Check for magic header "#usda" - don't skip it
            if (pos_ == 0 && length_ > 5 &&
                source_[1] == 'u' && source_[2] == 's' &&
                source_[3] == 'd' && source_[4] == 'a') {
                return;
            }

            while (!at_end() && current() != '\n') {
                advance();
            }
            continue;
        }

        break;
    }
}

LexToken Lexer::make_token(TokenType type) {
    LexToken tok(type, cursor_, cursor_);
    advance();
    tok.end = cursor_;
    return tok;
}

LexToken Lexer::make_error(const std::string& message) {
    LexToken tok(TokenType::Error, cursor_, cursor_);
    tok.str_value = message;
    diagnostics_.error(cursor_, message);
    return tok;
}

TokenType Lexer::lookup_keyword(const std::string& name) {
    // Simple linear lookup - could use hash map for larger keyword sets
    if (name == "def")         return TokenType::Kw_def;
    if (name == "over")        return TokenType::Kw_over;
    if (name == "class")       return TokenType::Kw_class;
    if (name == "rel")         return TokenType::Kw_rel;
    if (name == "uniform")     return TokenType::Kw_uniform;
    if (name == "varying")     return TokenType::Kw_varying;
    if (name == "custom")      return TokenType::Kw_custom;
    if (name == "add")         return TokenType::Kw_add;
    if (name == "delete")      return TokenType::Kw_delete;
    if (name == "append")      return TokenType::Kw_append;
    if (name == "prepend")     return TokenType::Kw_prepend;
    if (name == "reorder")     return TokenType::Kw_reorder;
    if (name == "variantSet")  return TokenType::Kw_variantSet;
    if (name == "None")        return TokenType::Kw_None;
    if (name == "true")        return TokenType::Kw_true;
    if (name == "false")       return TokenType::Kw_false;
    if (name == "timeSamples") return TokenType::Kw_timeSamples;
    if (name == "connect")     return TokenType::Kw_connect;
    if (name == "dictionary")  return TokenType::Kw_dictionary;

    return TokenType::Identifier;
}

LexToken Lexer::next() {
    if (has_peeked_) {
        has_peeked_ = false;
        return peeked_;
    }
    return scan_token();
}

const LexToken& Lexer::peek() {
    if (!has_peeked_) {
        peeked_ = scan_token();
        has_peeked_ = true;
    }
    return peeked_;
}

LexToken Lexer::scan_token() {
    skip_whitespace_and_comments();

    if (at_end()) {
        return LexToken(TokenType::Eof, cursor_, cursor_);
    }

    Cursor start = cursor_;
    char c = current();

    // Single character tokens
    switch (c) {
        case '(': return make_token(TokenType::LParen);
        case ')': return make_token(TokenType::RParen);
        case '[': return make_token(TokenType::LBracket);
        case ']': return make_token(TokenType::RBracket);
        case '{': return make_token(TokenType::LBrace);
        case '}': return make_token(TokenType::RBrace);
        case '=': return make_token(TokenType::Equals);
        case ':': return make_token(TokenType::Colon);
        case ',': return make_token(TokenType::Comma);
        case '.':
            // Check if this is a float like .001
            if (is_digit(peek_char())) {
                return scan_number();
            }
            return make_token(TokenType::Dot);
        case ';': return make_token(TokenType::Semicolon);
        case '>': return make_token(TokenType::RAngle);
    }

    // Asset path @...@
    if (c == '@') {
        return scan_asset_path();
    }

    // Path <...>
    if (c == '<') {
        return scan_path();
    }

    // Number (including .001 style floats, and -inf/-nan/+inf/+nan)
    if (is_digit(c) || (c == '-' && is_digit(peek_char())) ||
        (c == '+' && is_digit(peek_char())) ||
        (c == '.' && is_digit(peek_char())) ||
        (c == '-' && (peek_char() == 'i' || peek_char() == 'n')) ||
        (c == '+' && (peek_char() == 'i' || peek_char() == 'n'))) {
        return scan_number();
    }

    // String
    if (c == '"' || c == '\'') {
        // Check for triple-quoted string
        if (peek_char(1) == c && peek_char(2) == c) {
            return scan_triple_string(c);
        }
        return scan_string(c);
    }

    // Magic header #usda
    if (c == '#' && pos_ == 0) {
        // Read "#usda 1.0"
        LexToken tok(TokenType::Identifier, cursor_, cursor_);
        advance(); // #
        while (!at_end() && current() != '\n') {
            advance();
        }
        tok.end = cursor_;
        tok.str_value = std::string(source_, tok.end.offset);
        return tok;
    }

    // Identifier or keyword
    if (is_alpha(c)) {
        return scan_identifier_or_keyword();
    }

    // Unknown character
    return make_error(std::string("Unexpected character '") + c + "'");
}

LexToken Lexer::scan_number() {
    LexToken tok(TokenType::Integer, cursor_, cursor_);
    size_t start_pos = pos_;

    // Sign
    if (current() == '-' || current() == '+') {
        advance();
    }

    // Integer part
    while (is_digit(current())) {
        advance();
    }

    // Fractional part
    bool is_float = false;
    if (current() == '.' && is_digit(peek_char())) {
        is_float = true;
        advance(); // .
        while (is_digit(current())) {
            advance();
        }
    }

    // Exponent
    if (current() == 'e' || current() == 'E') {
        is_float = true;
        advance();
        if (current() == '+' || current() == '-') {
            advance();
        }
        while (is_digit(current())) {
            advance();
        }
    }

    // Handle inf/nan suffixes (USD supports these)
    if (current() == 'i' || current() == 'I') {
        // inf
        is_float = true;
        advance();
        if (current() == 'n' || current() == 'N') advance();
        if (current() == 'f' || current() == 'F') advance();
    } else if (current() == 'n' || current() == 'N') {
        // nan
        is_float = true;
        advance();
        if (current() == 'a' || current() == 'A') advance();
        if (current() == 'n' || current() == 'N') advance();
    }

    tok.end = cursor_;

    // Parse the number
    std::string num_str(source_ + start_pos, pos_ - start_pos);
    if (is_float) {
        tok.type = TokenType::Float;
        tok.float_value = strtod(num_str.c_str(), nullptr);
    } else {
        tok.int_value = strtoll(num_str.c_str(), nullptr, 10);
    }

    return tok;
}

LexToken Lexer::scan_string(char quote) {
    LexToken tok(TokenType::String, cursor_, cursor_);
    advance(); // Opening quote

    std::string value;
    while (!at_end() && current() != quote) {
        if (current() == '\\') {
            advance();
            if (at_end()) break;

            // Escape sequences
            switch (current()) {
                case 'n':  value += '\n'; break;
                case 'r':  value += '\r'; break;
                case 't':  value += '\t'; break;
                case '\\': value += '\\'; break;
                case '"':  value += '"';  break;
                case '\'': value += '\''; break;
                default:   value += current(); break;
            }
            advance();
        } else if (current() == '\n') {
            // Unterminated string
            tok.end = cursor_;
            tok.str_value = value;
            diagnostics_.error(tok.start, "Unterminated string literal");
            return tok;
        } else {
            value += current();
            advance();
        }
    }

    if (at_end()) {
        tok.end = cursor_;
        tok.str_value = value;
        diagnostics_.error(tok.start, "Unterminated string literal");
        return tok;
    }

    advance(); // Closing quote
    tok.end = cursor_;
    tok.str_value = value;
    return tok;
}

LexToken Lexer::scan_triple_string(char quote) {
    LexToken tok(TokenType::String, cursor_, cursor_);
    advance(); advance(); advance(); // Opening """

    std::string value;
    while (!at_end()) {
        if (current() == quote && peek_char(1) == quote && peek_char(2) == quote) {
            advance(); advance(); advance(); // Closing """
            tok.end = cursor_;
            tok.str_value = value;
            return tok;
        }

        if (current() == '\\') {
            advance();
            if (at_end()) break;
            switch (current()) {
                case 'n':  value += '\n'; break;
                case 'r':  value += '\r'; break;
                case 't':  value += '\t'; break;
                case '\\': value += '\\'; break;
                case '"':  value += '"';  break;
                case '\'': value += '\''; break;
                default:   value += current(); break;
            }
            advance();
        } else {
            value += current();
            advance();
        }
    }

    tok.end = cursor_;
    tok.str_value = value;
    diagnostics_.error(tok.start, "Unterminated triple-quoted string");
    return tok;
}

LexToken Lexer::scan_identifier_or_keyword() {
    LexToken tok(TokenType::Identifier, cursor_, cursor_);

    std::string name;
    while (is_alnum(current()) || current() == ':') {
        // Allow namespaced identifiers like "primvars:st"
        name += current();
        advance();
    }

    tok.end = cursor_;
    tok.type = lookup_keyword(name);
    tok.str_value = name;
    return tok;
}

LexToken Lexer::scan_asset_path() {
    LexToken tok(TokenType::AssetPath, cursor_, cursor_);
    advance(); // Opening @

    // Check for triple @@@ delimiter
    bool triple = false;
    if (current() == '@' && peek_char() == '@') {
        triple = true;
        advance(); advance();
    }

    std::string value;
    while (!at_end()) {
        // Handle escape sequences
        if (current() == '\\') {
            advance();
            if (!at_end()) {
                if (triple && current() == '@' && peek_char(1) == '@' && peek_char(2) == '@') {
                    // \@@@ -> @@@ (escaped triple @)
                    value += "@@@";
                    advance(); advance(); advance();
                } else {
                    // \@ -> @ (escaped @)
                    // \\ -> \ (escaped backslash)
                    value += current();
                    advance();
                }
            }
            continue;
        }

        if (triple) {
            if (current() == '@' && peek_char(1) == '@' && peek_char(2) == '@') {
                advance(); advance(); advance();
                break;
            }
        } else {
            if (current() == '@') {
                advance();
                break;
            }
        }
        value += current();
        advance();
    }

    tok.end = cursor_;
    tok.str_value = value;
    return tok;
}

LexToken Lexer::scan_path() {
    LexToken tok(TokenType::PathRef, cursor_, cursor_);
    advance(); // Opening <

    std::string value;
    int depth = 1;
    while (!at_end() && depth > 0) {
        if (current() == '<') {
            depth++;
            value += current();
        } else if (current() == '>') {
            depth--;
            if (depth > 0) {
                value += current();
            }
        } else {
            value += current();
        }
        advance();
    }

    tok.end = cursor_;
    tok.str_value = value;
    return tok;
}

} // namespace v1
} // namespace lightusd
