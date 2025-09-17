// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2025, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.

#include "pprinter-core.hh"
#include "usda-formatter.hh"
#include "json-formatter.hh"
#include <sstream>
#include <algorithm>
#include <cctype>

namespace tinyusdz {
namespace pprint {

// Global indentation string
static std::string g_indent_string = "    ";

// Formatter base implementation
std::string Formatter::Indent(uint32_t level) const {
  std::stringstream ss;
  for (uint32_t i = 0; i < level; i++) {
    ss << config_.indent_string;
  }
  return ss.str();
}

std::string Formatter::Quote(const std::string &str) const {
  return "\"" + str + "\"";
}

std::string Formatter::PathQuote(const std::string &path) const {
  return "<" + path + ">";
}

// Factory implementation
std::unique_ptr<Formatter> FormatterFactory::Create(PrintConfig::Format format) {
  switch (format) {
    case PrintConfig::USDA:
      return CreateUSDA();
    case PrintConfig::JSON:
      return CreateJSON();
    case PrintConfig::COMPACT:
      return CreateCompact();
    default:
      return CreateUSDA();  // Default to USDA
  }
}

std::unique_ptr<Formatter> FormatterFactory::CreateUSDA() {
  return std::make_unique<USDAFormatter>();
}

std::unique_ptr<Formatter> FormatterFactory::CreateJSON() {
  return std::make_unique<JSONFormatter>();
}

std::unique_ptr<Formatter> FormatterFactory::CreateCompact() {
  // For now, return USDA formatter configured for compact output
  auto formatter = std::make_unique<USDAFormatter>();
  PrintConfig config;
  config.format = PrintConfig::COMPACT;
  config.indent_string = "";
  formatter->SetConfig(config);
  return formatter;
}

// Global utilities
std::string Indent(uint32_t n) {
  std::stringstream ss;
  for (uint32_t i = 0; i < n; i++) {
    ss << g_indent_string;
  }
  return ss.str();
}

void SetIndentString(const std::string &s) {
  g_indent_string = s;
}

const std::string& GetIndentString() {
  return g_indent_string;
}

// Quoting utilities
std::string Quote(const std::string &str) {
  return "\"" + str + "\"";
}

std::string SingleQuote(const std::string &str) {
  return "'" + str + "'";
}

std::string TripleQuote(const std::string &str) {
  return "\"\"\"" + str + "\"\"\"";
}

std::string PathQuote(const std::string &path) {
  return "<" + path + ">";
}

std::string AngleBracketQuote(const std::string &str) {
  return "<" + str + ">";
}

// Number formatting utilities
std::string FormatFloat(float value, int precision) {
  std::stringstream ss;
  if (precision >= 0) {
    ss.precision(precision);
    ss << std::fixed;
  }
  ss << value;
  return ss.str();
}

std::string FormatDouble(double value, int precision) {
  std::stringstream ss;
  if (precision >= 0) {
    ss.precision(precision);
    ss << std::fixed;
  }
  ss << value;
  return ss.str();
}

std::string FormatInt(int32_t value) {
  return std::to_string(value);
}

std::string FormatInt64(int64_t value) {
  return std::to_string(value);
}

std::string FormatUInt(uint32_t value) {
  return std::to_string(value);
}

std::string FormatUInt64(uint64_t value) {
  return std::to_string(value);
}

std::string FormatBool(bool value) {
  return value ? "true" : "false";
}

// String escaping utilities
std::string EscapeString(const std::string &str) {
  std::stringstream ss;
  for (char c : str) {
    switch (c) {
      case '\\': ss << "\\\\"; break;
      case '"':  ss << "\\\""; break;
      case '\n': ss << "\\n"; break;
      case '\r': ss << "\\r"; break;
      case '\t': ss << "\\t"; break;
      case '\b': ss << "\\b"; break;
      case '\f': ss << "\\f"; break;
      default:
        if (c >= 0x20 && c <= 0x7E) {
          ss << c;
        } else {
          ss << "\\x" << std::hex << static_cast<int>(static_cast<unsigned char>(c));
        }
    }
  }
  return ss.str();
}

std::string UnescapeString(const std::string &str) {
  std::stringstream ss;
  bool escaping = false;
  
  for (size_t i = 0; i < str.length(); ++i) {
    if (escaping) {
      switch (str[i]) {
        case '\\': ss << '\\'; break;
        case '"':  ss << '"'; break;
        case 'n':  ss << '\n'; break;
        case 'r':  ss << '\r'; break;
        case 't':  ss << '\t'; break;
        case 'b':  ss << '\b'; break;
        case 'f':  ss << '\f'; break;
        case 'x': {
          // Hex escape
          if (i + 2 < str.length()) {
            std::string hex = str.substr(i + 1, 2);
            int value;
            std::stringstream hexss;
            hexss << std::hex << hex;
            hexss >> value;
            ss << static_cast<char>(value);
            i += 2;
          }
          break;
        }
        default:
          ss << str[i];
      }
      escaping = false;
    } else if (str[i] == '\\') {
      escaping = true;
    } else {
      ss << str[i];
    }
  }
  
  return ss.str();
}

std::string EscapeForUSDA(const std::string &str) {
  // USDA has specific escaping rules
  std::stringstream ss;
  for (char c : str) {
    switch (c) {
      case '\\': ss << "\\\\"; break;
      case '"':  ss << "\\\""; break;
      case '\n': ss << "\\n"; break;
      case '\r': ss << "\\r"; break;
      case '\t': ss << "\\t"; break;
      default:
        if (c >= 0x20 && c <= 0x7E) {
          ss << c;
        } else {
          // USDA uses \x for non-printable
          ss << "\\x" << std::hex << static_cast<int>(static_cast<unsigned char>(c));
        }
    }
  }
  return ss.str();
}

std::string EscapeForJSON(const std::string &str) {
  // JSON has specific escaping rules
  std::stringstream ss;
  for (char c : str) {
    switch (c) {
      case '\\': ss << "\\\\"; break;
      case '"':  ss << "\\\""; break;
      case '/':  ss << "\\/"; break;  // Optional but common
      case '\n': ss << "\\n"; break;
      case '\r': ss << "\\r"; break;
      case '\t': ss << "\\t"; break;
      case '\b': ss << "\\b"; break;
      case '\f': ss << "\\f"; break;
      default:
        if (c >= 0x20 && c <= 0x7E) {
          ss << c;
        } else {
          // JSON uses \uXXXX for Unicode
          ss << "\\u" << std::hex << std::setfill('0') << std::setw(4) 
             << static_cast<int>(static_cast<unsigned char>(c));
        }
    }
  }
  return ss.str();
}

// Line wrapping utilities
std::string WrapLine(const std::string &text, size_t max_width, 
                    const std::string &indent) {
  if (text.length() + indent.length() <= max_width) {
    return indent + text;
  }
  
  std::stringstream result;
  std::stringstream current_line;
  std::istringstream words(text);
  std::string word;
  bool first_line = true;
  
  current_line << indent;
  
  while (words >> word) {
    size_t current_length = current_line.str().length();
    size_t word_length = word.length();
    
    if (current_length + word_length + 1 > max_width && current_length > indent.length()) {
      // Start new line
      result << current_line.str() << "\n";
      current_line.str("");
      current_line << indent;
      first_line = false;
    }
    
    if (!first_line || current_length > indent.length()) {
      current_line << " ";
    }
    current_line << word;
  }
  
  // Add remaining content
  if (current_line.str().length() > indent.length()) {
    result << current_line.str();
  }
  
  return result.str();
}

std::vector<std::string> WrapLines(const std::string &text, size_t max_width) {
  std::vector<std::string> lines;
  std::istringstream stream(text);
  std::string line;
  
  while (std::getline(stream, line)) {
    if (line.length() <= max_width) {
      lines.push_back(line);
    } else {
      // Wrap long line
      size_t pos = 0;
      while (pos < line.length()) {
        size_t end = std::min(pos + max_width, line.length());
        
        // Try to break at word boundary
        if (end < line.length()) {
          size_t space = line.rfind(' ', end);
          if (space != std::string::npos && space > pos) {
            end = space;
          }
        }
        
        lines.push_back(line.substr(pos, end - pos));
        pos = end;
        
        // Skip space at beginning of next line
        if (pos < line.length() && line[pos] == ' ') {
          pos++;
        }
      }
    }
  }
  
  return lines;
}

// Comment formatting
std::string FormatComment(const std::string &comment, uint32_t indent) {
  std::stringstream ss;
  std::string indent_str = Indent(indent);
  
  std::istringstream stream(comment);
  std::string line;
  
  while (std::getline(stream, line)) {
    ss << indent_str << "# " << line << "\n";
  }
  
  return ss.str();
}

std::string FormatMultilineComment(const std::string &comment, uint32_t indent) {
  std::stringstream ss;
  std::string indent_str = Indent(indent);
  
  ss << indent_str << "'''\n";
  
  std::istringstream stream(comment);
  std::string line;
  
  while (std::getline(stream, line)) {
    ss << indent_str << line << "\n";
  }
  
  ss << indent_str << "'''\n";
  
  return ss.str();
}

std::string FormatDocString(const std::string &doc, uint32_t indent) {
  if (doc.find('\n') != std::string::npos) {
    // Multi-line doc string
    return FormatMultilineComment(doc, indent);
  } else {
    // Single line doc string
    return Indent(indent) + "doc = " + Quote(doc) + "\n";
  }
}

} // namespace pprint
} // namespace tinyusdz