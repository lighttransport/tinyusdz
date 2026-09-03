// SPDX-License-Identifier: Apache-2.0
#include "usdVol.hh"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

// TinyVDB's C implementation is compiled separately in tinyvdb_io.c. Keep
// this C++ translation unit limited to the public C declarations.
#include "external/tinyvdb/tinyvdb_io.h"

namespace lightusd {
namespace usdVol {

namespace {

struct LeafView {
  const tvdb_leaf_node_t *leaf{nullptr};
  int32_t origin[3]{0, 0, 0};
};

struct StackEntry {
  size_t node{0};
  int32_t origin[3]{0, 0, 0};
};

bool MaskOn(const tvdb_nodemask_t &mask, size_t index) {
  return index < mask.bits.num_bits &&
         (mask.bits.data[index >> 3] & uint8_t(1u << (index & 7u))) != 0;
}

void GatherLeaves(const tvdb_grid_t &grid, std::vector<LeafView> *leaves) {
  if (!leaves || grid.tree.num_nodes == 0) return;
  std::vector<StackEntry> stack(1);
  while (!stack.empty()) {
    const StackEntry entry = stack.back();
    stack.pop_back();
    if (entry.node >= grid.tree.num_nodes) continue;
    const tvdb_tree_node_t &node = grid.tree.nodes[entry.node];
    if (node.type == TVDB_NODE_ROOT) {
      const tvdb_root_node_t &root = node.u.root;
      for (uint32_t c = 0; c < root.num_children; ++c) {
        StackEntry child;
        child.node = root.child_indices[c];
        for (uint32_t a = 0; a < 3; ++a) {
          child.origin[a] = root.child_origins[c * 3 + a];
        }
        stack.push_back(child);
      }
    } else if (node.type == TVDB_NODE_INTERNAL) {
      const tvdb_internal_node_t &internal = node.u.internal;
      const int l = internal.child_mask.log2dim;
      int level = -1;
      for (int i = 0; i < grid.tree.layout.num_levels; ++i) {
        if (grid.tree.layout.levels[i].node_type == TVDB_NODE_INTERNAL &&
            grid.tree.layout.levels[i].log2dim == l) {
          level = i;
          break;
        }
      }
      int child_dim = 1;
      if (level >= 0) {
        int shift = 0;
        for (int i = level + 1; i < grid.tree.layout.num_levels; ++i)
          shift += grid.tree.layout.levels[i].log2dim;
        child_dim = 1 << shift;
      }
      const size_t slots = size_t(1) << (3 * l);
      const int axis_mask = (1 << l) - 1;
      size_t child_index = 0;
      for (size_t slot = 0;
           slot < slots && child_index < internal.num_children; ++slot) {
        if (!MaskOn(internal.child_mask, slot)) continue;
        StackEntry child;
        child.node = internal.child_indices[child_index++];
        child.origin[0] = entry.origin[0] +
            int32_t((slot >> (2 * l)) & size_t(axis_mask)) * child_dim;
        child.origin[1] = entry.origin[1] +
            int32_t((slot >> l) & size_t(axis_mask)) * child_dim;
        child.origin[2] = entry.origin[2] +
            int32_t(slot & size_t(axis_mask)) * child_dim;
        stack.push_back(child);
      }
    } else if (node.type == TVDB_NODE_LEAF) {
      LeafView leaf;
      leaf.leaf = &node.u.leaf;
      std::copy(entry.origin, entry.origin + 3, leaf.origin);
      leaves->push_back(leaf);
    }
  }
}

float HalfToFloat(uint16_t h) {
  const uint32_t sign = uint32_t(h & 0x8000u) << 16;
  uint32_t exponent = (h >> 10) & 0x1fu;
  uint32_t mantissa = h & 0x3ffu;
  uint32_t bits = 0;
  if (exponent == 0) {
    if (mantissa == 0) bits = sign;
    else {
      exponent = 127u - 15u + 1u;
      while ((mantissa & 0x400u) == 0) { mantissa <<= 1; --exponent; }
      bits = sign | (exponent << 23) | ((mantissa & 0x3ffu) << 13);
    }
  } else if (exponent == 31) {
    bits = sign | 0x7f800000u | (mantissa << 13);
  } else {
    bits = sign | ((exponent + (127u - 15u)) << 23) | (mantissa << 13);
  }
  float out = 0.0f;
  std::memcpy(&out, &bits, sizeof(out));
  return out;
}

bool ScalarToFloat(const uint8_t *data, tvdb_value_type_t type, float *out) {
  if (!data || !out) return false;
  switch (type) {
    case TVDB_VALUE_BOOL: *out = *data ? 1.0f : 0.0f; return true;
    case TVDB_VALUE_INT32: { int32_t v; std::memcpy(&v, data, 4); *out = float(v); return true; }
    case TVDB_VALUE_INT64: { int64_t v; std::memcpy(&v, data, 8); *out = float(v); return true; }
    case TVDB_VALUE_FLOAT: std::memcpy(out, data, 4); return true;
    case TVDB_VALUE_DOUBLE: { double v; std::memcpy(&v, data, 8); *out = float(v); return true; }
    case TVDB_VALUE_HALF: { uint16_t v; std::memcpy(&v, data, 2); *out = HalfToFloat(v); return true; }
    case TVDB_VALUE_VEC3I: {
      int32_t v[3]; std::memcpy(v, data, sizeof(v));
      *out = std::sqrt(float(v[0]) * float(v[0]) +
                       float(v[1]) * float(v[1]) +
                       float(v[2]) * float(v[2])); return true;
    }
    case TVDB_VALUE_VEC3F: {
      float v[3]; std::memcpy(v, data, sizeof(v));
      *out = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]); return true;
    }
    case TVDB_VALUE_VEC3D: {
      double v[3]; std::memcpy(v, data, sizeof(v));
      *out = float(std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2])); return true;
    }
    default: return false;
  }
}

