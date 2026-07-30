// SPDX-License-Identifier: Apache-2.0
#include "loader.hh"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstring>
#include <new>

#include <cstdio>
#include <memory>

#include "image_decode.hh"
#include "next/reader/usdz-reader.hh"
#include "next/tinyusdz-next.hh"
#include "tydra/next/render-converter.hh"
#include "tydra/next/render-data.hh"

namespace tusdql {

namespace tnext = ::tinyusdz::next;
namespace tyn = ::tinyusdz::tydra::next;

const char* LoadPhaseName(LoadPhase phase) {
  switch (phase) {
    case LoadPhase::Idle: return "idle";
    case LoadPhase::Open: return "reading";
    case LoadPhase::Compose: return "composing";
    case LoadPhase::Extract: return "extracting";
    case LoadPhase::Textures: return "textures";
    case LoadPhase::Done: return "done";
    case LoadPhase::Failed: return "failed";
    case LoadPhase::Cancelled: return "cancelled";
  }
  return "?";
}

uint64_t LoadEvent::byte_size() const {
  uint64_t n = sizeof(LoadEvent) + message.size() + error.size();
  n += mesh.byte_size();
  n += materials.size() * sizeof(QlMaterial);
  n += lights.size() * sizeof(QlLight);
  n += cameras.size() * sizeof(QlCameraDesc);
  for (const QlTexture& t : textures) n += t.rgba.size();
  return n;
}

// ---------------------------------------------------------------------------
// LoadStream
// ---------------------------------------------------------------------------

bool LoadStream::Push(LoadEvent&& ev) {
  const uint64_t sz = ev.byte_size();
  std::unique_lock<std::mutex> lk(mu_);
  // Always admit at least one event, however large, or an oversized mesh would
  // deadlock against its own byte bound.
  space_.wait(lk, [&] {
    return cancelled_.load() || q_.empty() || queued_bytes_ + sz <= max_bytes_;
  });
  if (cancelled_.load()) return false;
  queued_bytes_ += sz;
  q_.push_back(std::move(ev));
  lk.unlock();
  ready_.notify_one();
  return true;
}

bool LoadStream::TryPop(LoadEvent* out) {
  std::unique_lock<std::mutex> lk(mu_);
  if (q_.empty()) return false;
  *out = std::move(q_.front());
  q_.pop_front();
  const uint64_t sz = out->byte_size();
  queued_bytes_ = queued_bytes_ > sz ? queued_bytes_ - sz : 0;
  lk.unlock();
  space_.notify_one();
  return true;
}

void LoadStream::Cancel() {
  {
    std::lock_guard<std::mutex> lk(mu_);
    cancelled_.store(true);
  }
  space_.notify_all();
  ready_.notify_all();
}

uint64_t LoadStream::queued_bytes() const {
  std::lock_guard<std::mutex> lk(mu_);
  return queued_bytes_;
}

// ---------------------------------------------------------------------------
// Transform helpers
//
// tydra_next Matrix4 is row-major with USD's row-vector convention
// (world = local * parent_world, translation in the last row), so a point
// transforms as p' = p * M.
// ---------------------------------------------------------------------------

namespace {

void XformPoint(const tyn::Matrix4& m, const float p[3], float out[3]) {
  const float* a = m.m;
  out[0] = p[0] * a[0] + p[1] * a[4] + p[2] * a[8] + a[12];
  out[1] = p[0] * a[1] + p[1] * a[5] + p[2] * a[9] + a[13];
  out[2] = p[0] * a[2] + p[1] * a[6] + p[2] * a[10] + a[14];
}

// Normal matrix = adjugate of the upper 3x3 (i.e. inverse-transpose scaled by
// det). Correct under non-uniform scale and shear; the renderer renormalizes,
// so the det factor is harmless — except for its sign, which we keep so a
// mirroring transform does not invert the lighting.
struct NormalMatrix {
  float n[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
};

NormalMatrix MakeNormalMatrix(const tyn::Matrix4& m) {
  const float* a = m.m;
  // Upper 3x3, row-major.
  const float u[9] = {a[0], a[1], a[2], a[4], a[5], a[6], a[8], a[9], a[10]};
  NormalMatrix out;
  // adj(U)^T == cofactor matrix, which is what transforms normals under the
  // row-vector convention.
  out.n[0] = u[4] * u[8] - u[5] * u[7];
  out.n[1] = u[5] * u[6] - u[3] * u[8];
  out.n[2] = u[3] * u[7] - u[4] * u[6];
  out.n[3] = u[2] * u[7] - u[1] * u[8];
  out.n[4] = u[0] * u[8] - u[2] * u[6];
  out.n[5] = u[1] * u[6] - u[0] * u[7];
  out.n[6] = u[1] * u[5] - u[2] * u[4];
  out.n[7] = u[2] * u[3] - u[0] * u[5];
  out.n[8] = u[0] * u[4] - u[1] * u[3];
  return out;
}

void XformNormal(const NormalMatrix& nm, const float v[3], float out[3]) {
  const float* n = nm.n;
  out[0] = v[0] * n[0] + v[1] * n[3] + v[2] * n[6];
  out[1] = v[0] * n[1] + v[1] * n[4] + v[2] * n[7];
  out[2] = v[0] * n[2] + v[1] * n[5] + v[2] * n[8];
}

// ---------------------------------------------------------------------------
// Texture bytes: filesystem, or an entry inside the USDZ package.
//
// tydra_next's built-in texture loader is metadata-only by design, so decoding
// is ours to do. For a USDZ root the "resolved" path points at an archive entry
// that does not exist on disk, so the package is consulted first.
// ---------------------------------------------------------------------------

bool EndsWithPath(const std::string& haystack, const std::string& needle) {
  if (needle.empty() || needle.size() > haystack.size()) return false;
  if (haystack.compare(haystack.size() - needle.size(), needle.size(),
                       needle) != 0) {
    return false;
  }
  // Match only on a path boundary so "b/x.png" does not match "ab/x.png".
  if (haystack.size() == needle.size()) return true;
  const char prev = haystack[haystack.size() - needle.size() - 1];
  return prev == '/' || prev == '\\';
}

class TextureSource {
 public:
  void Init(const std::string& root_path, size_t max_archive_bytes) {
    root_path_ = root_path;
    const size_t dot = root_path.find_last_of('.');
    std::string ext = dot == std::string::npos ? "" : root_path.substr(dot);
    for (char& c : ext) c = static_cast<char>(::tolower(c));
    if (ext != ".usdz") return;

    ::tinyusdz::next::USDZReadOptions zopts;
    zopts.max_archive_size = max_archive_bytes;
    zip_ = std::make_unique<::tinyusdz::next::USDZReader>();
    if (!zip_->OpenFile(root_path, zopts)) zip_.reset();
  }

