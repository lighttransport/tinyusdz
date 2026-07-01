// SPDX-License-Identifier: Apache-2.0
// tusdrender — shared LightRT GPU-backend helpers (see tusdr_gpu_common.hh).
#include "tusdr_gpu_common.hh"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

#include "image-writer.hh"

namespace tusdr {

bool BuildGpuTriScene(const std::vector<Vec3> &base_colors,
                      const std::vector<RTPreviewStats::MeshGeometry> &geos,
                      GpuTriScene *out) {
  // Flatten all meshes into one indexed vertex/index array LightRT can build:
  // flat_verts = unique positions, flat_idx = 3*ntris vertex ids.
  uint32_t base_idx = 0;
  for (const auto &g : geos) {
    uint32_t nv = uint32_t(g.positions.size() / 3);
    for (uint32_t j = 0; j < nv; ++j) {
      out->flat_verts.push_back(g.positions[j * 3 + 0]);
      out->flat_verts.push_back(g.positions[j * 3 + 1]);
      out->flat_verts.push_back(g.positions[j * 3 + 2]);
    }
    for (uint32_t j = 0; j < uint32_t(g.indices.size()); ++j) {
      out->flat_idx.push_back(g.indices[j] + base_idx);
    }
    // Only trust per-vertex normals when they are exactly one-per-position
    // (USD faceVarying normals have a different layout, so fall back to flat).
    const bool perVertexN = (g.normals.size() == size_t(nv) * 3u);
    auto vnorm = [&](uint32_t vi) -> Vec3 {
      return perVertexN ? Vec3{g.normals[vi * 3 + 0], g.normals[vi * 3 + 1],
                               g.normals[vi * 3 + 2]}
                        : Vec3{0, 0, 0};
    };
    // Per-triangle shading data (flat normal + the three vertex normals).
    for (uint32_t j = 0; j < uint32_t(g.indices.size()) / 3; ++j) {
      uint32_t i0 = g.indices[j * 3 + 0];
      uint32_t i1 = g.indices[j * 3 + 1];
      uint32_t i2 = g.indices[j * 3 + 2];
      Vec3 p0{g.positions[i0 * 3 + 0], g.positions[i0 * 3 + 1], g.positions[i0 * 3 + 2]};
      Vec3 p1{g.positions[i1 * 3 + 0], g.positions[i1 * 3 + 1], g.positions[i1 * 3 + 2]};
      Vec3 p2{g.positions[i2 * 3 + 0], g.positions[i2 * 3 + 1], g.positions[i2 * 3 + 2]};
      Vec3 e1 = Sub(p1, p0), e2 = Sub(p2, p0);
      out->normals.push_back(Normalize(Cross(e1, e2)));
      out->vn0.push_back(vnorm(i0));
      out->vn1.push_back(vnorm(i1));
      out->vn2.push_back(vnorm(i2));
    }
    size_t nm = (base_colors.size() > geos.size()) ? geos.size() : base_colors.size();
    Vec3 bc = (&g - &geos[0]) < nm ? base_colors[&g - &geos[0]] : Vec3{0.5f, 0.5f, 0.5f};
    for (uint32_t j = 0; j < uint32_t(g.indices.size()) / 3; ++j) {
      out->base_colors.push_back(bc);
    }
    base_idx += nv;
  }

  out->ntris = uint32_t(out->flat_idx.size() / 3);
  if (out->ntris == 0) {
    std::cerr << "No triangles to render.\n";
    return false;
  }

  lrt_tri_build_options bopts;
  std::memset(&bopts, 0, sizeof(bopts));
  bopts.quality = LRT_TRI_BUILD_DEFAULT;
  bopts.layout = LRT_TRI_LAYOUT_BVH4;
  bopts.num_threads = 1;

  lrt_result lrterr = LRT_RESULT_OK;
  out->scene = lrt_tri_scene_build_indexed(out->flat_verts.data(),
                                           out->flat_verts.size() / 3,
                                           out->flat_idx.data(), out->ntris,
                                           &bopts, &lrterr);
  if (!out->scene || lrterr != LRT_RESULT_OK) {
    std::cerr << "Failed to build LightRT scene.\n";
    return false;
  }
  return true;
}

bool ShadeAndWriteImageInstanced(const Options &opt, const GpuInstancedScene &s,
                                 const std::vector<lrt_ray> &rays,
                                 const std::vector<InstSampleHit> &hits, int w,
                                 int h, int spp) {
  const float ambient = opt.ambient;
  const Vec3 light = Normalize(Vec3{0.5f, 0.8f, 0.6f});

  tinyusdz::Image img;
  img.width = w;
  img.height = h;
  img.channels = 4;
  img.bpp = 8;
  img.data.resize(size_t(w) * size_t(h) * 4, 0);

  // Apply a row-major 3x3 (normal matrix) to an object-space normal.
  auto xform_n = [](const float m[9], const Vec3 &n) -> Vec3 {
    return Vec3{m[0] * n.x + m[1] * n.y + m[2] * n.z,
                m[3] * n.x + m[4] * n.y + m[5] * n.z,
                m[6] * n.x + m[7] * n.y + m[8] * n.z};
  };

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      size_t base = (size_t(y) * size_t(w) + size_t(x)) * size_t(spp);
      Vec3 color{0, 0, 0};
      for (int sp = 0; sp < spp; ++sp) {
        const InstSampleHit &hit = hits[base + size_t(sp)];
        if (!hit.valid) continue;
        const uint32_t inst = hit.inst;
        const uint32_t local = hit.local;
        if (inst >= s.insts.size()) continue;
        const GpuInstPlacement &pl = s.insts[inst];
        if (pl.proto >= s.protos.size()) continue;
        const GpuInstProto &pr = s.protos[pl.proto];
        if (local >= pr.ntris) continue;

        Vec3 bc = pr.base_color;
        // Smooth normal in prototype space, then transform to world.
        Vec3 Nobj = pr.normals[local];
        const float w0 = 1.0f - hit.u - hit.v, w1 = hit.u, w2 = hit.v;
        Vec3 sn = Add(Add(Mul(pr.vn0[local], w0), Mul(pr.vn1[local], w1)),
                      Mul(pr.vn2[local], w2));
        if (Length(sn) > 1.0e-8f) Nobj = sn;
        Vec3 N = xform_n(pl.n2w, Nobj);
        if (Length(N) > 1.0e-8f) N = Normalize(N);
        Vec3 V = Vec3{-rays[base + size_t(sp)].dir[0],
                      -rays[base + size_t(sp)].dir[1],
                      -rays[base + size_t(sp)].dir[2]};
        if (Dot(N, V) < 0.0f) N = Mul(N, -1.0f);
        float key = std::max(0.0f, Dot(N, light));
        float head = std::max(0.0f, Dot(N, V));
        float lit = ambient + 0.8f * key + 0.35f * head;
        color = Add(color, Mul(bc, lit));
      }
      color = Mul(color, 1.0f / float(spp));
      size_t pi = (size_t(y) * size_t(w) + size_t(x)) * 4;
      img.data[pi + 0] = uint8_t(std::min(255.0f, color.x * 255.0f));
      img.data[pi + 1] = uint8_t(std::min(255.0f, color.y * 255.0f));
      img.data[pi + 2] = uint8_t(std::min(255.0f, color.z * 255.0f));
      img.data[pi + 3] = 255;
    }
  }

  tinyusdz::image::WriteOption wopt;
  wopt.format = tinyusdz::image::WriteImageFormat::Autodetect;
  auto ret = tinyusdz::image::WriteImageToFile(opt.output, img, wopt);
  if (!ret) {
    std::cerr << "Failed to write image: " << ret.error() << "\n";
    return false;
  }
  return true;
}

