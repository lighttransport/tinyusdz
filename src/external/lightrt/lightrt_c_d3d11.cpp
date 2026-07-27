// SPDX-License-Identifier: Apache-2.0
// lightrt_c_d3d11.cpp — Direct3D 11 compute trace backend for LightRT.
// See lightrt_c_d3d11.h. Windows-only; empty TU elsewhere.

#if defined(_WIN32)

#include "lightrt_c_d3d11.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

#include "d3d/shaders/trace_bvh_hlsl.h"  // const char trace_bvh_hlsl[]

namespace {

// LRTS serialization header — must match lightrt_c_tri.c / lightrt_c_vk.c.
struct lrts_header {
  char magic[4];
  uint32_t version, endian, flags, layout, prim_kind;
  uint32_t node_count, block_count, root, node_stride, block_stride, reserved0;
  float root_lo[3], root_hi[3];
  uint64_t node_offset, block_offset, file_size;
};

// cbuffer PC { root, node_count, block_count, ray_count }  (16 bytes)
struct TracePC {
  uint32_t root, node_count, block_count, ray_count;
};

// Bucket the traversal stack like the Vulkan path (so shaders are reused).
uint32_t trace_stack_for(uint32_t max_depth, uint32_t w) {
  uint32_t need = max_depth * (w - 1u) + w + 1u;
  static const uint32_t buckets[] = {32u, 64u, 128u, 256u};
  for (int i = 0; i < 4; i++)
    if (need <= buckets[i]) return buckets[i];
  return 0;  // too deep for the compute stack
}

template <class T>
void safe_release(T*& p) { if (p) { p->Release(); p = nullptr; } }

// One cached compute shader keyed by (BVH width, stack depth).
struct ShaderSlot {
  uint32_t w = 0, stack = 0;
  ID3D11ComputeShader* cs = nullptr;
};

}  // namespace

struct lrt_d3d11_engine {
  ID3D11Device* dev = nullptr;
  ID3D11DeviceContext* ctx = nullptr;
  std::string name;
  std::string last_error;
  ShaderSlot slots[8];
};

static void set_err(lrt_d3d11_engine* e, const char* msg) {
  if (e) e->last_error = msg ? msg : "";
}

extern "C" lrt_d3d11_engine* lrt_d3d11_engine_create(int prefer_discrete,
                                                     lrt_result* err) {
  lrt_d3d11_engine* e = new (std::nothrow) lrt_d3d11_engine();
  if (!e) { if (err) *err = LRT_RESULT_OUT_OF_MEMORY; return nullptr; }

  // Pick an adapter (optionally a discrete one). Enumerate via DXGI.
  IDXGIFactory* factory = nullptr;
  IDXGIAdapter* chosen = nullptr;
  if (SUCCEEDED(CreateDXGIFactory(__uuidof(IDXGIFactory),
                                  reinterpret_cast<void**>(&factory)))) {
    IDXGIAdapter* a = nullptr;
    SIZE_T best_vram = 0;
    for (UINT i = 0; factory->EnumAdapters(i, &a) != DXGI_ERROR_NOT_FOUND; ++i) {
      DXGI_ADAPTER_DESC d{};
      a->GetDesc(&d);
      bool sw = (d.VendorId == 0x1414);  // Microsoft Basic Render (WARP)
      if (!chosen || (prefer_discrete && !sw &&
                      d.DedicatedVideoMemory > best_vram)) {
        safe_release(chosen);
        chosen = a;  // keep (ref held by enum); take ownership
        best_vram = d.DedicatedVideoMemory;
        continue;
      }
      a->Release();
    }
    factory->Release();
  }

  D3D_FEATURE_LEVEL want[] = {D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0};
  D3D_FEATURE_LEVEL got{};
  UINT flags = 0;
  HRESULT hr = D3D11CreateDevice(
      chosen, chosen ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
      nullptr, flags, want, 2, D3D11_SDK_VERSION, &e->dev, &got, &e->ctx);
  if (FAILED(hr)) {
    hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                           want, 2, D3D11_SDK_VERSION, &e->dev, &got, &e->ctx);
  }
  safe_release(chosen);
  if (FAILED(hr) || !e->dev) {
    set_err(e, "D3D11CreateDevice failed");
    if (err) *err = LRT_RESULT_NOT_BUILT;
    delete e;
    return nullptr;
  }

  // Device name via the device's own adapter.
  IDXGIDevice* dxdev = nullptr;
  if (SUCCEEDED(e->dev->QueryInterface(__uuidof(IDXGIDevice),
                                       reinterpret_cast<void**>(&dxdev)))) {
    IDXGIAdapter* ad = nullptr;
    if (SUCCEEDED(dxdev->GetAdapter(&ad))) {
      DXGI_ADAPTER_DESC d{};
      ad->GetDesc(&d);
      char buf[160];
      WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, buf, sizeof(buf),
                          nullptr, nullptr);
      e->name = buf;
      ad->Release();
    }
    dxdev->Release();
  }
  if (err) *err = LRT_RESULT_OK;
  return e;
}