  bool Read(const std::string& resolved_path, std::vector<uint8_t>* out) const {
    if (zip_) {
      for (size_t i = 0; i < zip_->NumEntries(); i++) {
        const std::string& name = zip_->EntryName(i);
        if (!EndsWithPath(resolved_path, name) && resolved_path != name) {
          continue;
        }
        const uint8_t* data = zip_->EntryData(i);
        const size_t size = zip_->EntrySize(i);
        if (!data || size == 0) return false;
        out->assign(data, data + size);
        return true;
      }
      // Fall through: some packages reference external files too.
    }

    std::FILE* f = std::fopen(resolved_path.c_str(), "rb");
    if (!f) return false;
    std::fseek(f, 0, SEEK_END);
    const long len = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (len <= 0) {
      std::fclose(f);
      return false;
    }
    out->resize(static_cast<size_t>(len));
    const size_t got = std::fread(out->data(), 1, out->size(), f);
    std::fclose(f);
    return got == out->size();
  }

 private:
  std::string root_path_;
  std::unique_ptr<::tinyusdz::next::USDZReader> zip_;
};

// ---------------------------------------------------------------------------
// Texture downsampling
// ---------------------------------------------------------------------------

uint8_t FloatToU8(float v) {
  const float c = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
  return static_cast<uint8_t>(c * 255.0f + 0.5f);
}

// Expand any supported source image into tightly packed RGBA8 at full size.
bool ExpandToRgba8(const tyn::TextureImage& img, std::vector<uint8_t>* out) {
  const size_t w = img.width;
  const size_t h = img.height;
  const size_t ch = img.channels ? img.channels : 4;
  if (w == 0 || h == 0) return false;

  const size_t texels = w * h;
  out->assign(texels * 4, 255);

  if (img.component_type == tyn::ComponentType::UInt8) {
    if (img.data.size() < texels * ch) return false;
    for (size_t i = 0; i < texels; i++) {
      const size_t src = i * ch;
      uint8_t rgba[4] = {0, 0, 0, 255};
      for (size_t c = 0; c < ch && c < 4; c++) rgba[c] = img.data[src + c];
      if (ch == 1) {  // grayscale
        rgba[1] = rgba[2] = rgba[0];
      }
      std::memcpy(out->data() + i * 4, rgba, 4);
    }
    return true;
  }

  if (img.component_type == tyn::ComponentType::Float32) {
    const size_t floats = texels * ch;
    if (img.data.size() < floats * sizeof(float)) return false;
    // The chunked array is byte-addressed; read floats through a memcpy so we
    // never rely on chunk alignment.
    for (size_t i = 0; i < texels; i++) {
      uint8_t rgba[4] = {0, 0, 0, 255};
      for (size_t c = 0; c < ch && c < 4; c++) {
        const size_t byte_off = (i * ch + c) * sizeof(float);
        uint8_t tmp[sizeof(float)];
        for (size_t b = 0; b < sizeof(float); b++) {
          tmp[b] = img.data[byte_off + b];
        }
        float f;
        std::memcpy(&f, tmp, sizeof(float));
        rgba[c] = FloatToU8(f);
      }
      if (ch == 1) rgba[1] = rgba[2] = rgba[0];
      std::memcpy(out->data() + i * 4, rgba, 4);
    }
    return true;
  }

  // Other component types (16-bit, half, double) are rare in preview content;
  // skipping is better than showing garbage.
  return false;
}

// Box-filter downsample to at most kMaxTextureDim on the long edge.
bool MakePreviewTexture(const tyn::TextureImage& img, QlTexture* out) {
  std::vector<uint8_t> full;
  if (!ExpandToRgba8(img, &full)) return false;

  const uint32_t sw = img.width;
  const uint32_t sh = img.height;
  const uint32_t max_dim = QlTexture::kMaxTextureDim;

  uint32_t dw = sw;
  uint32_t dh = sh;
  if (sw > max_dim || sh > max_dim) {
    const double scale =
        double(max_dim) / double(std::max(sw, sh));
    dw = std::max(1u, static_cast<uint32_t>(sw * scale));
    dh = std::max(1u, static_cast<uint32_t>(sh * scale));
  }

  out->width = dw;
  out->height = dh;
  out->rgba.assign(size_t(dw) * dh * 4, 0);
  out->srgb = (img.color_space == tyn::ColorSpace::sRGB);

  for (uint32_t y = 0; y < dh; y++) {
    const uint32_t y0 = uint32_t(uint64_t(y) * sh / dh);
    const uint32_t y1 = std::max(y0 + 1, uint32_t(uint64_t(y + 1) * sh / dh));
    for (uint32_t x = 0; x < dw; x++) {
      const uint32_t x0 = uint32_t(uint64_t(x) * sw / dw);
      const uint32_t x1 = std::max(x0 + 1, uint32_t(uint64_t(x + 1) * sw / dw));
      uint32_t acc[4] = {0, 0, 0, 0};
      uint32_t n = 0;
      for (uint32_t sy = y0; sy < y1 && sy < sh; sy++) {
        for (uint32_t sx = x0; sx < x1 && sx < sw; sx++) {
          const uint8_t* p = full.data() + (size_t(sy) * sw + sx) * 4;
          acc[0] += p[0];
          acc[1] += p[1];
          acc[2] += p[2];
          acc[3] += p[3];
          n++;
        }
      }
      uint8_t* d = out->rgba.data() + (size_t(y) * dw + x) * 4;
      if (n == 0) n = 1;
      for (int c = 0; c < 4; c++) d[c] = static_cast<uint8_t>(acc[c] / n);
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// Material / light conversion
// ---------------------------------------------------------------------------

QlMaterial ConvertMaterial(const tyn::RenderMaterial& src,
                           const tyn::RenderScene& scene,
                           const std::vector<int>& image_to_texture) {
  QlMaterial dst;
  dst.double_sided = src.double_sided;

  auto resolve_texture = [&](int32_t texture_id) -> int {
    if (texture_id < 0 ||
        texture_id >= static_cast<int32_t>(scene.textures.size())) {
      return -1;
    }
    const int32_t image_id = scene.textures[size_t(texture_id)].image_id;
    if (image_id < 0 ||
        image_id >= static_cast<int32_t>(image_to_texture.size())) {
      return -1;
    }
    return image_to_texture[size_t(image_id)];
  };

  if (src.shader_type == tyn::RenderMaterial::ShaderType::PreviewSurface &&
      src.preview_surface) {
    const tyn::PreviewSurfaceShader& s = *src.preview_surface;
    if (s.diffuse_color.is_texture()) {
      dst.base_color_tex = resolve_texture(s.diffuse_color.texture_id);
      // Keep a plausible tint for the untextured fallback.
      dst.base_color[0] = dst.base_color[1] = dst.base_color[2] = 0.8f;
    } else {
      dst.base_color[0] = s.diffuse_color.value.x;
      dst.base_color[1] = s.diffuse_color.value.y;
      dst.base_color[2] = s.diffuse_color.value.z;
    }
    if (s.emissive_color.is_value()) {
      dst.emissive[0] = s.emissive_color.value.x;
      dst.emissive[1] = s.emissive_color.value.y;
      dst.emissive[2] = s.emissive_color.value.z;
    }
    if (s.roughness.is_value()) dst.roughness = s.roughness.as_float();
    if (s.metallic.is_value()) dst.metallic = s.metallic.as_float();
    if (s.opacity.is_value()) dst.opacity = s.opacity.as_float();
  } else if (src.shader_type == tyn::RenderMaterial::ShaderType::OpenPBR &&
             src.openpbr) {
    const tyn::OpenPBRSurfaceShader& s = *src.openpbr;
    if (s.base_color.is_texture()) {
      dst.base_color_tex = resolve_texture(s.base_color.texture_id);
      dst.base_color[0] = dst.base_color[1] = dst.base_color[2] = 0.8f;
    } else {
      dst.base_color[0] = s.base_color.value.x;
      dst.base_color[1] = s.base_color.value.y;
      dst.base_color[2] = s.base_color.value.z;
    }
    if (s.specular_roughness.is_value()) {
      dst.roughness = s.specular_roughness.as_float();
    }
    if (s.base_metalness.is_value()) {
      dst.metallic = s.base_metalness.as_float();
    }
    if (s.opacity.is_value()) dst.opacity = s.opacity.as_float();
    if (s.emission_color.is_value() && s.emission_luminance.is_value()) {
      const float lum = s.emission_luminance.as_float();
      dst.emissive[0] = s.emission_color.value.x * lum;
      dst.emissive[1] = s.emission_color.value.y * lum;
      dst.emissive[2] = s.emission_color.value.z * lum;
    }
  }

  dst.roughness = std::max(0.02f, std::min(1.0f, dst.roughness));
  dst.metallic = std::max(0.0f, std::min(1.0f, dst.metallic));
  dst.opacity = std::max(0.0f, std::min(1.0f, dst.opacity));
  return dst;
}

bool ConvertLight(const tyn::RenderLight& src, QlLight* out) {
  QlLight l;
  switch (src.type) {
    case tyn::LightType::Directional:
      l.type = QlLight::Type::Distant;
      break;
    case tyn::LightType::Point:
      l.type = QlLight::Type::Point;
      break;
    case tyn::LightType::Sphere:
      l.type = QlLight::Type::Sphere;
      l.radius = src.params.sphere.radius;
      break;
    case tyn::LightType::Rect:
      l.type = QlLight::Type::Rect;
      break;
    case tyn::LightType::Disk:
      l.type = QlLight::Type::Disk;
      l.radius = src.params.disk.radius;
      break;
    case tyn::LightType::Dome:
      l.type = QlLight::Type::Dome;
      break;
    default:
      // Spot/cylinder/geometry lights are approximated as point lights; the
      // preview shader has no cone or area sampling.
      l.type = QlLight::Type::Point;
      break;
  }

  // Row-vector convention: translation is the last row; -Z is the UsdLux
  // light-emission axis.
  const float* m = src.transform.m;
  l.position[0] = m[12];
  l.position[1] = m[13];
  l.position[2] = m[14];
  l.direction[0] = -m[8];
  l.direction[1] = -m[9];
  l.direction[2] = -m[10];
  const float len = std::sqrt(l.direction[0] * l.direction[0] +
                              l.direction[1] * l.direction[1] +
                              l.direction[2] * l.direction[2]);
  if (len > 1e-8f) {
    l.direction[0] /= len;
    l.direction[1] /= len;
    l.direction[2] /= len;
  } else {
    l.direction[0] = 0.0f;
    l.direction[1] = -1.0f;
    l.direction[2] = 0.0f;
  }

  l.color[0] = src.color.x;
  l.color[1] = src.color.y;
  l.color[2] = src.color.z;
  // exposure is a power of two stop on top of intensity (UsdLux).
  l.intensity = src.intensity * std::pow(2.0f, src.exposure);
  l.casts_shadow = src.enable_shadow;

  if (!(l.intensity > 0.0f)) return false;
  *out = l;
  return true;
}

QlCameraDesc ConvertCamera(const tyn::RenderCamera& src) {
  QlCameraDesc d;
  d.name = src.name.empty() ? src.prim_path : src.name;
  std::memcpy(d.world_from_camera, src.transform.m, sizeof(d.world_from_camera));
  d.fov_y_radians = src.fov_y();
  d.near_clip = src.near_clip;
  d.far_clip = src.far_clip;
  return d;
}

}  // namespace

// ---------------------------------------------------------------------------
// QlSceneSink — the streaming tydra_next consumer.
//
// This is where "ok to skip large files" is actually implemented: not as a
// whole-file verdict, but per geometry prim, against the running budget.
// ---------------------------------------------------------------------------

namespace {

class QlSceneSink : public tyn::SceneSink {
 public:
  QlSceneSink(LoadControl* ctrl, LoadStream* stream, const Options& opts,
              const PreviewBudget& budget)
      : ctrl_(ctrl), stream_(stream), opts_(opts), budget_(budget) {}

  bool BeginScene(tyn::RenderScene&& catalog) override {
    catalog_ = std::move(catalog);
    ctrl_->phase.store(LoadPhase::Textures);

    y_up_ = catalog_.up_axis == tyn::RenderScene::UpAxis::Y;

    // Mesh id -> world transform, from the catalog's node hierarchy. Baking the
    // world transform into the points is what lets the renderers stay
    // hierarchy-free.
    for (const tyn::SceneNode& n : catalog_.nodes) {
      if (n.type == tyn::NodeType::Mesh && n.data_id >= 0) {
        if (mesh_world_.size() <= size_t(n.data_id)) {
          mesh_world_.resize(size_t(n.data_id) + 1);
          mesh_visible_.resize(size_t(n.data_id) + 1, true);
        }
        mesh_world_[size_t(n.data_id)] = n.world_transform;
        mesh_visible_[size_t(n.data_id)] = n.visible;
      }
    }

    LoadEvent ev;
    ev.kind = LoadEvent::Kind::Resources;

    // Textures: downsample now and release the full-resolution catalog images
    // immediately, so peak holds one decoded image rather than all of them.
    std::vector<int> image_to_texture(catalog_.images.size(), -1);
    uint64_t texture_bytes = 0;
    for (size_t i = 0; i < catalog_.images.size(); i++) {
      if (ctrl_->cancel.load()) return false;
      if (ev.textures.size() >= ctrl_->max_textures) {
        degraded_.textures_dropped = true;
        degraded_.detail = "texture count capped";
        break;
      }
      tyn::TextureImage& img = catalog_.images[i];
      if (!img.is_loaded()) continue;

      QlTexture tex;
      bool ok = false;
      try {
        ok = MakePreviewTexture(img, &tex);
      } catch (const std::bad_alloc&) {
        ok = false;
      }
      // Free the decoded source either way: peak holds one full-resolution
      // image, not all of them.
      img.data.clear();

      if (!ok) continue;
      if (texture_bytes + tex.rgba.size() > ctrl_->max_texture_bytes) {
        degraded_.textures_dropped = true;
        degraded_.detail = "texture budget reached";
        break;
      }
      texture_bytes += tex.rgba.size();
      image_to_texture[i] = static_cast<int>(ev.textures.size());
      ev.textures.push_back(std::move(tex));
    }
    texture_bytes_ = texture_bytes;

    for (const tyn::RenderMaterial& m : catalog_.materials) {
      ev.materials.push_back(ConvertMaterial(m, catalog_, image_to_texture));
    }
    for (const tyn::RenderLight& l : catalog_.lights) {
      QlLight out;
      if (ConvertLight(l, &out)) ev.lights.push_back(out);
    }
    for (const tyn::RenderCamera& c : catalog_.cameras) {
      ev.cameras.push_back(ConvertCamera(c));
    }

    material_count_ = ev.materials.size();
    texture_count_ = ev.textures.size();
    light_count_ = ev.lights.size();
    prim_count_ = catalog_.nodes.size();

    if (!stream_->Push(std::move(ev))) return false;

    ctrl_->phase.store(LoadPhase::Extract);
    ctrl_->meshes_total.store(static_cast<uint32_t>(catalog_.meshes.size()));
    return !ctrl_->cancel.load();
  }

  tyn::GeometryDisposition SelectGeometry(
      const tyn::GeometryInfo& info) override {
    if (ctrl_->cancel.load()) return tyn::GeometryDisposition::Cancel;

    // v1 previews triangle meshes only.
    if (info.kind != tyn::GeometryKind::Mesh) {
      return tyn::GeometryDisposition::Skip;
    }

    const uint64_t tris = ctrl_->triangles_done.load();
    if (tris >= ctrl_->max_triangles) {
      degraded_.triangle_cap_hit = true;
      // Still take a cheap box so the object appears in the frame.
      return info.has_authored_extent ? tyn::GeometryDisposition::Proxy
                                      : tyn::GeometryDisposition::Skip;
    }

    if (geometry_bytes_ + info.estimated_resident_bytes >
        ctrl_->max_geometry_bytes) {
      degraded_.proxy_geometry = true;
      if (degraded_.detail.empty()) degraded_.detail = "geometry budget reached";
      if (info.has_authored_extent) return tyn::GeometryDisposition::Proxy;
      degraded_.geometry_skipped = true;
      return tyn::GeometryDisposition::Skip;
    }

    // Last line of defence: the real process RSS, which catches everything the
    // per-mesh estimates miss (allocator slack, the stage itself).
    std::string why;
    if (MemBudget::Get().WouldExceed(info.estimated_resident_bytes, &why)) {
      degraded_.proxy_geometry = true;
      degraded_.detail = why;
      return info.has_authored_extent ? tyn::GeometryDisposition::Proxy
                                      : tyn::GeometryDisposition::Skip;
    }

    return tyn::GeometryDisposition::Full;
  }

  bool AddMesh(int32_t id, tyn::RenderMesh&& mesh) override {
    if (ctrl_->cancel.load()) return false;

    LoadEvent ev;
    ev.kind = LoadEvent::Kind::Mesh;
    bool ok = false;
    try {
      ok = BuildQlMesh(id, mesh, &ev.mesh);
    } catch (const std::bad_alloc&) {
      // Over budget mid-mesh: drop this one and keep the scene we have.
      degraded_.geometry_skipped = true;
      degraded_.detail = "memory budget reached during geometry conversion";
      ok = false;
    }
    // Release the tydra copy immediately, whether or not we kept it.
    mesh = tyn::RenderMesh{};

    ctrl_->meshes_done.fetch_add(1);
    if (!ok) return !ctrl_->cancel.load();

    geometry_bytes_ += ev.mesh.byte_size();
    triangle_count_ += ev.mesh.triangle_count();
    vertex_count_ += ev.mesh.vertex_count();
    ctrl_->triangles_done.store(triangle_count_);
    bounds_.Expand(ev.mesh.bounds);
    mesh_count_++;

    const uint32_t total = ctrl_->meshes_total.load();
    if (total) {
      ctrl_->phase_permille.store(
          static_cast<int>(1000ull * ctrl_->meshes_done.load() / total));
    }

    return stream_->Push(std::move(ev));
  }

  bool AddPoints(int32_t, tyn::RenderPoints&&) override { return true; }
  bool AddCurves(int32_t, tyn::RenderCurves&&) override { return true; }

  bool EndScene() override {
    LoadEvent ev;
    ev.kind = LoadEvent::Kind::Bounds;
    ev.bounds = bounds_;
    ev.y_up = y_up_;
    return stream_->Push(std::move(ev));
  }

  void AbortScene() override {}

  QlSceneStats stats() const {
    QlSceneStats s;
    s.prim_count = prim_count_;
    s.mesh_count = mesh_count_;
    s.triangle_count = triangle_count_;
    s.vertex_count = vertex_count_;
    s.texture_count = texture_count_;
    s.material_count = material_count_;
    s.light_count = light_count_;
    s.geometry_bytes = geometry_bytes_;
    s.texture_bytes = texture_bytes_;
    return s;
  }
  const QlDegradation& degradation() const { return degraded_; }
  QlAabb bounds() const { return bounds_; }
  bool y_up() const { return y_up_; }

 private:
  bool BuildQlMesh(int32_t id, const tyn::RenderMesh& src, QlMesh* out);

  LoadControl* ctrl_;
  LoadStream* stream_;
  Options opts_;
  PreviewBudget budget_;

  tyn::RenderScene catalog_;
  std::vector<tyn::Matrix4> mesh_world_;
  std::vector<bool> mesh_visible_;

  QlAabb bounds_;
  bool y_up_ = true;

  uint64_t geometry_bytes_ = 0;
  uint64_t texture_bytes_ = 0;
  uint64_t triangle_count_ = 0;
  uint64_t vertex_count_ = 0;
  uint64_t mesh_count_ = 0;
  uint64_t prim_count_ = 0;
  size_t material_count_ = 0;
  size_t texture_count_ = 0;
  size_t light_count_ = 0;

  QlDegradation degraded_;
};

// Convert one tydra mesh into a world-space triangle mesh.
//
// Two paths: when every attribute is per-vertex we keep the converter's index
// buffer as-is (the common case, and the cheap one). When normals or UVs are
// face-varying we de-index into per-corner arrays, because a single index
// buffer cannot address two different attribute layouts.
bool QlSceneSink::BuildQlMesh(int32_t id, const tyn::RenderMesh& src,
                              QlMesh* out) {
  if (!src.is_triangulated || src.triangulated_indices.empty()) return false;
  if (src.points.empty()) return false;

  if (id >= 0 && size_t(id) < mesh_visible_.size() && !mesh_visible_[size_t(id)]) {
    return false;
  }

  tyn::Matrix4 world;
  if (id >= 0 && size_t(id) < mesh_world_.size()) world = mesh_world_[size_t(id)];
  const NormalMatrix nrm = MakeNormalMatrix(world);

  out->name = src.name;
  out->prim_path = src.prim_path;
  out->material_id = src.material_id;
  out->is_proxy = src.is_proxy;

  const size_t corner_count = src.triangulated_indices.size();
  const size_t point_count = src.points.size() / 3;

  const bool have_normals = !src.normals.empty();
  const bool have_uvs = !src.texcoords_0.empty();
  const bool normals_face_varying =
      have_normals && src.normals_interp == tyn::Interpolation::FaceVarying;
  const bool uvs_face_varying =
      have_uvs && src.texcoords_0_interp == tyn::Interpolation::FaceVarying;
  const bool need_deindex = normals_face_varying || uvs_face_varying;

  auto push_position = [&](size_t vi) {
    const float p[3] = {src.points[vi * 3 + 0], src.points[vi * 3 + 1],
                        src.points[vi * 3 + 2]};
    float w[3];
    XformPoint(world, p, w);
    out->positions.push_back(w[0]);
    out->positions.push_back(w[1]);
    out->positions.push_back(w[2]);
    out->bounds.Expand(w);
  };

  auto push_normal = [&](size_t ni) {
    const float n[3] = {src.normals[ni * 3 + 0], src.normals[ni * 3 + 1],
                        src.normals[ni * 3 + 2]};
    float w[3];
    XformNormal(nrm, n, w);
    const float len = std::sqrt(w[0] * w[0] + w[1] * w[1] + w[2] * w[2]);
    const float inv = len > 1e-20f ? 1.0f / len : 0.0f;
    out->normals.push_back(w[0] * inv);
    out->normals.push_back(w[1] * inv);
    out->normals.push_back(w[2] * inv);
  };

  if (!need_deindex) {
    out->positions.reserve(point_count * 3);
    for (size_t i = 0; i < point_count; i++) push_position(i);

    if (have_normals && src.normals.size() / 3 >= point_count) {
      out->normals.reserve(point_count * 3);
      for (size_t i = 0; i < point_count; i++) push_normal(i);
    }
    if (have_uvs && src.texcoords_0.size() / 2 >= point_count) {
      out->uvs.reserve(point_count * 2);
      for (size_t i = 0; i < point_count; i++) {
        out->uvs.push_back(src.texcoords_0[i * 2 + 0]);
        out->uvs.push_back(src.texcoords_0[i * 2 + 1]);
      }
    }

    out->indices.reserve(corner_count);
    for (size_t i = 0; i < corner_count; i++) {
      const uint32_t vi = src.triangulated_indices[i];
      if (vi >= point_count) return false;  // corrupt topology: drop the mesh
      out->indices.push_back(vi);
    }
    return out->triangle_count() > 0;
  }

  // De-indexed path. triangulated_face_vertex_indices maps each triangulated
  // corner back to the authored face-vertex, which is how face-varying arrays
  // are addressed.
  const bool have_fv = src.triangulated_face_vertex_indices.size() ==
                       corner_count;
  if (!have_fv) return false;

  out->positions.reserve(corner_count * 3);
  if (have_normals) out->normals.reserve(corner_count * 3);
  if (have_uvs) out->uvs.reserve(corner_count * 2);
  out->indices.reserve(corner_count);

  const size_t normal_count = src.normals.size() / 3;
  const size_t uv_count = src.texcoords_0.size() / 2;

  for (size_t i = 0; i < corner_count; i++) {
    const uint32_t vi = src.triangulated_indices[i];
    const uint32_t fv = src.triangulated_face_vertex_indices[i];
    if (vi >= point_count) return false;
    push_position(vi);

    if (have_normals) {
      const size_t ni = normals_face_varying ? fv : vi;
      if (ni < normal_count) {
        push_normal(ni);
      } else {
        out->normals.push_back(0.0f);
        out->normals.push_back(1.0f);
        out->normals.push_back(0.0f);
      }
    }
    if (have_uvs) {
      const size_t ui = uvs_face_varying ? fv : vi;
      if (ui < uv_count) {
        out->uvs.push_back(src.texcoords_0[ui * 2 + 0]);
        out->uvs.push_back(src.texcoords_0[ui * 2 + 1]);
      } else {
        out->uvs.push_back(0.0f);
        out->uvs.push_back(0.0f);
      }
    }
    out->indices.push_back(static_cast<uint32_t>(i));
  }
  return out->triangle_count() > 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// The worker body
// ---------------------------------------------------------------------------

void RunLoad(const std::string& path, const Options& opts,
             const PreviewBudget& budget, std::shared_ptr<LoadControl> ctrl,
             std::shared_ptr<LoadStream> stream) {
  const auto t0 = std::chrono::steady_clock::now();

  auto fail = [&](const std::string& msg) {
    ctrl->phase.store(LoadPhase::Failed);
    LoadEvent ev;
    ev.kind = LoadEvent::Kind::Failed;
    ev.error = msg;
    stream->Push(std::move(ev));
  };

  auto progress = [&](LoadPhase phase, int permille, const std::string& msg) {
    ctrl->phase.store(phase);
    ctrl->phase_permille.store(permille);
    LoadEvent ev;
    ev.kind = LoadEvent::Kind::Progress;
    ev.phase = phase;
    ev.permille = permille;
    ev.message = msg;
    stream->Push(std::move(ev));
  };

  ctrl->phase.store(LoadPhase::Open);
  progress(LoadPhase::Open, 0, "opening");

  // ---- Load + compose ------------------------------------------------------
  tnext::StageSessionOptions sopts;
  sopts.compose = opts.compose;
  sopts.max_total_memory = static_cast<size_t>(budget.stage);
  // The composed stage is all we need; dropping the parsed dependency layers
  // and the PCP cache afterwards is a large part of staying inside the budget.
  sopts.cache_retention = tnext::CacheRetention::LayersOnly;

  bool budget_hit_during_compose = false;
  std::string budget_why;

  sopts.progress_callback =
      [&](const tnext::ProgressEvent& e) -> bool {
    if (ctrl->cancel.load()) return false;

    LoadPhase phase = LoadPhase::Open;
    switch (e.phase) {
      case tnext::ProgressPhase::RootLoad: phase = LoadPhase::Open; break;
      case tnext::ProgressPhase::Compose:
      case tnext::ProgressPhase::Recompose:
      case tnext::ProgressPhase::PreviewCompose:
        phase = LoadPhase::Compose;
        break;
    }
    ctrl->phase.store(phase);
    ctrl->phase_permille.store(static_cast<int>(e.progress * 1000.0f));

    // Returning false aborts composition mid-flight. That is the whole reason
    // the budget is enforced here rather than after the load: a stage that
    // would bust the cap must never be fully built first.
    if (MemBudget::Get().WouldExceed(e.estimated_resident_bytes, &budget_why)) {
      budget_hit_during_compose = true;
      return false;
    }
    return true;
  };

  tnext::StageSession session;
  std::string load_error;
  bool opened = false;
  try {
    opened = session.OpenFile(path, sopts);
  } catch (const std::bad_alloc&) {
    opened = false;
    load_error = "out of budget while loading";
  }

  if (ctrl->cancel.load()) {
    ctrl->phase.store(LoadPhase::Cancelled);
    return;
  }

  QlDegradation degraded;

  if (!opened && budget_hit_during_compose && opts.compose) {
    // Rung 4 of the ladder: fall back to the uncomposed root layer. A partial
    // preview beats no preview, as long as we say so.
    degraded.uncomposed = true;
    degraded.detail = budget_why;
    tnext::StageSessionOptions flat = sopts;
    flat.compose = false;
    flat.progress_callback = [&](const tnext::ProgressEvent&) -> bool {
      return !ctrl->cancel.load();
    };
    try {
      opened = session.OpenFile(path, flat);
    } catch (const std::bad_alloc&) {
      opened = false;
    }
  }

  if (!opened) {
    if (ctrl->cancel.load()) {
      ctrl->phase.store(LoadPhase::Cancelled);
      return;
    }
    std::string msg = session.GetError();
    if (msg.empty()) msg = load_error;
    if (msg.empty()) msg = "failed to open " + path;
    fail(msg);
    return;
  }

  if (ctrl->cancel.load()) {
    ctrl->phase.store(LoadPhase::Cancelled);
    return;
  }

  // ---- Convert -------------------------------------------------------------
  progress(LoadPhase::Extract, 0, "extracting geometry");

  tyn::ConverterConfig cfg;
  cfg.mesh.triangulate = true;
  cfg.mesh.compute_normals = true;
  cfg.mesh.compute_tangents = false;
  cfg.mesh.build_vertex_indices = true;
  cfg.mesh.use_chunked_arrays = true;
  cfg.material.load_textures = true;
  cfg.material.allow_missing_textures = true;

  // Decode + downsample in one step, inside the loader, so the full-resolution
  // image is never handed back to the converter and peak stays at one image.
  TextureSource tex_source;
  tex_source.Init(path, static_cast<size_t>(budget.stage));
  std::atomic<uint64_t> texture_bytes_loaded{0};
  cfg.material.custom_texture_loader =
      [&](const std::string& asset_path, tyn::TextureImage* out) -> bool {
    if (ctrl->cancel.load()) return false;
    if (texture_bytes_loaded.load() >= budget.textures) return false;

    std::vector<uint8_t> bytes;
    if (!tex_source.Read(asset_path, &bytes)) return false;

    DecodedImage img;
    if (!DecodeImageToRgba(bytes.data(), bytes.size(),
                           QlTexture::kMaxTextureDim, &img)) {
      return false;
    }
    bytes.clear();
    bytes.shrink_to_fit();

    out->width = img.width;
    out->height = img.height;
    out->channels = 4;
    out->component_type = tyn::ComponentType::UInt8;
    // Unknown lets the converter apply the authored colorSpace rather than our
    // guess (see the LoadTexture call site in render-converter.cc).
    out->color_space = tyn::ColorSpace::Unknown;
    out->resolved_path = asset_path;
    if (!out->data.append(img.rgba.data(), img.rgba.size())) return false;

    texture_bytes_loaded.fetch_add(img.rgba.size());
    return true;
  };
  cfg.material.assign_default_material = true;
  // Preview shading works in linear; textures are tagged so the sampler can
  // decode sRGB itself.
  cfg.material.target_color_space = tyn::ColorSpace::sRGB;
  cfg.point_instancer.duplicate_meshes = false;
  cfg.animation.bake_value_clips = false;
  cfg.cancel_callback = [&] { return ctrl->cancel.load(); };

  {
    const size_t slash = path.find_last_of("/\\");
    if (slash != std::string::npos) cfg.asset_base_dir = path.substr(0, slash);
  }

  QlSceneSink sink(ctrl.get(), stream.get(), opts, budget);
  tyn::RenderSceneConverter converter(cfg);

  tyn::StreamConvertResult result;
  try {
    result = converter.ConvertToSink(session.GetStage(), &sink);
  } catch (const std::bad_alloc&) {
    result.success = false;
    result.error = "out of budget during scene conversion";
  }

  if (ctrl->cancel.load()) {
    ctrl->phase.store(LoadPhase::Cancelled);
    return;
  }

  // The stage is no longer needed once the geometry has been copied out.
  session.ReleaseCompositionCache();
  session.TrimCaches();

  if (!result.success && sink.stats().mesh_count == 0) {
    fail(result.error.empty() ? "no renderable geometry" : result.error);
    return;
  }

  QlDegradation final_degraded = sink.degradation();
  final_degraded.uncomposed = final_degraded.uncomposed || degraded.uncomposed;
  if (final_degraded.detail.empty()) final_degraded.detail = degraded.detail;

  QlSceneStats stats = sink.stats();
  stats.load_seconds =
      std::chrono::duration<double>(std::chrono::steady_clock::now() - t0)
          .count();

  ctrl->phase.store(LoadPhase::Done);
  ctrl->phase_permille.store(1000);

  LoadEvent done;
  done.kind = LoadEvent::Kind::Complete;
  done.stats = stats;
  done.degraded = final_degraded;
  done.bounds = sink.bounds();
  done.y_up = sink.y_up();
  stream->Push(std::move(done));
}

// ---------------------------------------------------------------------------
// Loader
// ---------------------------------------------------------------------------

Loader::~Loader() {
  Cancel();
  Join();
}

void Loader::Cancel() {
  if (control_) control_->cancel.store(true);
  if (stream_) stream_->Cancel();
}

void Loader::Join() {
  if (thread_.joinable()) thread_.join();
  running_.store(false);
}

void Loader::Start(const std::string& path, const Options& opts,
                   const PreviewBudget& budget) {
  // Cancel first, then join: the previous worker sees the flag while this
  // thread is still winding it down, so the wait is bounded by one mesh rather
  // than by the whole remaining file.
  Cancel();
  Join();

  path_ = path;
  control_ = std::make_shared<LoadControl>();
  control_->Reset();
  control_->max_stage_bytes = budget.stage;
  control_->max_geometry_bytes = budget.geometry;
  control_->max_texture_bytes = budget.textures;

  // Queue bound: a slice of the render share. Big enough that conversion is not
  // stalled by an idle UI, small enough that a huge scene cannot pile up.
  const uint64_t queue_bytes = std::max<uint64_t>(budget.render / 2, 4u << 20);
  stream_ = std::make_shared<LoadStream>(queue_bytes);

  auto ctrl = control_;
  auto stream = stream_;
  running_.store(true);
  thread_ = std::thread([this, path, opts, budget, ctrl, stream] {
    RunLoad(path, opts, budget, ctrl, stream);
    running_.store(false);
  });
}

}  // namespace tusdql
