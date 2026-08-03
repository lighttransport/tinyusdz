// SPDX-License-Identifier: Apache-2.0
#include "ptx-loader.hh"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <limits>

#include "external/miniz.h"

namespace tinyusdz {
namespace ptx {
namespace {

bool Add(size_t a, size_t b, size_t* out) {
  if (b > std::numeric_limits<size_t>::max() - a) return false;
  *out = a + b;
  return true;
}
bool Mul(size_t a, size_t b, size_t* out) {
  if (a && b > std::numeric_limits<size_t>::max() / a) return false;
  *out = a * b;
  return true;
}
bool Range(size_t off, size_t n, size_t size) {
  size_t end = 0;
  return Add(off, n, &end) && end <= size;
}
uint16_t U16(const uint8_t* p) {
  return static_cast<uint16_t>(static_cast<uint16_t>(p[0]) |
                              (static_cast<uint16_t>(p[1]) << 8));
}
uint32_t U32(const uint8_t* p) {
  return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
         uint32_t(p[3]) << 24;
}
uint64_t U64(const uint8_t* p) {
  return uint64_t(U32(p)) | uint64_t(U32(p + 4)) << 32;
}
size_t BytesPerComponent(DataType t) {
  return t == DataType::UInt8 ? 1 : (t == DataType::UInt16 || t == DataType::Half) ? 2 : 4;
}
bool Inflate(const uint8_t* src, size_t srcSize, size_t dstSize,
             std::vector<uint8_t>* dst, std::string* err) {
  const mz_ulong dstSizeULong = static_cast<mz_ulong>(dstSize);
  if (static_cast<size_t>(dstSizeULong) != dstSize) {
    if (err) *err = "Ptex block is too large";
    return false;
  }
  dst->resize(dstSize);
  mz_ulong n = dstSizeULong;
  const int rc = mz_uncompress(dst->data(), &n, src, static_cast<mz_ulong>(srcSize));
  if (rc != MZ_OK || n != dstSize) {
    if (err) *err = "Ptex deflate block failed";
    dst->clear();
    return false;
  }
  return true;
}
bool Fail(std::string* err, const char* msg) {
  if (err) *err = msg;
  return false;
}
bool PlanarToInterleaved(const std::vector<uint8_t>& planar, uint32_t w,
                         uint32_t h, uint16_t channels, size_t bpc,
                         std::vector<uint8_t>* out, std::string* err) {
  size_t samples = 0;
  if (!Mul(size_t(w), h, &samples) || !Mul(samples, channels, &samples) ||
      !Mul(samples, bpc, &samples) || planar.size() != samples)
    return Fail(err, "Ptex planar face size mismatch");
  out->resize(samples);
  const size_t plane = size_t(w) * h * bpc;
  for (size_t p = 0; p < size_t(w) * h; ++p) {
    for (uint16_t c = 0; c < channels; ++c) {
      std::memcpy(out->data() + (p * channels + c) * bpc,
                  planar.data() + size_t(c) * plane + p * bpc, bpc);
    }
  }
  return true;
}

}  // namespace

bool Reader::OpenFile(const std::string& path, Reader* out, std::string* err) {
  if (!out) return Fail(err, "Ptex output reader is null");
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) return Fail(err, "cannot open Ptex file");
  const std::streamoff n = f.tellg();
  if (n <= 0) return Fail(err, "empty Ptex file");
  out->owned_.resize(static_cast<size_t>(n));
  f.seekg(0, std::ios::beg);
  if (!f.read(reinterpret_cast<char*>(out->owned_.data()), n)) {
    return Fail(err, "cannot read Ptex file");
  }
  out->data_ = out->owned_.data();
  out->size_ = out->owned_.size();
  return out->Parse(err);
}

bool Reader::OpenMemory(const uint8_t* data, size_t size, Reader* out,
                        std::string* err) {
  if (!out || (!data && size)) return Fail(err, "invalid Ptex memory");
  out->owned_.clear();
  out->data_ = data;
  out->size_ = size;
  return out->Parse(err);
}

bool Reader::Parse(std::string* err) {
  info_ = Info();
  constantData_.clear();
  levels_.clear();
  if (!data_ || size_ < 64) return Fail(err, "Ptex header is truncated");
  if (std::memcmp(data_, "Ptex", 4) != 0) return Fail(err, "invalid Ptex magic");
  if (U32(data_ + 4) != 1) return Fail(err, "unsupported Ptex major version");
  info_.meshType = U32(data_ + 8);
  const uint32_t type = U32(data_ + 12);
  if (type > 3) return Fail(err, "unsupported Ptex data type");
  info_.dataType = static_cast<DataType>(type);
  info_.alphaChannel = U32(data_ + 16);
  info_.channels = U16(data_ + 20);
  info_.levels = U16(data_ + 22);
  info_.faces = U32(data_ + 24);
  const uint32_t extSize = U32(data_ + 28);
  const uint32_t faceSize = U32(data_ + 32);
  const uint32_t constSize = U32(data_ + 36);
  const uint32_t levelInfoSize = U32(data_ + 40);
  const uint64_t levelDataSize = U64(data_ + 48);
  (void)U32(data_ + 56);  // metadata is intentionally left lazy
  if (!info_.channels || !info_.levels || !info_.faces || info_.faces > 100000000u)
    return Fail(err, "invalid Ptex dimensions");
  size_t expected = 0;
  if (!Mul(info_.faces, size_t(20), &expected) || expected > (1ull << 31))
    return Fail(err, "Ptex face table is too large");
  size_t off = 64;
  if (!Range(off, extSize, size_)) return Fail(err, "Ptex extended header truncated");
  off += extSize;
  if (!Range(off, faceSize, size_) || !Inflate(data_ + off, faceSize, expected,
                                                &levelInfoBytes_, err))
    return Fail(err, "Ptex face-info block is invalid");
  info_.faceInfo.resize(info_.faces);
  for (uint32_t i = 0; i < info_.faces; ++i) {
    const uint8_t* p = levelInfoBytes_.data() + size_t(i) * 20;
    FaceInfo& fi = info_.faceInfo[i];
    fi.ures = p[0]; fi.vres = p[1]; fi.adjEdges = p[2]; fi.flags = p[3];
    for (int e = 0; e < 4; ++e) fi.adjFaces[e] = U32(p + 4 + e * 4);
  }
  off += faceSize;
  size_t constExpected = 0;
  if (!Mul(size_t(info_.faces), size_t(info_.channels), &constExpected) ||
      !Mul(constExpected, BytesPerComponent(info_.dataType), &constExpected) ||
      constExpected > (1ull << 31) || !Range(off, constSize, size_))
    return Fail(err, "Ptex constant block is invalid");
  if (!Inflate(data_ + off, constSize, constExpected, &constantData_, err))
    return false;
  off += constSize;
  size_t levelInfoExpected = 0;
  if (!Mul(size_t(info_.levels), size_t(16), &levelInfoExpected) ||
      levelInfoSize != levelInfoExpected || !Range(off, levelInfoSize, size_))
    return Fail(err, "Ptex level-info block is invalid");
  levels_.resize(info_.levels);
  uint64_t dataCursor = 0;
  for (uint16_t i = 0; i < info_.levels; ++i) {
    const uint8_t* p = data_ + off + size_t(i) * 16;
    levels_[i].size = U64(p);
    levels_[i].headerSize = U32(p + 8);
    levels_[i].faces = U32(p + 12);
    levels_[i].offset = dataCursor;
    if (levels_[i].headerSize > levels_[i].size) return Fail(err, "invalid Ptex level header");
    if (levels_[i].faces > info_.faces) return Fail(err, "invalid Ptex level face count");
    if (levels_[i].size > std::numeric_limits<uint64_t>::max() - dataCursor)
      return Fail(err, "Ptex level data overflow");
    dataCursor += levels_[i].size;
  }
  off += levelInfoSize;
  if (dataCursor != levelDataSize || !Range(off, static_cast<size_t>(levelDataSize), size_))
    return Fail(err, "Ptex level data is truncated");
  levelDataOffset_ = off;
  return true;
}

bool Reader::ReadFace(uint32_t face, uint32_t level, size_t max_bytes,
                      FaceImage* out, std::string* err) const {
  if (!out || face >= info_.faces || level >= levels_.size()) return Fail(err, "invalid Ptex face/level");
  const FaceInfo& fi = info_.faceInfo[face];
  const uint32_t w = std::max(1u, fi.width() >> level);
  const uint32_t h = std::max(1u, fi.height() >> level);
  size_t bytes = 0;
  if (!Mul(size_t(w), size_t(h), &bytes) || !Mul(bytes, info_.channels, &bytes) ||
      !Mul(bytes, BytesPerComponent(info_.dataType), &bytes) || bytes > max_bytes)
    return Fail(err, "Ptex face exceeds decode budget");
  out->width = w; out->height = h; out->channels = info_.channels; out->dataType = info_.dataType;
  out->data.clear();
  if (fi.constant()) {
    const size_t bpc = BytesPerComponent(info_.dataType);
    const size_t src = size_t(face) * info_.channels * bpc;
    if (!Range(src, info_.channels * bpc, constantData_.size())) return Fail(err, "Ptex constant face is truncated");
    out->data.resize(bytes);
    for (size_t p = 0; p < size_t(w) * h; ++p)
      std::memcpy(out->data.data() + p * info_.channels * bpc, constantData_.data() + src, info_.channels * bpc);
    return true;
  }
  const Level& li = levels_[level];
  if (li.headerSize == 0 || li.headerSize > li.size) return Fail(err, "Ptex level header is invalid");
  const size_t levelBase = static_cast<size_t>(levelDataOffset_ + li.offset);
  if (!Range(levelBase, static_cast<size_t>(li.size), size_)) return Fail(err, "Ptex level block is truncated");
  const uint8_t* levelBytes = data_ + levelBase;
  std::vector<uint8_t> header;
  if (!Inflate(levelBytes, li.headerSize, size_t(li.faces) * 4, &header, err)) return false;
  size_t payload = li.headerSize;
  std::vector<uint32_t> order;
  order.reserve(li.faces);
  if (level == 0) {
    for (uint32_t i = 0; i < info_.faces; ++i) order.push_back(i);
  } else {
    for (uint32_t i = 0; i < info_.faces; ++i) {
      if (info_.faceInfo[i].constant()) continue;
      const uint32_t w0 = info_.faceInfo[i].width();
      const uint32_t h0 = info_.faceInfo[i].height();
      if (std::max(1u, w0 >> level) == 0 || std::max(1u, h0 >> level) == 0) continue;
      order.push_back(i);
    }
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
      const uint32_t amin = std::min(info_.faceInfo[a].width(), info_.faceInfo[a].height());
      const uint32_t bmin = std::min(info_.faceInfo[b].width(), info_.faceInfo[b].height());
      if ((amin >> level) != (bmin >> level)) return (amin >> level) > (bmin >> level);
      return a < b;
    });
  }
  auto it = std::find(order.begin(), order.end(), face);
  if (it == order.end() || static_cast<size_t>(it - order.begin()) >= li.faces)
    return Fail(err, "Ptex face is absent from mip level");
  const uint32_t ordinal = static_cast<uint32_t>(it - order.begin());
  for (uint32_t i = 0; i < ordinal; ++i) {
    const uint32_t packed = U32(header.data() + size_t(i) * 4);
    const uint32_t encoding = packed >> 30;
    const size_t n = packed & 0x3fffffffu;
    if (encoding == 0) continue;
    if (!Add(payload, n, &payload) || payload > li.size) return Fail(err, "Ptex face payload overflow");
  }
  const uint32_t packed = U32(header.data() + size_t(ordinal) * 4);
  const uint32_t encoding = packed >> 30;
  const size_t compressedSize = packed & 0x3fffffffu;
  if (encoding == 0) return Fail(err, "Ptex non-constant face has no payload");
  if (encoding == 3) {
    if (compressedSize < 6 || !Range(levelBase + payload, compressedSize, size_))
      return Fail(err, "Ptex tiled face header is truncated");
    const uint8_t* q = levelBytes + payload;
    const uint32_t tw = 1u << q[0], th = 1u << q[1];
    const uint32_t tileHeaderSize = U32(q + 2);
    const uint32_t nx = (w + tw - 1) / tw, ny = (h + th - 1) / th;
    size_t tileCount = 0;
    if (!Mul(nx, ny, &tileCount) || tileHeaderSize > compressedSize - 6 ||
        tileCount > (1ull << 28)) return Fail(err, "Ptex tile table is invalid");
    std::vector<uint8_t> tileHeader;
    if (!Inflate(q + 6, tileHeaderSize, tileCount * 4, &tileHeader, err)) return false;
    const size_t bpc = BytesPerComponent(info_.dataType);
    out->data.assign(bytes, 0);
    size_t cursor = 6 + tileHeaderSize;
    for (uint32_t ty = 0; ty < ny; ++ty) {
      for (uint32_t tx = 0; tx < nx; ++tx) {
        const uint32_t tp = U32(tileHeader.data() + (size_t(ty) * nx + tx) * 4);
        const uint32_t te = tp >> 30;
        const size_t ts = tp & 0x3fffffffu;
        const uint32_t cw = std::min(tw, w - tx * tw), ch = std::min(th, h - ty * th);
        size_t tileBytes = 0;
        if (!Mul(size_t(cw), ch, &tileBytes) || !Mul(tileBytes, info_.channels, &tileBytes) ||
            !Mul(tileBytes, bpc, &tileBytes) || cursor > compressedSize ||
            ts > compressedSize - cursor)
          return Fail(err, "Ptex tile payload overflow");
        std::vector<uint8_t> tile;
        if (te == 0) {
          const size_t valueBytes = size_t(info_.channels) * bpc;
          if (ts < valueBytes) return Fail(err, "Ptex tile constant is truncated");
          tile.resize(tileBytes);
          for (size_t p = 0; p < size_t(cw) * ch; ++p) std::memcpy(tile.data() + p * valueBytes, levelBytes + payload + cursor, valueBytes);
        } else if (te == 1 || te == 2) {
          if (!Inflate(levelBytes + payload + cursor, ts, tileBytes, &tile, err)) return false;
        } else return Fail(err, "unsupported Ptex tile encoding");
        std::vector<uint8_t> interleaved;
        if (te != 0 && !PlanarToInterleaved(tile, cw, ch, info_.channels, bpc, &interleaved, err)) return false;
        if (te != 0) tile.swap(interleaved);
        for (uint32_t y = 0; y < ch; ++y) {
          const size_t dst = (size_t(ty * th + y) * w + tx * tw) * info_.channels * bpc;
          std::memcpy(out->data.data() + dst, tile.data() + size_t(y) * cw * info_.channels * bpc, size_t(cw) * info_.channels * bpc);
        }
        cursor += ts;
      }
    }
    return true;
  }
  if (encoding != 1 && encoding != 2) return Fail(err, "unsupported Ptex face encoding");
  if (!Range(levelBase + payload, compressedSize, size_)) return Fail(err, "Ptex face payload is truncated");
  std::vector<uint8_t> planar;
  if (!Inflate(levelBytes + payload, compressedSize, bytes, &planar, err)) return false;
  if (encoding == 2) {
    // Ptex differencing is a per-channel prefix sum over typed samples.
    const size_t bpc = BytesPerComponent(info_.dataType);
    const size_t count = size_t(w) * h;
    if (bpc == 1) {
      for (uint16_t c = 0; c < info_.channels; ++c)
        for (size_t i = 1; i < count; ++i) planar[size_t(c) * count + i] = uint8_t(planar[size_t(c) * count + i] + planar[size_t(c) * count + i - 1]);
    } else if (info_.dataType == DataType::UInt16 || info_.dataType == DataType::Half) {
      if (info_.dataType == DataType::Half) return Fail(err, "differenced Ptex half data is unsupported");
      for (uint16_t c = 0; c < info_.channels; ++c) for (size_t i = 1; i < count; ++i) {
        uint16_t a = U16(planar.data() + (size_t(c) * count + i) * 2);
        uint16_t b = U16(planar.data() + (size_t(c) * count + i - 1) * 2);
        a = uint16_t(a + b); planar[(size_t(c) * count + i) * 2] = uint8_t(a); planar[(size_t(c) * count + i) * 2 + 1] = uint8_t(a >> 8);
      }
    } else return Fail(err, "differenced Ptex float data is unsupported");
  }
  return PlanarToInterleaved(planar, w, h, info_.channels, BytesPerComponent(info_.dataType), &out->data, err);
}

}  // namespace ptx
}  // namespace tinyusdz
