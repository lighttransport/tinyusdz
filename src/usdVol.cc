// SPDX-License-Identifier: Apache-2.0
#include "usdVol.hh"

#include <fstream>
#include <map>

// tinyvdb is header-only; emit its implementation here (single TU). It reuses
// the repository's vendored miniz (see external/tinyvdb/tinyvdbio.h).
// The vendored library has unused helpers and maybe-uninitialized patterns that
// trip the project's -Werror; suppress those just for the third-party header.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC diagnostic ignored "-Wmaybe-uninitialized"
#endif
#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-private-field"
#pragma clang diagnostic ignored "-Wsign-conversion"
#pragma clang diagnostic ignored "-Wshorten-64-to-32"
#pragma clang diagnostic ignored "-Wsuggest-override"
#pragma clang diagnostic ignored "-Wsuggest-destructor-override"
#pragma clang diagnostic ignored "-Wreserved-identifier"
#pragma clang diagnostic ignored "-Wdocumentation-unknown-command"
#endif
#define TINYVDBIO_IMPLEMENTATION
// Point tinyvdb at this repo's vendored miniz / LZ4 / zstd copies.
#define TINYVDBIO_MINIZ_INCLUDE "../miniz.h"
#define TINYVDBIO_LZ4_INCLUDE "../../lz4/lz4.h"
#define TINYVDBIO_ZSTD_INCLUDE "../zstd.h"
#include "external/tinyvdb/tinyvdbio.h"
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace tinyusdz {
namespace usdVol {

static void ConvertGrids(std::vector<tinyvdb::DenseFloatGrid> &src,
                         std::vector<VDBGrid> *grids) {
  for (size_t i = 0; i < src.size(); i++) {
    tinyvdb::DenseFloatGrid &g = src[i];
    VDBGrid o;
    o.name = g.name;
    o.value_type = "float";
    o.background = g.background;
    for (int a = 0; a < 3; a++) {
      o.origin[a] = g.origin[a];
      o.dim[a] = g.dim[a];
      o.voxel_size[a] = g.voxel_size[a];
      o.world_translation[a] = g.world_translation[a];
    }
    o.data = std::move(g.data);
    grids->push_back(std::move(o));
  }
}

bool ReadVDBFromMemory(const uint8_t *data, size_t len, const std::string &uri,
                       std::vector<VDBGrid> *grids, std::string *warn,
                       std::string *err, size_t max_voxels) {
  if (!grids) {
    if (err) (*err) += "usdVol::ReadVDBFromMemory: `grids` output is null.\n";
    return false;
  }
  if (!data || len == 0) {
    if (err) (*err) += "usdVol::ReadVDBFromMemory: empty buffer (" + uri + ").\n";
    return false;
  }

  tinyvdb::VDBHeader header;
  std::string e2;
  if (tinyvdb::ParseVDBHeader(data, len, &header, &e2) !=
      tinyvdb::TINYVDBIO_SUCCESS) {
    if (err) (*err) += "Failed to parse VDB header (" + uri + "): " + e2 + "\n";
    return false;
  }

  std::map<std::string, tinyvdb::GridDescriptor> gd_map;
  e2.clear();
  if (tinyvdb::ReadGridDescriptors(data, len, header, &gd_map, &e2) !=
      tinyvdb::TINYVDBIO_SUCCESS) {
    if (err)
      (*err) += "Failed to read VDB grid descriptors (" + uri + "): " + e2 + "\n";
    return false;
  }

  std::vector<tinyvdb::DenseFloatGrid> dense;
  std::string w2;
  e2.clear();
  if (tinyvdb::ReadDenseGrids(data, len, header, gd_map, &dense, max_voxels, &w2,
                              &e2) != tinyvdb::TINYVDBIO_SUCCESS) {
    if (err) (*err) += "Failed to decode VDB grids (" + uri + "): " + e2 + "\n";
    return false;
  }
  if (warn && !w2.empty()) (*warn) += w2;

  ConvertGrids(dense, grids);
  return true;
}

bool ReadVDBFromFile(const std::string &filepath, std::vector<VDBGrid> *grids,
                     std::string *warn, std::string *err, size_t max_voxels) {
  std::ifstream ifs(filepath.c_str(), std::ifstream::binary);
  if (!ifs) {
    if (err) (*err) += "Cannot open VDB file: " + filepath + "\n";
    return false;
  }
  ifs.seekg(0, ifs.end);
  std::streamoff sz = ifs.tellg();
  ifs.seekg(0, ifs.beg);
  if (sz <= 0) {
    if (err) (*err) += "Empty or invalid VDB file: " + filepath + "\n";
    return false;
  }

  std::vector<uint8_t> buf(static_cast<size_t>(sz));
  ifs.read(reinterpret_cast<char *>(buf.data()),
           static_cast<std::streamsize>(sz));
  if (!ifs) {
    if (err) (*err) += "Failed to read VDB file: " + filepath + "\n";
    return false;
  }

  return ReadVDBFromMemory(buf.data(), buf.size(), filepath, grids, warn, err,
                           max_voxels);
}

}  // namespace usdVol
}  // namespace tinyusdz
