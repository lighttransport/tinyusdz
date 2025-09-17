// SPDX-License-Identifier: Apache 2.0
// Copyright 2021 - 2025, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.

#include "pretty-print-utils.hh"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>
#include <cstring>
#include <cstdlib>

namespace tinyusdz {
namespace pprint {

// String manipulation utilities
namespace string_utils {

std::string TrimLeft(const std::string &str) {
  size_t start = str.find_first_not_of(" \t\n\r");
  return (start == std::string::npos) ? "" : str.substr(start);
}

std::string TrimRight(const std::string &str) {
  size_t end = str.find_last_not_of(" \t\n\r");
  return (end == std::string::npos) ? "" : str.substr(0, end + 1);
}

std::string Trim(const std::string &str) {
  return TrimLeft(TrimRight(str));
}

std::string PadLeft(const std::string &str, size_t width, char pad) {
  if (str.length() >= width) return str;
  return std::string(width - str.length(), pad) + str;
}

std::string PadRight(const std::string &str, size_t width, char pad) {
  if (str.length() >= width) return str;
  return str + std::string(width - str.length(), pad);
}

std::string Center(const std::string &str, size_t width, char pad) {
  if (str.length() >= width) return str;
  size_t left_pad = (width - str.length()) / 2;
  size_t right_pad = width - str.length() - left_pad;
  return std::string(left_pad, pad) + str + std::string(right_pad, pad);
}

std::string ToLower(const std::string &str) {
  std::string result = str;
  std::transform(result.begin(), result.end(), result.begin(), ::tolower);
  return result;
}

std::string ToUpper(const std::string &str) {
  std::string result = str;
  std::transform(result.begin(), result.end(), result.begin(), ::toupper);
  return result;
}

std::string Capitalize(const std::string &str) {
  if (str.empty()) return str;
  std::string result = str;
  result[0] = ::toupper(result[0]);
  return result;
}

std::string CamelCaseToSnakeCase(const std::string &str) {
  std::string result;
  for (size_t i = 0; i < str.length(); ++i) {
    if (::isupper(str[i])) {
      if (i > 0) result += '_';
      result += ::tolower(str[i]);
    } else {
      result += str[i];
    }
  }
  return result;
}

std::string SnakeCaseToCamelCase(const std::string &str) {
  std::string result;
  bool capitalize_next = false;
  for (char c : str) {
    if (c == '_') {
      capitalize_next = true;
    } else if (capitalize_next) {
      result += ::toupper(c);
      capitalize_next = false;
    } else {
      result += c;
    }
  }
  return result;
}

std::vector<std::string> Split(const std::string &str, char delimiter) {
  std::vector<std::string> result;
  std::stringstream ss(str);
  std::string token;
  
  while (std::getline(ss, token, delimiter)) {
    result.push_back(token);
  }
  
  return result;
}

std::vector<std::string> Split(const std::string &str, const std::string &delimiter) {
  std::vector<std::string> result;
  size_t start = 0;
  size_t end = str.find(delimiter);
  
  while (end != std::string::npos) {
    result.push_back(str.substr(start, end - start));
    start = end + delimiter.length();
    end = str.find(delimiter, start);
  }
  
  result.push_back(str.substr(start));
  return result;
}

std::vector<std::string> SplitLines(const std::string &str) {
  return Split(str, '\n');
}

std::string Join(const std::vector<std::string> &parts, const std::string &delimiter) {
  if (parts.empty()) return "";
  
  std::stringstream ss;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) ss << delimiter;
    ss << parts[i];
  }
  