extern "C" void lrt_d3d11_engine_destroy(lrt_d3d11_engine* e) {
  if (!e) return;
  for (auto& s : e->slots) safe_release(s.cs);
  safe_release(e->ctx);
  safe_release(e->dev);
  delete e;
}

extern "C" const char* lrt_d3d11_engine_device_name(const lrt_d3d11_engine* e) {
  return e ? e->name.c_str() : "";
}
extern "C" const char* lrt_d3d11_engine_last_error(const lrt_d3d11_engine* e) {
  return e ? e->last_error.c_str() : "";
}

// Compile (and cache) the trace compute shader for a given BVH width + stack.
static ID3D11ComputeShader* get_shader(lrt_d3d11_engine* e, uint32_t w,
                                        uint32_t stack) {
  for (auto& s : e->slots)
    if (s.cs && s.w == w && s.stack == stack) return s.cs;

  char w_s[16], stk_s[16];
  std::snprintf(w_s, sizeof(w_s), "%uu", w);
  std::snprintf(stk_s, sizeof(stk_s), "%uu", stack);
  D3D_SHADER_MACRO macros[] = {
      {"SPIRV_CROSS_CONSTANT_ID_0", w_s},     // W (BVH width)
      {"SPIRV_CROSS_CONSTANT_ID_1", stk_s},   // STACK
      {nullptr, nullptr}};
  ID3DBlob* code = nullptr;
  ID3DBlob* errs = nullptr;
  HRESULT hr = D3DCompile(trace_bvh_hlsl, std::strlen(trace_bvh_hlsl),
                          "trace_bvh.hlsl", macros, nullptr, "main", "cs_5_0",
                          0, 0, &code, &errs);
  if (FAILED(hr)) {
    std::string m = "D3DCompile(trace_bvh) failed";
    if (errs) { m += ": "; m.append((const char*)errs->GetBufferPointer()); }
    set_err(e, m.c_str());
    safe_release(errs);
    safe_release(code);
    return nullptr;
  }
  safe_release(errs);
  ID3D11ComputeShader* cs = nullptr;
  hr = e->dev->CreateComputeShader(code->GetBufferPointer(),
                                   code->GetBufferSize(), nullptr, &cs);
  safe_release(code);
  if (FAILED(hr)) { set_err(e, "CreateComputeShader failed"); return nullptr; }

  // Insert into the cache (evict slot 0 if full).
  for (auto& s : e->slots) {
    if (!s.cs) { s.cs = cs; s.w = w; s.stack = stack; return cs; }
  }
  safe_release(e->slots[0].cs);
  e->slots[0] = {w, stack, cs};
  return cs;
}

