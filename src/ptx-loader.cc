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
uint16_t U16(const uint8_t* p) { return uint16_t(p[0]) | uint16_t(p[1]) << 8; }
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
  if (dstSize > static_cast<size_t>(std::numeric_limits<mz_ulong>::max())) {
    if (err) *err = "Ptex block is too large";
    return false;
  }
  dst->resize(dstSize);
  mz_ulong n = static_cast<mz_ulong>(dstSize);
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
  if (level != 0) return Fail(err, "Ptex reduced levels are not enabled yet");
  const Level& li = levels_[level];
  if (face >= li.faces) return Fail(err, "Ptex face is absent from mip level");
  if (li.headerSize == 0 || li.headerSize > li.size) return Fail(err, "Ptex level header is invalid");
  const size_t levelBase = static_cast<size_t>(levelDataOffset_ + li.offset);
  if (!Range(levelBase, static_cast<size_t>(li.size), size_)) return Fail(err, "Ptex level block is truncated");
  const uint8_t* levelBytes = data_ + levelBase;
  std::vector<uint8_t> header;
  if (!Inflate(levelBytes, li.headerSize, size_t(li.faces) * 4, &header, err)) return false;
  size_t payload = li.headerSize;
  for (uint32_t i = 0; i < face; ++i) {
    const uint32_t packed = U32(header.data() + size_t(i) * 4);
    const uint32_t encoding = packed >> 30;
    const size_t n = packed & 0x3fffffffu;
    if (encoding == 0) continue;
    if (!Add(payload, n, &payload) || payload > li.size) return Fail(err, "Ptex face payload overflow");
  }
  const uint32_t packed = U32(header.data() + size_t(face) * 4);
  const uint32_t encoding = packed >> 30;
  const size_t compressedSize = packed & 0x3fffffffu;
  if (encoding == 0) return Fail(err, "Ptex non-constant face has no payload");
  if (encoding == 3) return Fail(err, "Ptex tiled face decoding is not enabled yet");
  if (encoding != 1) return Fail(err, "Ptex differenced face decoding is not enabled yet");
  if (!Range(levelBase + payload, compressedSize, size_)) return Fail(err, "Ptex face payload is truncated");
  if (!Inflate(levelBytes + payload, compressedSize, bytes, &out->data, err)) return false;
  return true;
}

}  // namespace ptx
}  // namespace tinyusdz