void GenerateCameraRays(const CameraFrame &camera, int w, int h, int spp,
                        std::vector<lrt_ray> *rays) {
  Vec3 eye = camera.origin;
  Vec3 fwd = camera.forward;
  Vec3 up = camera.up;
  Vec3 right = Normalize(Cross(fwd, up));
  Vec3 camUp = Cross(right, fwd);
  float aspect = float(w) / float(h);
  float half_h = std::tan(camera.yfov * 0.5f);
  float half_w = half_h * aspect;

  const size_t nrays = size_t(w) * size_t(h) * size_t(spp);
  rays->resize(nrays);
  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      size_t base = (size_t(y) * size_t(w) + size_t(x)) * size_t(spp);
      for (int s = 0; s < spp; ++s) {
        float jx, jy;
        PixelJitter(s, spp, &jx, &jy);
        float fx = (float(x) + jx) / float(w) * 2.0f - 1.0f;
        float fy = (float(y) + jy) / float(h) * 2.0f - 1.0f;
        Vec3 dir = Add(Mul(right, fx * half_w), Add(Mul(camUp, fy * half_h), fwd));
        dir = Normalize(dir);
        lrt_ray &ray = (*rays)[base + size_t(s)];
        ray.org[0] = eye.x; ray.org[1] = eye.y; ray.org[2] = eye.z;
        ray.tmin = 0.001f;
        ray.dir[0] = dir.x; ray.dir[1] = dir.y; ray.dir[2] = dir.z;
        ray.tmax = 1.0e10f;
      }
    }
  }
}

