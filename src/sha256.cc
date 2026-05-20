#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <cstdint>

#include "sha256.hh"

namespace tinyusdz {

namespace {

constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

inline uint32_t rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

inline uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

inline uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

inline uint32_t sigma0(uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

inline uint32_t sigma1(uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

inline uint32_t gamma0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

inline uint32_t gamma1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

inline uint32_t pack32(const uint8_t *str) {
    return (static_cast<uint32_t>(str[0]) << 24) |
           (static_cast<uint32_t>(str[1]) << 16) |
           (static_cast<uint32_t>(str[2]) << 8) |
           static_cast<uint32_t>(str[3]);
}

void sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t W[64];
    uint32_t a, b, c, d, e, f, g, h;
    uint32_t T1, T2;

    for (int i = 0; i < 16; i++) {
        W[i] = pack32(&block[i * 4]);
    }

    for (int i = 16; i < 64; i++) {
        W[i] = gamma1(W[i - 2]) + W[i - 7] + gamma0(W[i - 15]) + W[i - 16];
    }

    a = state[0];
    b = state[1];
    c = state[2];
    d = state[3];
    e = state[4];
    f = state[5];
    g = state[6];
    h = state[7];

    for (int i = 0; i < 64; i++) {
        T1 = h + sigma1(e) + ch(e, f, g) + K[i] + W[i];
        T2 = sigma0(a) + maj(a, b, c);
        h = g;
        g = f;
        f = e;
        e = d + T1;
        d = c;
        c = b;
        b = a;
        a = T1 + T2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

}  // anonymous namespace

std::string sha256(const char *binary, size_t size) {
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    // Guard against overflow in allocation: new_len can be up to size + 64,
    // and we need new_len + 8 bytes. SIZE_MAX is a realistic limit on 32-bit.
    if (size > (std::numeric_limits<size_t>::max)() - 72) {
        return "";
    }

    const uint8_t *data = reinterpret_cast<const uint8_t *>(binary);
    size_t bit_len = size * 8;
    size_t new_len = size;

    new_len += 1;
    while (new_len % 64 != 56) {
        new_len++;
    }

    uint8_t *msg = new uint8_t[new_len + 8];
    memcpy(msg, data, size);
    msg[size] = 0x80;

    for (size_t i = size + 1; i < new_len; i++) {
        msg[i] = 0;
    }

    for (int i = 0; i < 8; i++) {
        msg[new_len + size_t(i)] = static_cast<uint8_t>((bit_len >> (56 - i * 8)) & 0xff);
    }

    for (size_t i = 0; i < new_len + 8; i += 64) {
        sha256_transform(state, &msg[i]);
    }

    delete[] msg;

    std::stringstream ss;
    for (int i = 0; i < 8; i++) {
        ss << std::hex << std::setfill('0') << std::setw(8) << state[i];
    }

    return ss.str();
}

}  // namespace tinyusdz
