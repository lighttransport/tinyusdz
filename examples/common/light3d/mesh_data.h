#pragma once

// NOTE: Trimmed for tusdview. Original light3d mesh_data.h pulled in the full
// Xform/Prim scene-graph via "xform.h" and defined a MeshPrim class. tusdview
// only needs the plain MeshGeometry POD (used by material.cpp helpers), so the
// MeshPrim class and the xform.h dependency were removed.
#include "light3d.h"  // Vec3 / Vec4
#include <vector>
#include <cstdint>

namespace light3d {

struct MeshGeometry {
    std::vector<Vec3> points;
    std::vector<Vec3> normals;
    std::vector<Vec3> uvs;           // stored as Vec3 for flexibility (u, v, 0)
    std::vector<int> faceVertexCounts;   // e.g. {3, 3, 4} = tri, tri, quad
    std::vector<int> faceVertexIndices;  // indices into points
    std::vector<int> faceMaterialIds;    // one per face, index into MaterialLibrary

    // Skinning data (per-vertex)
    std::vector<Vec4> jointIndices;  // up to 4 joint influences per vertex (as float)
    std::vector<Vec4> jointWeights;  // corresponding weights

    size_t vertexCount() const { return points.size(); }
    size_t faceCount() const { return faceVertexCounts.size(); }
    bool hasPerFaceMaterials() const { return !faceMaterialIds.empty(); }
    bool hasSkinning() const { return !jointWeights.empty(); }
};

} // namespace light3d