float BackgroundFloat(const tvdb_value_t &value) {
  switch (value.type) {
    case TVDB_VALUE_BOOL: return value.u.b ? 1.0f : 0.0f;
    case TVDB_VALUE_INT32: return float(value.u.i32);
    case TVDB_VALUE_INT64: return float(value.u.i64);
    case TVDB_VALUE_FLOAT: return value.u.f;
    case TVDB_VALUE_DOUBLE: return float(value.u.d);
    case TVDB_VALUE_VEC3I: return std::sqrt(
        float(value.u.vec3i[0]) * float(value.u.vec3i[0]) +
        float(value.u.vec3i[1]) * float(value.u.vec3i[1]) +
        float(value.u.vec3i[2]) * float(value.u.vec3i[2]));
    case TVDB_VALUE_VEC3F: return std::sqrt(
        value.u.vec3f[0] * value.u.vec3f[0] +
        value.u.vec3f[1] * value.u.vec3f[1] +
        value.u.vec3f[2] * value.u.vec3f[2]);
    case TVDB_VALUE_VEC3D: return float(std::sqrt(
        value.u.vec3d[0] * value.u.vec3d[0] +
        value.u.vec3d[1] * value.u.vec3d[1] +
        value.u.vec3d[2] * value.u.vec3d[2]));
    default: return 0.0f;
  }
}

const char *ValueTypeName(tvdb_value_type_t type) {
  switch (type) {
    case TVDB_VALUE_BOOL: return "bool";
    case TVDB_VALUE_INT32: return "int32";
    case TVDB_VALUE_INT64: return "int64";
    case TVDB_VALUE_FLOAT: return "float";
    case TVDB_VALUE_DOUBLE: return "double";
    case TVDB_VALUE_HALF: return "half";
    case TVDB_VALUE_VEC3I: return "vec3i-magnitude";
    case TVDB_VALUE_VEC3F: return "vec3f-magnitude";
    case TVDB_VALUE_VEC3D: return "vec3d-magnitude";
    default: return "unsupported";
  }
}