// Create a raw (ByteAddressBuffer) buffer, optionally with an SRV and/or UAV.
static ID3D11Buffer* make_raw_buffer(ID3D11Device* dev, const void* data,
                                     uint32_t bytes, bool uav,
                                     ID3D11ShaderResourceView** srv,
                                     ID3D11UnorderedAccessView** uav_out) {
  D3D11_BUFFER_DESC bd{};
  bd.ByteWidth = (bytes + 3u) & ~3u;  // 4-byte aligned
  bd.Usage = D3D11_USAGE_DEFAULT;
  bd.BindFlags = D3D11_BIND_SHADER_RESOURCE | (uav ? D3D11_BIND_UNORDERED_ACCESS : 0);
  bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
  D3D11_SUBRESOURCE_DATA init{};
  init.pSysMem = data;
  ID3D11Buffer* buf = nullptr;
  if (FAILED(dev->CreateBuffer(&bd, data ? &init : nullptr, &buf))) return nullptr;
  UINT n4 = bd.ByteWidth / 4u;
  if (srv) {
    D3D11_SHADER_RESOURCE_VIEW_DESC sd{};
    sd.Format = DXGI_FORMAT_R32_TYPELESS;
    sd.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
    sd.BufferEx.NumElements = n4;
    sd.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
    if (FAILED(dev->CreateShaderResourceView(buf, &sd, srv))) { buf->Release(); return nullptr; }
  }
  if (uav && uav_out) {
    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = DXGI_FORMAT_R32_TYPELESS;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.NumElements = n4;
    ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    if (FAILED(dev->CreateUnorderedAccessView(buf, &ud, uav_out))) { buf->Release(); return nullptr; }
  }
  return buf;
}

