#pragma once

#include <cstdint>
#include <algorithm>
#include <array>

namespace tinyusdz {
namespace spatial {

inline uint32_t expandBits(uint32_t v) {
    v = (v | (v << 16)) & 0x030000FF;
    v = (v | (v <<  8)) & 0x0300F00F;
    v = (v | (v <<  4)) & 0x030C30C3;
    v = (v | (v <<  2)) & 0x09249249;
    return v;
}

inline uint32_t compactBits(uint32_t v) {
    v &= 0x09249249;
    v = (v | (v >> 2)) & 0x030C30C3;
    v = (v | (v >> 4)) & 0x0300F00F;
    v = (v | (v >> 8)) & 0x030000FF;
    v = (v | (v >> 16)) & 0x000003FF;
    return v;
}

inline uint32_t morton3D(uint32_t x, uint32_t y, uint32_t z) {
    x = std::min(x, uint32_t(0x3FF));
    y = std::min(y, uint32_t(0x3FF));
    z = std::min(z, uint32_t(0x3FF));
    
    uint32_t xx = expandBits(x);
    uint32_t yy = expandBits(y);
    uint32_t zz = expandBits(z);
    
    return (xx << 2) | (yy << 1) | zz;
}

inline void decodeMorton3D(uint32_t code, uint32_t& x, uint32_t& y, uint32_t& z) {
    x = compactBits(code >> 2);
    y = compactBits(code >> 1);
    z = compactBits(code);
}

inline uint64_t expandBits64(uint32_t v) {
    uint64_t x = v;
    x = (x | (x << 32)) & 0x1F00000000FFFFull;
    x = (x | (x << 16)) & 0x1F0000FF0000FFull;
    x = (x | (x <<  8)) & 0x100F00F00F00F00Full;
    x = (x | (x <<  4)) & 0x10C30C30C30C30C3ull;
    x = (x | (x <<  2)) & 0x1249249249249249ull;
    return x;
}

inline uint32_t compactBits64(uint64_t v) {
    v &= 0x1249249249249249ull;
    v = (v | (v >> 2)) & 0x10C30C30C30C30C3ull;
    v = (v | (v >> 4)) & 0x100F00F00F00F00Full;
    v = (v | (v >> 8)) & 0x1F0000FF0000FFull;
    v = (v | (v >> 16)) & 0x1F00000000FFFFull;
    v = (v | (v >> 32)) & 0x00000000001FFFFFull;
    return static_cast<uint32_t>(v);
}

inline uint64_t morton3D64(uint32_t x, uint32_t y, uint32_t z) {
    x = std::min(x, uint32_t(0x1FFFFF));
    y = std::min(y, uint32_t(0x1FFFFF));
    z = std::min(z, uint32_t(0x1FFFFF));
    
    uint64_t xx = expandBits64(x);
    uint64_t yy = expandBits64(y);
    uint64_t zz = expandBits64(z);
    
    return (xx << 2) | (yy << 1) | zz;
}

inline void decodeMorton3D64(uint64_t code, uint32_t& x, uint32_t& y, uint32_t& z) {
    x = compactBits64(code >> 2);
    y = compactBits64(code >> 1);
    z = compactBits64(code);
}

template <typename T>
inline void computeGridCoords(const T pos[3], const T origin[3], T cellSize, 
                              uint32_t& gx, uint32_t& gy, uint32_t& gz) {
    T dx = pos[0] - origin[0];
    T dy = pos[1] - origin[1];
    T dz = pos[2] - origin[2];
    
    gx = static_cast<uint32_t>(std::max(T(0), dx / cellSize));
    gy = static_cast<uint32_t>(std::max(T(0), dy / cellSize));
    gz = static_cast<uint32_t>(std::max(T(0), dz / cellSize));
}

inline std::array<std::array<int, 3>, 27> getNeighborOffsets() {
    std::array<std::array<int, 3>, 27> offsets;
    int idx = 0;
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dz = -1; dz <= 1; dz++) {
                offsets[idx++] = {dx, dy, dz};
            }
        }
    }
    return offsets;
}

} // namespace spatial
} // namespace tinyusdz