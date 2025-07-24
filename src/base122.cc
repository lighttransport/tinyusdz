#include "base122.hh"

// Base122 uses 122 safe printable ASCII characters
// Excluding problematic chars like quotes, backslash, DEL, etc.
static const char base122_alphabet[122] = 
    "!#$%&()*+,-./0123456789:;<=>?@"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ[]^_`"
    "abcdefghijklmnopqrstuvwxyz{|}~"
    "\xA1\xA2\xA3\xA4\xA5\xA6\xA7\xA8\xA9\xAA\xAB\xAC\xAD\xAE\xAF"
    "\xB0\xB1\xB2\xB3\xB4\xB5\xB6\xB7\xB8\xB9\xBA\xBB\xBC\xBD\xBE";

// Build decode map at startup
static uint8_t base122_decode_map[256] = {0};
static bool base122_decode_map_ready = false;
static void base122_init_decode_map() {
    if (base122_decode_map_ready) return;
    for (int i = 0; i < 256; ++i) base122_decode_map[i] = 0xFF;
    for (int i = 0; i < 122; ++i) base122_decode_map[static_cast<unsigned char>(base122_alphabet[i])] = static_cast<uint8_t>(i);
    base122_decode_map_ready = true;
}

// Encode binary data to base122 string
inline std::string base122_encode(const std::vector<uint8_t>& data) {
    std::string out;
    size_t i = 0;
    while (i < data.size()) {
        uint32_t val = 0;
        int bytes = 0;
        for (; bytes < 7 && i < data.size(); ++bytes, ++i) {
            val |= static_cast<uint32_t>(data[i]) << (8 * bytes);
        }
        int chars = (bytes * 8 + 6) / 7;
        for (int c = 0; c < chars; ++c) {
            out += base122_alphabet[val % 122];
            val /= 122;
        }
    }
    return out;
}

// Decode base122 string to binary data
// Returns 0 on success, nonzero on error (invalid char or truncated input)
inline int base122_decode(const std::string& str, std::vector<uint8_t>& out) {
    base122_init_decode_map();
    out.clear();
    size_t i = 0;
    while (i < str.size()) {
        uint32_t val = 0;
        int chars = 0;
        uint32_t mult = 1;
        // Read up to 8 chars (max for 7 bytes)
        for (; chars < 8 && i < str.size(); ++chars, ++i) {
            uint8_t v = base122_decode_map[static_cast<unsigned char>(str[i])];
            if (v == 0xFF) return 1; // Invalid char
            val += v * mult;
            mult *= 122;
        }
        // Output up to 7 bytes
        for (int b = 0; b < 7; ++b) {
            if (val == 0 && (i == str.size() || chars < b+1)) break;
            out.push_back(static_cast<uint8_t>(val & 0xFF));
            val >>= 8;
        }
    }
    return 0;
}