extern "C" int lrt_d3d11_trace_scene(lrt_d3d11_engine* e, const lrt_tri_scene* s,
                                     const lrt_ray* rays, uint32_t n,
                                     lrt_hit* out, lrt_result* err) {
  if (!e || !s || (n && (!rays || !out))) {
    if (err) *err = LRT_RESULT_INVALID_ARGUMENT;
    return -1;
  }
  if (n == 0) { if (err) *err = LRT_RESULT_OK; return 0; }

  void* blob = nullptr;
  size_t blob_n = 0;
  lrt_result sr = lrt_tri_scene_save_to_memory(s, &blob, &blob_n);
  if (sr != LRT_RESULT_OK) {
    set_err(e, "scene not GPU-traceable (quantized/curve/user?)");
    if (err) *err = sr;
    return -1;
  }
  const lrts_header* h = (const lrts_header*)blob;
  uint32_t w = h->layout;  // 4 or 8
  if (w != 4 && w != 8) {
    set_err(e, "unsupported BVH layout for D3D11 trace (need BVH4/BVH8)");
    free(blob);
    if (err) *err = LRT_RESULT_INVALID_ARGUMENT;
    return -1;
  }

  lrt_tri_stats st;
  lrt_tri_scene_stats(s, &st);
  uint32_t stack = trace_stack_for(st.max_depth, w);
  if (stack == 0) {
    set_err(e, "BVH too deep for the GPU compute stack");
    free(blob);
    if (err) *err = LRT_RESULT_TRAVERSAL_OVERFLOW;
    return -1;
  }

  ID3D11ComputeShader* cs = get_shader(e, w, stack);
  if (!cs) { free(blob); if (err) *err = LRT_RESULT_OUT_OF_MEMORY; return -1; }

  const uint32_t nodes_bytes = h->node_count * h->node_stride;
  const uint32_t blocks_bytes = h->block_count * h->block_stride;

  ID3D11Buffer *b_nodes = nullptr, *b_blocks = nullptr, *b_rays = nullptr,
               *b_hits = nullptr, *b_cb = nullptr, *b_stage = nullptr;
  ID3D11ShaderResourceView *srv_nodes = nullptr, *srv_blocks = nullptr,
                           *srv_rays = nullptr;
  ID3D11UnorderedAccessView* uav_hits = nullptr;
  int result = -1;

  b_nodes = make_raw_buffer(e->dev, (const char*)blob + h->node_offset,
                            nodes_bytes, false, &srv_nodes, nullptr);
  b_blocks = make_raw_buffer(e->dev, (const char*)blob + h->block_offset,
                             blocks_bytes, false, &srv_blocks, nullptr);
  b_rays = make_raw_buffer(e->dev, rays, n * (uint32_t)sizeof(lrt_ray), false,
                           &srv_rays, nullptr);
  b_hits = make_raw_buffer(e->dev, nullptr, n * (uint32_t)sizeof(lrt_hit), true,
                           nullptr, &uav_hits);
  if (!b_nodes || !b_blocks || !b_rays || !b_hits) {
    set_err(e, "buffer/view creation failed");
    if (err) *err = LRT_RESULT_OUT_OF_MEMORY;
    goto cleanup;
  }

  {
    TracePC pc{h->root, h->node_count, h->block_count, n};
    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = sizeof(TracePC);  // 16 bytes
    cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SUBRESOURCE_DATA cinit{};
    cinit.pSysMem = &pc;
    if (FAILED(e->dev->CreateBuffer(&cbd, &cinit, &b_cb))) {
      set_err(e, "constant buffer creation failed");
      if (err) *err = LRT_RESULT_OUT_OF_MEMORY;
      goto cleanup;
    }
  }

  {
    ID3D11DeviceContext* c = e->ctx;
    c->CSSetShader(cs, nullptr, 0);
    ID3D11ShaderResourceView* srvs[3] = {srv_nodes, srv_blocks, srv_rays};
    c->CSSetShaderResources(0, 3, srvs);  // t0,t1,t2
    UINT init_counts = 0;
    c->CSSetUnorderedAccessViews(3, 1, &uav_hits, &init_counts);  // u3
    c->CSSetConstantBuffers(0, 1, &b_cb);  // b0
    UINT groups = (n + 63u) / 64u;
    c->Dispatch(groups, 1, 1);
    // Unbind UAV so we can copy it out.
    ID3D11UnorderedAccessView* none_uav = nullptr;
    c->CSSetUnorderedAccessViews(3, 1, &none_uav, &init_counts);
  }

  {  // Read hits back via a staging buffer.
    D3D11_BUFFER_DESC sd{};
    sd.ByteWidth = n * (uint32_t)sizeof(lrt_hit);
    sd.Usage = D3D11_USAGE_STAGING;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    if (FAILED(e->dev->CreateBuffer(&sd, nullptr, &b_stage))) {
      set_err(e, "staging buffer creation failed");
      if (err) *err = LRT_RESULT_OUT_OF_MEMORY;
      goto cleanup;
    }
    e->ctx->CopyResource(b_stage, b_hits);
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(e->ctx->Map(b_stage, 0, D3D11_MAP_READ, 0, &mapped))) {
      set_err(e, "map(staging) failed");
      if (err) *err = LRT_RESULT_OUT_OF_MEMORY;
      goto cleanup;
    }
    std::memcpy(out, mapped.pData, n * sizeof(lrt_hit));
    e->ctx->Unmap(b_stage, 0);
  }

  {
    int hits = 0;
    for (uint32_t i = 0; i < n; i++)
      if (out[i].prim_id != LRT_TRI_NO_HIT) hits++;
    if (err) *err = LRT_RESULT_OK;
    result = hits;
  }

cleanup:
  safe_release(srv_nodes);
  safe_release(srv_blocks);
  safe_release(srv_rays);
  safe_release(uav_hits);
  safe_release(b_nodes);
  safe_release(b_blocks);
  safe_release(b_rays);
  safe_release(b_hits);
  safe_release(b_cb);
  safe_release(b_stage);
  free(blob);
  return result;
}

#endif  // _WIN32
