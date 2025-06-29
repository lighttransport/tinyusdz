#include "json-writer.hh"
#include "str-util.hh"
#include <string>

// dtoa_milo does not work well for float types
// (e.g. it prints float 0.01 as 0.009999999997),
// so use floaxie for float types
// TODO: Use floaxie also for double?
#include "external/dtoa_milo.h"


namespace tinyusdz {
namespace json {

namespace detail {

inline std::string dtos(const double v) {
  char buf[64];
  dtoa_milo(v, buf);

  return std::string(buf);
}


bool WriteInt(const int value, std::string *out_json) {
  if (!out_json) return false;

  (*out_json) = detail::dtos(double(value));

  return true;
  
}

bool WriteString(const std::string &str, std::string *out_json) {
  if (!out_json) return false;

  // Escape quotes and backslashes
  std::string escaped_str;
  for (char c : str) {
    if (c == '"' || c == '\\') {
      escaped_str += '\\'; // Escape character
    }
    escaped_str += c;
  }

  *out_json += '"' + escaped_str + '"';
  return true;
}

// Base122 encoding using 122 printable ASCII characters
// Excludes: " (34), \ (92), DEL (127), and non-printable control characters
std::string EncodeBase122(const std::vector<uint8_t> &data) {
  // Base122 character set (122 printable ASCII characters)
  static const char kBase122Chars[] = 
    "!#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[]^_`"
    "abcdefghijklmnopqrstuvwxyz{|}~"
    "\x20\x21\x23\x24\x25\x26\x27\x28\x29\x2A\x2B\x2C\x2D\x2E\x2F"
    "\x30\x31\x32\x33\x34\x35\x36\x37\x38\x39\x3A\x3B\x3C\x3D\x3E\x3F";
  
  if (data.empty()) {
    return "";
  }
  
  std::string result;
  result.reserve((data.size() * 8 + 6) / 7); // Approximate output size
  
  uint64_t buffer = 0;
  int bits = 0;
  
  for (uint8_t byte : data) {
    buffer = (buffer << 8) | byte;
    bits += 8;
    
    while (bits >= 7) {
      bits -= 7;
      uint8_t index = (buffer >> bits) & 0x7F; // Extract 7 bits
      if (index < 122) {
        result += kBase122Chars[index];
      } else {
        // Handle overflow by using modulo
        result += kBase122Chars[index % 122];
      }
    }
  }
  
  // Handle remaining bits
  if (bits > 0) {
    uint8_t index = (buffer << (7 - bits)) & 0x7F;
    if (index < 122) {
      result += kBase122Chars[index];
    } else {
      result += kBase122Chars[index % 122];
    }
  }
  
  return result;
}

// Base64 encoding using standard base64 character set
std::string EncodeBase64(const std::vector<uint8_t> &data) {
  static const char kBase64Chars[] = 
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  
  if (data.empty()) {
    return "";
  }
  
  std::string result;
  result.reserve(((data.size() + 2) / 3) * 4); // Exact output size
  
  for (size_t i = 0; i < data.size(); i += 3) {
    uint32_t buffer = 0;
    int padding = 0;
    
    // Pack 3 bytes into 24 bits
    buffer = (data[i] << 16);
    if (i + 1 < data.size()) {
      buffer |= (data[i + 1] << 8);
    } else {
      padding++;
    }
    if (i + 2 < data.size()) {
      buffer |= data[i + 2];
    } else {
      padding++;
    }
    
    // Extract 4 groups of 6 bits each
    result += kBase64Chars[(buffer >> 18) & 0x3F];
    result += kBase64Chars[(buffer >> 12) & 0x3F];
    result += (padding < 2) ? kBase64Chars[(buffer >> 6) & 0x3F] : '=';
    result += (padding < 1) ? kBase64Chars[buffer & 0x3F] : '=';
  }
  
  return result;
}

} // namespace detal

bool JsonWriter::to_json(const tinyusdz::Layer &layer, std::string *out_json) {

  // TODO
  return false;
}

} // namespace json
}  // namespace tinyusdz