  return ss.str();
}

std::string JoinLines(const std::vector<std::string> &lines) {
  return Join(lines, "\n");
}

std::string Replace(const std::string &str, 
                   const std::string &from, 
                   const std::string &to) {
  size_t pos = str.find(from);
  if (pos == std::string::npos) return str;
  
  std::string result = str;
  result.replace(pos, from.length(), to);
  return result;
}

std::string ReplaceAll(const std::string &str,
                      const std::string &from,
                      const std::string &to) {
  std::string result = str;
  size_t pos = 0;
  
  while ((pos = result.find(from, pos)) != std::string::npos) {
    result.replace(pos, from.length(), to);
    pos += to.length();
  }
  
  return result;
}

bool StartsWith(const std::string &str, const std::string &prefix) {
  return str.size() >= prefix.size() &&
         str.compare(0, prefix.size(), prefix) == 0;
}

bool EndsWith(const std::string &str, const std::string &suffix) {
  return str.size() >= suffix.size() &&
         str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool Contains(const std::string &str, const std::string &substring) {
  return str.find(substring) != std::string::npos;
}

bool IsNumeric(const std::string &str) {
  if (str.empty()) return false;
  
  size_t start = 0;
  if (str[0] == '-' || str[0] == '+') start = 1;
  
  bool has_dot = false;
  bool has_e = false;
  
  for (size_t i = start; i < str.length(); ++i) {
    if (str[i] == '.') {
      if (has_dot || has_e) return false;
      has_dot = true;
    } else if (str[i] == 'e' || str[i] == 'E') {
      if (has_e || i == start) return false;
      has_e = true;
      if (i + 1 < str.length() && (str[i+1] == '+' || str[i+1] == '-')) {
        i++; // Skip sign after e
      }
    } else if (!::isdigit(str[i])) {
      return false;
    }
  }
  
  return true;
}

bool IsAlpha(const std::string &str) {
  if (str.empty()) return false;
  for (char c : str) {
    if (!::isalpha(c)) return false;
  }
  return true;
}

bool IsAlphanumeric(const std::string &str) {
  if (str.empty()) return false;
  for (char c : str) {
    if (!::isalnum(c)) return false;
  }
  return true;
}

bool IsIdentifier(const std::string &str) {
  if (str.empty()) return false;
  if (!::isalpha(str[0]) && str[0] != '_') return false;
  
  for (size_t i = 1; i < str.length(); ++i) {
    if (!::isalnum(str[i]) && str[i] != '_') return false;
  }
  
  return true;
}

std::string Quote(const std::string &str, const std::string &quote_char) {
  return quote_char + str + quote_char;
}

std::string SingleQuote(const std::string &str) {
  return Quote(str, "'");
}

std::string DoubleQuote(const std::string &str) {
  return Quote(str, "\"");
}

std::string TripleQuote(const std::string &str) {
  return "\"\"\"" + str + "\"\"\"";
}

} // namespace string_utils

// Number formatting utilities
namespace number_utils {

std::string FormatInt(int32_t value, int width, char fill) {
  std::stringstream ss;
  if (width > 0) {
    ss << std::setw(width) << std::setfill(fill);
  }
  ss << value;
  return ss.str();
}

std::string FormatInt64(int64_t value, int width, char fill) {
  std::stringstream ss;
  if (width > 0) {
    ss << std::setw(width) << std::setfill(fill);
  }
  ss << value;
  return ss.str();
}

std::string FormatFloat(float value, int precision, bool use_scientific) {
  std::stringstream ss;
  if (precision >= 0) {
    ss << std::setprecision(precision);
  }
  if (use_scientific) {
    ss << std::scientific;
  } else {
    ss << std::fixed;
  }
  ss << value;
  return ss.str();
}

std::string FormatDouble(double value, int precision, bool use_scientific) {
  std::stringstream ss;
  if (precision >= 0) {
    ss << std::setprecision(precision);
  }
  if (use_scientific) {
    ss << std::scientific;
  } else {
    ss << std::fixed;
  }
  ss << value;
  return ss.str();
}

std::string FormatHex(uint32_t value, bool prefix, bool uppercase) {
  std::stringstream ss;
  if (prefix) ss << "0x";
  ss << std::hex;
  if (uppercase) ss << std::uppercase;
  ss << value;
  return ss.str();
}

std::string FormatBytes(uint64_t bytes, int precision) {
  const char* units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
  const size_t num_units = sizeof(units) / sizeof(units[0]);
  
  double value = static_cast<double>(bytes);
  size_t unit_idx = 0;
  
  while (value >= 1024.0 && unit_idx < num_units - 1) {
    value /= 1024.0;
    unit_idx++;
  }
  
  std::stringstream ss;
  ss << std::fixed << std::setprecision(precision) << value << " " << units[unit_idx];
  return ss.str();
}

bool ParseInt(const std::string &str, int32_t *value) {
  if (!value) return false;
  
  char *end;
  long result = std::strtol(str.c_str(), &end, 10);
  
  if (end == str.c_str() || *end != '\0') return false;
  if (result < INT32_MIN || result > INT32_MAX) return false;
  
  *value = static_cast<int32_t>(result);
  return true;
}

bool ParseDouble(const std::string &str, double *value) {
  if (!value) return false;
  
  char *end;
  double result = std::strtod(str.c_str(), &end);
  
  if (end == str.c_str() || *end != '\0') return false;
  
  *value = result;
  return true;
}

} // namespace number_utils

// Performance utilities
namespace perf_utils {

FastStringBuilder::FastStringBuilder(size_t initial_capacity)
    : size_(0), capacity_(initial_capacity) {
  buffer_ = new char[capacity_];
}

FastStringBuilder::~FastStringBuilder() {
  delete[] buffer_;
}

void FastStringBuilder::EnsureCapacity(size_t needed) {
  if (size_ + needed > capacity_) {
    size_t new_capacity = std::max(capacity_ * 2, size_ + needed);
    char *new_buffer = new char[new_capacity];
    std::memcpy(new_buffer, buffer_, size_);
    delete[] buffer_;
    buffer_ = new_buffer;
    capacity_ = new_capacity;
  }
}

FastStringBuilder& FastStringBuilder::Append(char c) {
  EnsureCapacity(1);
  buffer_[size_++] = c;
  return *this;
}

FastStringBuilder& FastStringBuilder::Append(const char *str) {
  size_t len = std::strlen(str);
  EnsureCapacity(len);
  std::memcpy(buffer_ + size_, str, len);
  size_ += len;
  return *this;
}

FastStringBuilder& FastStringBuilder::Append(const std::string &str) {
  return Append(str.c_str(), str.length());
}

FastStringBuilder& FastStringBuilder::Append(const char *str, size_t len) {
  EnsureCapacity(len);
  std::memcpy(buffer_ + size_, str, len);
  size_ += len;
  return *this;
}

FastStringBuilder& FastStringBuilder::AppendInt(int32_t value) {
  char buffer[32];
  int len = std::snprintf(buffer, sizeof(buffer), "%d", value);
  return Append(buffer, len);
}

FastStringBuilder& FastStringBuilder::AppendFloat(float value, int precision) {
  char buffer[64];
  int len;
  if (precision >= 0) {
    len = std::snprintf(buffer, sizeof(buffer), "%.*f", precision, value);
  } else {
    len = std::snprintf(buffer, sizeof(buffer), "%g", value);
  }
  return Append(buffer, len);
}

void FastStringBuilder::Clear() {
  size_ = 0;
}

void FastStringBuilder::Reserve(size_t capacity) {
  if (capacity > capacity_) {
    char *new_buffer = new char[capacity];
    std::memcpy(new_buffer, buffer_, size_);
    delete[] buffer_;
    buffer_ = new_buffer;
    capacity_ = capacity;
  }
}

std::string FastStringBuilder::ToString() const {
  return std::string(buffer_, size_);
}

} // namespace perf_utils

} // namespace pprint
} // namespace tinyusdz