// SPDX-License-Identifier: Apache-2.0
// Copyright 2026-Present Light Transport Entertainment Inc.
//
// tinysubdiv: edge interning and CSR adjacency.

#include "tsd-internal.hh"

namespace tinyusdz {
namespace tsd {

namespace {

// Open-addressing edge intern table: linear probe, power-of-2 capacity,
// load factor <= 0.5. Key is the sorted vertex pair.
struct EdgeTable {
  std::vector<uint64_t> keys;   // (min << 32) | max; kEmpty when unused
  std::vector<uint32_t> values; // edge id
  uint64_t mask = 0;

  static constexpr uint64_t kEmpty = 0xFFFFFFFFFFFFFFFFull;

  bool Init(uint64_t expected_edges) {
    uint64_t cap = 16;
    while (cap < expected_edges * 2) {
      cap <<= 1;
      if (cap > (1ull << 33)) {
        return false;  // would exceed any sane edge count
      }
    }
    keys.assign(size_t(cap), kEmpty);
    values.assign(size_t(cap), kInvalidIndex);
    mask = cap - 1;
    return true;
  }

  // Returns existing id, or assigns `next_id` and returns it.
  uint32_t FindOrInsert(uint32_t a, uint32_t b, uint32_t next_id) {
    const uint64_t key =
        (a < b) ? ((uint64_t(a) << 32) | b) : ((uint64_t(b) << 32) | a);
    // Fibonacci hashing of the 64-bit key.
    uint64_t h = (key * 0x9E3779B97F4A7C15ull) >> 32;
    for (;;) {
      const size_t slot = size_t(h & mask);
      if (keys[slot] == kEmpty) {
        keys[slot] = key;
        values[slot] = next_id;
        return next_id;
      }
      if (keys[slot] == key) {
        return values[slot];
      }
      h++;
    }
  }

