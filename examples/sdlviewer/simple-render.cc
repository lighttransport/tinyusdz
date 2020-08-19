#include "simple-render.hh"

#include <atomic>
#include <thread>

#include "nanort.h"
#include "nanosg.h"

// common
#include "matrix.h"
#include "trackball.h"

const float kPI = 3.141592f;

typedef nanort::real3<float> float3;

namespace example {

inline float3 Lerp3(float3 v0, float3 v1, float3 v2, float u, float v) {
  return (1.0f - u - v) * v0 + u * v1 + v * v2;
}

inline void CalcNormal(float3& N, float3 v0, float3 v1, float3 v2) {
  float3 v10 = v1 - v0;
  float3 v20 = v2 - v0;

  N = vcross(v10, v20);
  N = vnormalize(N);
}

// Trianglate mesh
bool ConvertToRenderMesh(const tinyusdz::GeomMesh& mesh, DrawGeomMesh* dst) {
  // vertex points
  // should be vec3f
  dst->vertices = mesh.points.buffer.GetAsVec3fArray();
  if (dst->vertices.size() != (mesh.GetNumPoints() * 3)) {
    return false;
  }

  std::vector<float> facevarying_normals;
  if (!mesh.GetFacevaryingNormals(&facevarying_normals)) {
    return false;
  }

  // Triangulate mesh

  // Make facevarying indices
  // TODO(LTE): Make facevarying normal, uvs, ...
  {
    size_t face_offset = 0;
    for (size_t fid = 0; fid < mesh.faceVertexCounts.size(); fid++) {
      int f_count = mesh.faceVertexCounts[fid];

      assert(f_count >= 3);

      if (f_count == 3) {
        for (size_t f = 0; f < f_count; f++) {
          dst->facevarying_indices.push_back(
              mesh.faceVertexIndices[face_offset + f]);
        }
      } else {
        // Simple triangulation with triangle-fan decomposition
        for (size_t f = 0; f < f_count - 2; f++) {
          size_t f0 = 0;
          size_t f1 = f;
          size_t f2 = f + 1;

          dst->facevarying_indices.push_back(
              mesh.faceVertexIndices[face_offset + f0]);
          dst->facevarying_indices.push_back(
              mesh.faceVertexIndices[face_offset + f1]);
          dst->facevarying_indices.push_back(
              mesh.faceVertexIndices[face_offset + f2]);
        }
      }

      face_offset += f_count;
    }
  }
}

void BuildCameraFrame(float3* origin, float3* corner, float3* u, float3* v,
                      const float quat[4], float eye[3], float lookat[3],
                      float up[3], float fov, int width, int height) {
  float e[4][4];

  Matrix::LookAt(e, eye, lookat, up);

  float r[4][4];
  build_rotmatrix(r, quat);

  float3 lo;
  lo[0] = lookat[0] - eye[0];
  lo[1] = lookat[1] - eye[1];
  lo[2] = lookat[2] - eye[2];
  float dist = vlength(lo);

  float dir[3];
  dir[0] = 0.0;
  dir[1] = 0.0;
  dir[2] = dist;

  Matrix::Inverse(r);

  float rr[4][4];
  float re[4][4];
  float zero[3] = {0.0f, 0.0f, 0.0f};
  float localUp[3] = {0.0f, 1.0f, 0.0f};
  Matrix::LookAt(re, dir, zero, localUp);

  // translate
  re[3][0] += eye[0];  // 0.0; //lo[0];
  re[3][1] += eye[1];  // 0.0; //lo[1];
  re[3][2] += (eye[2] - dist);

  // rot -> trans
  Matrix::Mult(rr, r, re);

  float m[4][4];
  for (int j = 0; j < 4; j++) {
    for (int i = 0; i < 4; i++) {
      m[j][i] = rr[j][i];
    }
  }

  float vzero[3] = {0.0f, 0.0f, 0.0f};
  float eye1[3];
  Matrix::MultV(eye1, m, vzero);

  float lookat1d[3];
  dir[2] = -dir[2];
  Matrix::MultV(lookat1d, m, dir);
  float3 lookat1(lookat1d[0], lookat1d[1], lookat1d[2]);

  float up1d[3];
  Matrix::MultV(up1d, m, up);

  float3 up1(up1d[0], up1d[1], up1d[2]);

  // absolute -> relative
  up1[0] -= eye1[0];
  up1[1] -= eye1[1];
  up1[2] -= eye1[2];
  // printf("up1(after) = %f, %f, %f\n", up1[0], up1[1], up1[2]);

  // Use original up vector
  // up1[0] = up[0];
  // up1[1] = up[1];
  // up1[2] = up[2];

  {
    float flen =
        (0.5f * (float)height / tanf(0.5f * (float)(fov * kPI / 180.0f)));
    float3 look1;
    look1[0] = lookat1[0] - eye1[0];
    look1[1] = lookat1[1] - eye1[1];
    look1[2] = lookat1[2] - eye1[2];
    // vcross(u, up1, look1);
    // flip
    (*u) = nanort::vcross(look1, up1);
    (*u) = vnormalize((*u));

    (*v) = vcross(look1, (*u));
    (*v) = vnormalize((*v));

    look1 = vnormalize(look1);
    look1[0] = flen * look1[0] + eye1[0];
    look1[1] = flen * look1[1] + eye1[1];
    look1[2] = flen * look1[2] + eye1[2];
    (*corner)[0] = look1[0] - 0.5f * (width * (*u)[0] + height * (*v)[0]);
    (*corner)[1] = look1[1] - 0.5f * (width * (*u)[1] + height * (*v)[1]);
    (*corner)[2] = look1[2] - 0.5f * (width * (*u)[2] + height * (*v)[2]);

    (*origin)[0] = eye1[0];
    (*origin)[1] = eye1[1];
    (*origin)[2] = eye1[2];
  }
}

bool Render(const RenderScene& scene, const Camera& cam, AOV* output) {
  int width = output->width;
  int height = output->height;

  float eye[3] = {cam.eye[0], cam.eye[1], cam.eye[2]};
  float look_at[3] = {cam.look_at[0], cam.look_at[1], cam.look_at[2]};
  float up[3] = {cam.up[0], cam.up[1], cam.up[2]};
  float fov = cam.fov;
  float3 origin, corner, u, v;
  BuildCameraFrame(&origin, &corner, &u, &v, cam.quat, eye, look_at, up, fov,
                   width, height);

  std::vector<std::thread> workers;
  std::atomic<int> i(0);

  uint32_t num_threads = std::max(1U, std::thread::hardware_concurrency());

  // auto startT = std::chrono::system_clock::now();

  for (auto t = 0; t < num_threads; t++) {
    workers.emplace_back(std::thread([&, t]() {
      int y = 0;
      while ((y = i++) < height) {
        for (int x = 0; x < width; x++) {
          nanort::Ray<float> ray;
          ray.org[0] = origin[0];
          ray.org[1] = origin[1];
          ray.org[2] = origin[2];

          float3 dir;

          float u0 = 0.5f;
          float u1 = 0.5f;

          dir = corner + (float(x) + u0) * u + (float(height - y - 1) + u1) * v;

          dir = vnormalize(dir);
          ray.dir[0] = dir[0];
          ray.dir[1] = dir[1];
          ray.dir[2] = dir[2];

          size_t pixel_idx = y * width + x;

          // HACK
          output->rgb[3 * pixel_idx + 0] = ray.dir[0];
          output->rgb[3 * pixel_idx + 1] = ray.dir[1];
          output->rgb[3 * pixel_idx + 2] = ray.dir[2];
        }
      }
    }));
  }

  for (auto &th : workers) {
    th.join();
  }

  // auto endT = std::chrono::system_clock::now();

  return true;
}

}  // namespace example