bool ShadeAndWriteImage(const Options &opt, const GpuTriScene &s,
                        const std::vector<lrt_ray> &rays,
                        const std::vector<lrt_hit> &hits, int w, int h, int spp) {
  const float ambient = opt.ambient;
  const Vec3 light = Normalize(Vec3{0.5f, 0.8f, 0.6f});

  tinyusdz::Image img;
  img.width = w;
  img.height = h;
  img.channels = 4;
  img.bpp = 8;
  img.data.resize(size_t(w) * size_t(h) * 4, 0);

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      size_t base = (size_t(y) * size_t(w) + size_t(x)) * size_t(spp);
      Vec3 color{0, 0, 0};
      for (int sp = 0; sp < spp; ++sp) {
        const lrt_hit &hit = hits[base + size_t(sp)];
        if (hit.prim_id != 0xFFFFFFFFu && hit.prim_id < s.ntris) {
          Vec3 bc = hit.prim_id < s.base_colors.size()
                        ? s.base_colors[hit.prim_id]
                        : Vec3{0.5f, 0.5f, 0.5f};
          // Smooth normal: barycentric-interpolate the triangle's vertex normals
          // (hit.u, hit.v are the v1/v2 weights). Fall back to the flat face
          // normal when vertex normals are absent/degenerate.
          Vec3 N = s.normals[hit.prim_id];
          const float w0 = 1.0f - hit.u - hit.v, w1 = hit.u, w2 = hit.v;
          Vec3 sn = Add(Add(Mul(s.vn0[hit.prim_id], w0), Mul(s.vn1[hit.prim_id], w1)),
                        Mul(s.vn2[hit.prim_id], w2));
          if (Length(sn) > 1.0e-8f) N = Normalize(sn);
          // Orient the normal toward the camera so a surface visible to the eye
          // is never left unlit by a back-facing normal (USD winding/normals are
          // not guaranteed consistent for a quick preview).
          Vec3 V = Vec3{-rays[base + size_t(sp)].dir[0],
                        -rays[base + size_t(sp)].dir[1],
                        -rays[base + size_t(sp)].dir[2]};
          if (Dot(N, V) < 0.0f) N = Mul(N, -1.0f);
          // Fixed key light + a dim camera headlight so the silhouette reads even
          // when the key grazes the surface; ambient lifts the shadow terminator.
          float key = std::max(0.0f, Dot(N, light));
          float head = std::max(0.0f, Dot(N, V));
          float lit = ambient + 0.8f * key + 0.35f * head;
          color = Add(color, Mul(bc, lit));
        }
      }
      color = Mul(color, 1.0f / float(spp));
      size_t pi = (size_t(y) * size_t(w) + size_t(x)) * 4;
      img.data[pi + 0] = uint8_t(std::min(255.0f, color.x * 255.0f));
      img.data[pi + 1] = uint8_t(std::min(255.0f, color.y * 255.0f));
      img.data[pi + 2] = uint8_t(std::min(255.0f, color.z * 255.0f));
      img.data[pi + 3] = 255;
    }
  }

  tinyusdz::image::WriteOption wopt;
  wopt.format = tinyusdz::image::WriteImageFormat::Autodetect;
  auto ret = tinyusdz::image::WriteImageToFile(opt.output, img, wopt);
  if (!ret) {
    std::cerr << "Failed to write image: " << ret.error() << "\n";
    return false;
  }
  return true;
}

}  // namespace tusdr
