#include "usdrender.hh"
#include "render-data.hh"
#include "../external/nanort.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace tinyusdz {
namespace tydra {

// Simple camera ray generation
struct Camera {
  float eye[3];
  float up[3]; 
  float lookat[3];
  float fov;
  float aspect;
  
  void generateRay(float u, float v, nanort::Ray<float> &ray) const {
    // Compute camera coordinate system
    float forward[3] = {lookat[0] - eye[0], lookat[1] - eye[1], lookat[2] - eye[2]};
    float flen = std::sqrt(forward[0]*forward[0] + forward[1]*forward[1] + forward[2]*forward[2]);
    forward[0] /= flen; forward[1] /= flen; forward[2] /= flen;
    
    // Right = forward x up
    float right[3] = {
      forward[1]*up[2] - forward[2]*up[1],
      forward[2]*up[0] - forward[0]*up[2], 
      forward[0]*up[1] - forward[1]*up[0]
    };
    float rlen = std::sqrt(right[0]*right[0] + right[1]*right[1] + right[2]*right[2]);
    right[0] /= rlen; right[1] /= rlen; right[2] /= rlen;
    
    // Up = right x forward  
    float up_corrected[3] = {
      right[1]*forward[2] - right[2]*forward[1],
      right[2]*forward[0] - right[0]*forward[2],
      right[0]*forward[1] - right[1]*forward[0]
    };
    
    // Convert screen coordinates to camera ray
    float scale = std::tan(fov * 0.5f);
    float dx = (2.0f * u - 1.0f) * aspect * scale;
    float dy = (1.0f - 2.0f * v) * scale;
    
    ray.org[0] = eye[0];
    ray.org[1] = eye[1]; 
    ray.org[2] = eye[2];
    
    ray.dir[0] = forward[0] + dx * right[0] + dy * up_corrected[0];
    ray.dir[1] = forward[1] + dx * right[1] + dy * up_corrected[1];
    ray.dir[2] = forward[2] + dx * right[2] + dy * up_corrected[2];
    
    // Normalize direction
    float dlen = std::sqrt(ray.dir[0]*ray.dir[0] + ray.dir[1]*ray.dir[1] + ray.dir[2]*ray.dir[2]);
    ray.dir[0] /= dlen; ray.dir[1] /= dlen; ray.dir[2] /= dlen;
    
    ray.min_t = 0.001f;
    ray.max_t = 1e30f;
  }
};

// Simple mesh container for NanoRT
struct MeshData {
  std::vector<float> vertices; // flattened: x,y,z,x,y,z,...
  std::vector<unsigned int> faces; // flattened: i0,i1,i2,i0,i1,i2,...
  size_t num_faces;
};

// Convert RenderMesh to NanoRT-compatible format
static bool convertMeshData(const RenderMesh &renderMesh, MeshData &meshData) {
  if (renderMesh.points.empty()) return false;
  
  // Convert points to flattened vertex array
  meshData.vertices.reserve(renderMesh.points.size() * 3);
  for (const auto &pt : renderMesh.points) {
    meshData.vertices.push_back(pt[0]);
    meshData.vertices.push_back(pt[1]);
    meshData.vertices.push_back(pt[2]);
  }
  
  // Convert face indices
  const auto& faceIndices = renderMesh.faceVertexIndices();
  const auto& faceCounts = renderMesh.faceVertexCounts();
  
  if (faceIndices.empty() || faceCounts.empty()) return false;
  
  // Triangulate if needed
  size_t face_offset = 0;
  for (uint32_t count : faceCounts) {
    if (count == 3) {
      // Already a triangle
      meshData.faces.push_back(faceIndices[face_offset]);
      meshData.faces.push_back(faceIndices[face_offset + 1]);
      meshData.faces.push_back(faceIndices[face_offset + 2]);
    } else if (count == 4) {
      // Quad -> 2 triangles
      uint32_t i0 = faceIndices[face_offset];
      uint32_t i1 = faceIndices[face_offset + 1];
      uint32_t i2 = faceIndices[face_offset + 2];
      uint32_t i3 = faceIndices[face_offset + 3];
      
      meshData.faces.push_back(i0);
      meshData.faces.push_back(i1);
      meshData.faces.push_back(i2);
      
      meshData.faces.push_back(i0);
      meshData.faces.push_back(i2);
      meshData.faces.push_back(i3);
    } else {
      // Fan triangulation for n-gons
      uint32_t i0 = faceIndices[face_offset];
      for (uint32_t i = 1; i < count - 1; i++) {
        meshData.faces.push_back(i0);
        meshData.faces.push_back(faceIndices[face_offset + i]);
        meshData.faces.push_back(faceIndices[face_offset + i + 1]);
      }
    }
    face_offset += count;
  }
  
  meshData.num_faces = meshData.faces.size() / 3;
  return true;
}

bool Render(const RenderScene &scene, const RenderOption &option, RenderBuffer &result) {
  if (scene.meshes.empty()) {
    return false;
  }
  
  // Combine all meshes into single mesh data for simplicity
  MeshData meshData;
  for (const auto &renderMesh : scene.meshes) {
    MeshData tempMesh;
    if (!convertMeshData(renderMesh, tempMesh)) continue;
    
    // Offset indices for combined mesh
    uint32_t vertex_offset = static_cast<uint32_t>(meshData.vertices.size() / 3);
    for (auto idx : tempMesh.faces) {
      meshData.faces.push_back(idx + vertex_offset);
    }
    
    // Append vertices
    meshData.vertices.insert(meshData.vertices.end(), 
                           tempMesh.vertices.begin(), tempMesh.vertices.end());
  }
  
  if (meshData.vertices.empty() || meshData.faces.empty()) {
    return false;
  }
  
  meshData.num_faces = meshData.faces.size() / 3;
  
  // Build BVH
  nanort::BVHBuildOptions<float> build_options;
  nanort::TriangleMesh<float> triangle_mesh(meshData.vertices.data(), meshData.faces.data(), sizeof(float) * 3);
  nanort::TriangleSAHPred<float> triangle_pred(meshData.vertices.data(), meshData.faces.data(), sizeof(float) * 3);
  
  nanort::BVHAccel<float> accel;
  bool ret = accel.Build(static_cast<unsigned int>(meshData.num_faces), triangle_mesh, triangle_pred, build_options);
  if (!ret) {
    return false;
  }
  
  // Setup camera
  Camera camera;
  for (int i = 0; i < 3; i++) {
    camera.eye[i] = option.eye[i];
    camera.up[i] = option.up[i];
    camera.lookat[i] = option.lookat[i];
  }
  camera.fov = 45.0f * static_cast<float>(M_PI) / 180.0f; // 45 degrees in radians
  camera.aspect = static_cast<float>(option.width) / static_cast<float>(option.height);
  
  // Allocate output buffers
  result.rgba.resize(static_cast<size_t>(option.width * option.height * 4));
  result.depth.resize(static_cast<size_t>(option.width * option.height));
  std::fill(result.rgba.begin(), result.rgba.end(), 0.0f);
  std::fill(result.depth.begin(), result.depth.end(), 1e30f);
  
  // Raytracing loop
  nanort::TriangleIntersector<> triangle_intersector(meshData.vertices.data(), meshData.faces.data(), sizeof(float) * 3);
  
  for (int y = 0; y < option.height; y++) {
    for (int x = 0; x < option.width; x++) {
      int pixel_idx = y * option.width + x;
      
      // Sample multiple times per pixel for antialiasing
      float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
      float closest_depth = 1e30f;
      
      for (int s = 0; s < option.spp; s++) {
        float u = (static_cast<float>(x) + (s > 0 ? static_cast<float>(rand()) / static_cast<float>(RAND_MAX) : 0.5f)) / static_cast<float>(option.width);
        float v = (static_cast<float>(y) + (s > 0 ? static_cast<float>(rand()) / static_cast<float>(RAND_MAX) : 0.5f)) / static_cast<float>(option.height);
        
        nanort::Ray<float> ray;
        camera.generateRay(u, v, ray);
        
        nanort::TriangleIntersection<> isect;
        bool hit = accel.Traverse(ray, triangle_intersector, &isect);
        
        if (hit) {
          // Simple shading based on surface normal
          float normal[3];
          // Compute face normal from triangle vertices
          uint32_t i0 = meshData.faces[isect.prim_id * 3 + 0];
          uint32_t i1 = meshData.faces[isect.prim_id * 3 + 1]; 
          uint32_t i2 = meshData.faces[isect.prim_id * 3 + 2];
          
          float v0[3] = {meshData.vertices[i0*3], meshData.vertices[i0*3+1], meshData.vertices[i0*3+2]};
          float v1[3] = {meshData.vertices[i1*3], meshData.vertices[i1*3+1], meshData.vertices[i1*3+2]};
          float v2[3] = {meshData.vertices[i2*3], meshData.vertices[i2*3+1], meshData.vertices[i2*3+2]};
          
          float edge1[3] = {v1[0]-v0[0], v1[1]-v0[1], v1[2]-v0[2]};
          float edge2[3] = {v2[0]-v0[0], v2[1]-v0[1], v2[2]-v0[2]};
          
          // Cross product for normal
          normal[0] = edge1[1]*edge2[2] - edge1[2]*edge2[1];
          normal[1] = edge1[2]*edge2[0] - edge1[0]*edge2[2];
          normal[2] = edge1[0]*edge2[1] - edge1[1]*edge2[0];
          
          // Normalize
          float nlen = std::sqrt(normal[0]*normal[0] + normal[1]*normal[1] + normal[2]*normal[2]);
          if (nlen > 0.0f) {
            normal[0] /= nlen; normal[1] /= nlen; normal[2] /= nlen;
          }
          
          // Simple Lambert shading with light from camera direction
          float light_dir[3] = {-ray.dir[0], -ray.dir[1], -ray.dir[2]};
          float ndotl = std::max(0.0f, normal[0]*light_dir[0] + normal[1]*light_dir[1] + normal[2]*light_dir[2]);
          
          float shaded_color = 0.3f + 0.7f * ndotl; // ambient + diffuse
          
          color[0] += shaded_color;
          color[1] += shaded_color; 
          color[2] += shaded_color;
          color[3] += 1.0f;
          
          closest_depth = std::min(closest_depth, isect.t);
        }
      }
      
      // Average samples
      if (option.spp > 0) {
        float inv_spp = 1.0f / static_cast<float>(option.spp);
        color[0] *= inv_spp;
        color[1] *= inv_spp;
        color[2] *= inv_spp;
        color[3] *= inv_spp;
      }
      
      result.rgba[static_cast<size_t>(pixel_idx * 4 + 0)] = color[0];
      result.rgba[static_cast<size_t>(pixel_idx * 4 + 1)] = color[1];
      result.rgba[static_cast<size_t>(pixel_idx * 4 + 2)] = color[2];
      result.rgba[static_cast<size_t>(pixel_idx * 4 + 3)] = color[3];
      result.depth[static_cast<size_t>(pixel_idx)] = closest_depth;
    }
  }
  
  return true;
}

} // namespace tydra
} // namespace tinyusdz