bool ConvertGrid(const tvdb_grid_t &source, size_t max_voxels,
                 VDBGrid *out, std::string *warn) {
  if (!out || source.tree.layout.num_levels < 1) return false;
  if (source.tree.is_point_data_grid || source.tree.is_point_index_grid) {
    if (warn) *warn += "Skipping OpenVDB point grid '" +
        std::string(source.descriptor.grid_name ? source.descriptor.grid_name : "") +
        "'; UsdVol density rendering requires voxel-valued grids.\n";
    return false;
  }
  const tvdb_value_type_t type = source.tree.layout
      .levels[source.tree.layout.num_levels - 1].value_type;
  if (std::string(ValueTypeName(type)) == "unsupported") {
    if (warn) *warn += "Skipping non-scalar VDB grid '" +
        std::string(source.descriptor.grid_name ? source.descriptor.grid_name : "") +
        "' of type '" +
        std::string(source.descriptor.grid_type ? source.descriptor.grid_type : "") + "'.\n";
    return false;
  }
  std::vector<LeafView> leaves;
  GatherLeaves(source, &leaves);
  int32_t lo[3] = {(std::numeric_limits<int32_t>::max)(),
                   (std::numeric_limits<int32_t>::max)(),
                   (std::numeric_limits<int32_t>::max)()};
  int32_t hi[3] = {(std::numeric_limits<int32_t>::min)(),
                   (std::numeric_limits<int32_t>::min)(),
                   (std::numeric_limits<int32_t>::min)()};
  bool any = false;
  for (const LeafView &view : leaves) {
    const int l = view.leaf->value_mask.log2dim;
    const int dim = 1 << l;
    for (size_t i = 0; i < view.leaf->num_voxels; ++i) {
      if (!MaskOn(view.leaf->value_mask, i)) continue;
      const int xyz[3] = {int(i >> (2 * l)), int((i >> l) & size_t(dim - 1)),
                          int(i & size_t(dim - 1))};
      for (int a = 0; a < 3; ++a) {
        const int32_t p = view.origin[a] + xyz[a];
        lo[a] = std::min(lo[a], p); hi[a] = std::max(hi[a], p);
      }
      any = true;
    }
  }
  if (!any) return false;
  size_t count = 1;
  for (int a = 0; a < 3; ++a) {
    const uint64_t dim = uint64_t(int64_t(hi[a]) - int64_t(lo[a]) + 1);
    if (dim > uint64_t((std::numeric_limits<int>::max)()) ||
        count > (std::numeric_limits<size_t>::max)() / size_t(dim)) return false;
    out->origin[a] = lo[a]; out->dim[a] = int(dim); count *= size_t(dim);
  }
  const size_t cap = max_voxels ? max_voxels : size_t(640) * 640 * 640;
  if (count > cap) {
    if (warn) *warn += "Skipping VDB grid '" +
        std::string(source.descriptor.grid_name ? source.descriptor.grid_name : "") +
        "': dense extent exceeds voxel limit.\n";
    return false;
  }
  out->name = source.descriptor.grid_name ? source.descriptor.grid_name : "";
  out->value_type = ValueTypeName(type);
  const tvdb_root_node_t &root = source.tree.nodes[0].u.root;
  out->background = BackgroundFloat(root.background);
  out->data.assign(count, out->background);
  const size_t elem = tvdb_value_type_size(type);
  for (const LeafView &view : leaves) {
    const int l = view.leaf->value_mask.log2dim;
    const int dim = 1 << l;
    for (size_t i = 0; i < view.leaf->num_voxels; ++i) {
      if (!MaskOn(view.leaf->value_mask, i)) continue;
      const int x = view.origin[0] + int(i >> (2 * l)) - lo[0];
      const int y = view.origin[1] + int((i >> l) & size_t(dim - 1)) - lo[1];
      const int z = view.origin[2] + int(i & size_t(dim - 1)) - lo[2];
      float value = 0.0f;
      if (!ScalarToFloat(view.leaf->data + i * elem, type, &value)) continue;
      out->data[size_t(x) + size_t(out->dim[0]) *
          (size_t(y) + size_t(out->dim[1]) * size_t(z))] = value;
    }
  }
  for (int a = 0; a < 3; ++a) {
    out->voxel_size[a] = source.transform.voxel_size[a];
    out->world_translation[a] = source.transform.translation[a];
  }
  return true;
}

}  // namespace

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

  tvdb_file_t file{};
  tvdb_error_t load_error{};
  tvdb_status_t status = tvdb_file_open_memory(&file, data, len, nullptr,
                                                &load_error);
  if (status == TVDB_OK) status = tvdb_read_all_grids(&file, &load_error);
  if (status != TVDB_OK) {
    if (err) *err += "Failed to decode VDB (" + uri + "): " +
        std::string(load_error.message[0] ? load_error.message
                                          : tvdb_status_string(status)) + "\n";
    tvdb_file_close(&file);
    return false;
  }
  for (size_t i = 0; i < file.num_grids; ++i) {
    VDBGrid grid;
    if (ConvertGrid(file.grids[i], max_voxels, &grid, warn))
      grids->push_back(std::move(grid));
  }
  tvdb_file_close(&file);
  return !grids->empty();
}

bool ReadVDBFromFile(const std::string &filepath, std::vector<VDBGrid> *grids,
                     std::string *warn, std::string *err, size_t max_voxels) {
  if (!grids) return false;
  tvdb_file_t file{};
  tvdb_error_t load_error{};
  tvdb_status_t status = tvdb_file_open(&file, filepath.c_str(), nullptr,
                                         &load_error);
  if (status == TVDB_OK) status = tvdb_read_all_grids(&file, &load_error);
  if (status != TVDB_OK) {
    if (err) *err += "Failed to decode VDB (" + filepath + "): " +
        std::string(load_error.message[0] ? load_error.message
                                          : tvdb_status_string(status)) + "\n";
    tvdb_file_close(&file);
    return false;
  }
  for (size_t i = 0; i < file.num_grids; ++i) {
    VDBGrid grid;
    if (ConvertGrid(file.grids[i], max_voxels, &grid, warn))
      grids->push_back(std::move(grid));
  }
  tvdb_file_close(&file);
  return !grids->empty();
}

}  // namespace usdVol
}  // namespace lightusd