  // Returns edge id, or kInvalidIndex if absent.
  uint32_t Find(uint32_t a, uint32_t b) const {
    const uint64_t key =
        (a < b) ? ((uint64_t(a) << 32) | b) : ((uint64_t(b) << 32) | a);
    uint64_t h = (key * 0x9E3779B97F4A7C15ull) >> 32;
    for (;;) {
      const size_t slot = size_t(h & mask);
      if (keys[slot] == kEmpty) {
        return kInvalidIndex;
      }
      if (keys[slot] == key) {
        return values[slot];
      }
      h++;
    }
  }
};

}  // namespace

Result BuildTopology(const uint32_t *face_vertex_counts, uint32_t num_faces,
                     const uint32_t *face_vertex_indices,
                     uint32_t num_face_vertex_indices, uint32_t num_points,
                     Topology *out, std::string *err) {
  out->num_points = num_points;
  out->num_faces = num_faces;

  // Prefix sums.
  out->face_offsets.resize(size_t(num_faces) + 1);
  {
    uint64_t off = 0;
    for (uint32_t f = 0; f < num_faces; f++) {
      out->face_offsets[f] = uint32_t(off);
      off += face_vertex_counts[f];
    }
    if (off != num_face_vertex_indices) {
      return Fail(Result::InvalidTopology, err,
                  "sum(face_vertex_counts) != num_face_vertex_indices.");
    }
    out->face_offsets[num_faces] = num_face_vertex_indices;
  }

  // Intern edges. Upper bound on edge count is the corner count.
  EdgeTable table;
  if (!table.Init(num_face_vertex_indices)) {
    return Fail(Result::LimitExceeded, err, "edge table too large.");
  }

  out->face_edges.assign(num_face_vertex_indices, kInvalidIndex);
  out->edge_verts.clear();
  out->edge_faces.clear();
  // `edge_dir_seen` bit 0: a->b direction seen; bit 1: b->a seen (a < b).
  std::vector<uint8_t> edge_dir_seen;

  uint32_t num_edges = 0;
  for (uint32_t f = 0; f < num_faces; f++) {
    const uint32_t begin = out->face_offsets[f];
    const uint32_t n = face_vertex_counts[f];
    for (uint32_t k = 0; k < n; k++) {
      const uint32_t a = face_vertex_indices[begin + k];
      const uint32_t b = face_vertex_indices[begin + ((k + 1 == n) ? 0 : k + 1)];
      if (a == b) {
        return Fail(Result::InvalidTopology, err,
                    "degenerate edge (repeated vertex) in face.");
      }
      const uint32_t e = table.FindOrInsert(a, b, num_edges);
      if (e == num_edges) {
        num_edges++;
        const uint32_t lo = (a < b) ? a : b;
        const uint32_t hi = (a < b) ? b : a;
        out->edge_verts.push_back(lo);
        out->edge_verts.push_back(hi);
        out->edge_faces.push_back(f);
        out->edge_faces.push_back(kInvalidIndex);
        edge_dir_seen.push_back((a < b) ? 1 : 2);
      } else {
        if (out->edge_faces[2 * e + 1] != kInvalidIndex) {
          return Fail(Result::InvalidTopology, err,
                      "non-manifold edge (more than 2 incident faces).");
        }
        if (out->edge_faces[2 * e] == f) {
          return Fail(Result::InvalidTopology, err,
                      "face references the same edge twice.");
        }
        const uint8_t dir = (a < b) ? 1 : 2;
        if (edge_dir_seen[e] & dir) {
          return Fail(Result::InvalidTopology, err,
                      "inconsistent face winding across shared edge.");
        }
        edge_dir_seen[e] |= dir;
        out->edge_faces[2 * e + 1] = f;
      }
      out->face_edges[begin + k] = e;
    }
  }
  out->num_edges = num_edges;

  // CSR vertex -> incident faces (one entry per face corner).
  out->vert_face_offsets.assign(size_t(num_points) + 1, 0);
  for (uint32_t i = 0; i < num_face_vertex_indices; i++) {
    out->vert_face_offsets[face_vertex_indices[i] + 1]++;
  }
  for (uint32_t v = 0; v < num_points; v++) {
    out->vert_face_offsets[v + 1] += out->vert_face_offsets[v];
  }
  out->vert_faces.assign(num_face_vertex_indices, kInvalidIndex);
  {
    std::vector<uint32_t> cursor(out->vert_face_offsets.begin(),
                                 out->vert_face_offsets.end() - 1);
    for (uint32_t f = 0; f < num_faces; f++) {
      const uint32_t begin = out->face_offsets[f];
      const uint32_t n = face_vertex_counts[f];
      for (uint32_t k = 0; k < n; k++) {
        out->vert_faces[cursor[face_vertex_indices[begin + k]]++] = f;
      }
    }
  }

  // CSR vertex -> incident edges (each edge contributes to both endpoints).
  out->vert_edge_offsets.assign(size_t(num_points) + 1, 0);
  for (uint32_t e = 0; e < num_edges; e++) {
    out->vert_edge_offsets[out->edge_verts[2 * e] + 1]++;
    out->vert_edge_offsets[out->edge_verts[2 * e + 1] + 1]++;
  }
  for (uint32_t v = 0; v < num_points; v++) {
    out->vert_edge_offsets[v + 1] += out->vert_edge_offsets[v];
  }
  out->vert_edges.assign(size_t(num_edges) * 2, kInvalidIndex);
  {
    std::vector<uint32_t> cursor(out->vert_edge_offsets.begin(),
                                 out->vert_edge_offsets.end() - 1);
    for (uint32_t e = 0; e < num_edges; e++) {
      out->vert_edges[cursor[out->edge_verts[2 * e]]++] = e;
      out->vert_edges[cursor[out->edge_verts[2 * e + 1]]++] = e;
    }
  }

  // Boundary vertices: endpoint of any boundary edge.
  out->vert_is_boundary.assign(num_points, 0);
  for (uint32_t e = 0; e < num_edges; e++) {
    if (out->edge_faces[2 * e + 1] == kInvalidIndex) {
      out->vert_is_boundary[out->edge_verts[2 * e]] = 1;
      out->vert_is_boundary[out->edge_verts[2 * e + 1]] = 1;
    }
  }

  return Result::Success;
}

// Internal helper used by the refine driver to map canonical crease vertex
// pairs onto edge ids of the level-0 topology. Pairs that are not actual
// mesh edges are skipped (OpenSubdiv warns and ignores them too).
void MapCreasesToEdges(const Topology &topo, const CreaseEdges &creases,
                       std::vector<float> *edge_sharpness) {
  if (creases.sharpnesses.empty()) {
    return;
  }
  // Rebuild a lookup over interned edges via vert_edges CSR (avoids keeping
  // the intern table alive): scan incident edges of the lower vertex.
  for (size_t i = 0; i < creases.sharpnesses.size(); i++) {
    const uint32_t a = creases.edge_verts[2 * i];
    const uint32_t b = creases.edge_verts[2 * i + 1];
    if (a >= topo.num_points || b >= topo.num_points) {
      continue;
    }
    const uint32_t begin = topo.vert_edge_offsets[a];
    const uint32_t end = topo.vert_edge_offsets[a + 1];
    for (uint32_t k = begin; k < end; k++) {
      const uint32_t e = topo.vert_edges[k];
      if ((topo.edge_verts[2 * e] == a && topo.edge_verts[2 * e + 1] == b) ||
          (topo.edge_verts[2 * e] == b && topo.edge_verts[2 * e + 1] == a)) {
        if ((*edge_sharpness)[e] < creases.sharpnesses[i]) {
          (*edge_sharpness)[e] = creases.sharpnesses[i];
        }
        break;
      }
    }
  }
}

}  // namespace tsd
}  // namespace tinyusdz
