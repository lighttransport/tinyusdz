// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// next_usdzconvert - USD -> USDZ conversion whose FLATTEN is done by the
// freestanding next-core + next_io stack (mmap file I/O, lazy arrays, parallel
// compose), then handed to the shared texture resize/encode + zip packager.
//
//   next_usdzconvert <root.usd> <out.usdz> [-resizeTextures N] [-textureFormat png|jpeg|keep]
//                    [-variant set=name ...] [-numThreads N] [-v]
//
// Pipeline:
//   1. next-core flatten (pipeline::FlattenUSDFileToUSDC) -> collect the
//      referenced asset paths (textures).
//   2. Resolve each referenced texture to an absolute path via a basename index
//      of the scene directory (the next flatten keeps texture paths anchored to
//      their authoring material's dir, so a plain root-relative package can't
//      find them). Build an asset_path_remap.
//   3. next-core flatten again WITH the remap -> a USDC whose texture
//      inputs:file are absolute (and so resolvable from anywhere).
//   4. Hand that USDC to the legacy usdz::Convert texture packager
//      (flatten=false) for resize/encode/zip.

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "next/pipeline/flatten.hh"
#include "next/reader/usdz-reader.hh"
#include "next/writer/usdz-writer.hh"
#include "usdz-convert.hh"

namespace {

std::string ToLower(std::string s) {
  for (char& c : s) c = static_cast<char>(std::tolower((unsigned char)c));
  return s;
}
std::string Basename(const std::string& p) {
  size_t s = p.find_last_of("/\\");
  return s == std::string::npos ? p : p.substr(s + 1);
}
std::string Dirname(const std::string& p) {
  size_t s = p.find_last_of("/\\");
  return s == std::string::npos ? std::string(".") : p.substr(0, s);
}
std::string Ext(const std::string& p) {
  std::string b = Basename(p);
  size_t d = b.find_last_of('.');
  return d == std::string::npos ? std::string() : ToLower(b.substr(d + 1));
}
bool IsImageExt(const std::string& e) {
  return e == "png" || e == "jpg" || e == "jpeg" || e == "exr" || e == "tga" ||
         e == "bmp" || e == "hdr" || e == "tif" || e == "tiff";
}

// Recursively index image files under `dir`: basename(lower) -> absolute path.
// First occurrence wins (texture basenames are unique in these UE exports).
void IndexImages(const std::string& dir, std::map<std::string, std::string>* idx) {
#if !defined(_WIN32)
  DIR* d = opendir(dir.c_str());
  if (!d) return;
  struct dirent* ent;
  while ((ent = readdir(d)) != nullptr) {
    std::string name = ent->d_name;
    if (name == "." || name == "..") continue;
    std::string full = dir + "/" + name;
    struct stat st;
    if (stat(full.c_str(), &st) != 0) continue;
    if (S_ISDIR(st.st_mode)) {
      IndexImages(full, idx);
    } else if (S_ISREG(st.st_mode) && IsImageExt(Ext(name))) {
      idx->emplace(ToLower(Basename(name)), full);
    }
  }
  closedir(d);
#else
  (void)dir; (void)idx;  // Windows: not implemented for this bench tool.
#endif
}

}  // namespace

int main(int argc, char** argv) {
  using namespace tinyusdz;

  std::string input, output;
  int resize = 0, num_threads = 0;
  std::string tex_format = "keep";
  bool verbose = false;
  std::map<std::string, std::string> variant_overrides;

  std::vector<std::string> pos;
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    if (a == "-resizeTextures" && i + 1 < argc) resize = std::atoi(argv[++i]);
    else if (a == "-textureFormat" && i + 1 < argc) tex_format = argv[++i];
    else if (a == "-numThreads" && i + 1 < argc) num_threads = std::atoi(argv[++i]);
    else if (a == "-variant" && i + 1 < argc) {
      // -variant set=name (repeatable): convert-time variant selection,
      // stronger than authored variantSelections on the same set (e.g.
      // "-variant lod=low" bakes the low LOD into the flattened package).
      std::string sel = argv[++i];
      size_t eq = sel.find('=');
      if (eq == std::string::npos || eq == 0 || eq + 1 >= sel.size()) {
        std::fprintf(stderr,
            "next_usdzconvert: -variant expects <variantSet>=<name>, got '%s'\n",
            sel.c_str());
        return 1;
      }
      variant_overrides[sel.substr(0, eq)] = sel.substr(eq + 1);
    }
    else if (a == "-v") verbose = true;
    else pos.push_back(a);
  }
  if (pos.size() < 2) {
    std::fprintf(stderr,
        "Usage: next_usdzconvert <root.usd> <out.usdz> [-resizeTextures N] "
        "[-textureFormat png|jpeg|keep] [-variant set=name ...] "
        "[-numThreads N] [-v]\n");
    return 1;
  }
  input = pos[0];
  output = pos[1];

  usdz::OutputTextureFormat fmt = usdz::OutputTextureFormat::KeepOriginal;
  if (tex_format == "png") fmt = usdz::OutputTextureFormat::PNG;
  else if (tex_format == "jpeg" || tex_format == "jpg") fmt = usdz::OutputTextureFormat::JPEG;

  // --- 1. next-core flatten: collect referenced assets ------------------------
  next::pipeline::FlattenOptions fopts;
  fopts.read.num_threads = num_threads;
  fopts.use_pcp_compose = true;            // parallel pcp compose (fast path)
  fopts.compose_num_threads = num_threads;  // 0 = auto
  fopts.variant_overrides = variant_overrides;  // -variant set=name selections
  next::pipeline::FlattenStats st1;
  std::vector<uint8_t> flat1;
  std::string err;
  if (!next::pipeline::FlattenUSDFileToUSDC(input, flat1, fopts, &st1, &err)) {
    std::fprintf(stderr, "next_usdzconvert: flatten failed: %s\n", err.c_str());
    return 1;
  }
  if (verbose) {
    std::fprintf(stderr,
        "[next flatten] %.1f ms compose, %zu prims, %zu referenced assets, "
        "%zu composition errors\n",
        st1.compose_ms, st1.prim_count, st1.referenced_assets.size(),
        st1.composition_errors.size());
  }

  // --- 2. rebase texture paths to absolute via a scene image index -----------
  const std::string scene_dir = Dirname(input);
  std::map<std::string, std::string> image_index;
  IndexImages(scene_dir, &image_index);

  std::map<std::string, std::string> remap;
  size_t missing = 0;
  for (const std::string& asset : st1.referenced_assets) {
    if (!IsImageExt(Ext(asset))) continue;
    auto it = image_index.find(ToLower(Basename(asset)));
    if (it != image_index.end()) remap[asset] = it->second;
    else missing++;
  }
  if (verbose) {
    std::fprintf(stderr,
        "[rebase] indexed %zu scene images; remapped %zu texture refs (%zu unresolved)\n",
        image_index.size(), remap.size(), missing);
  }

  // --- 3. re-serialize the (already self-contained) flattened USDC, rewriting
  //        texture paths to absolute. No re-compose -- just read/remap/write. ---
  next::pipeline::FlattenOptions ropts;
  ropts.read.num_threads = num_threads;
  ropts.asset_path_remap = remap;
  next::pipeline::FlattenStats st2;
  std::vector<uint8_t> flat2;
  if (!next::pipeline::FlattenUSDCToUSDC(flat1.data(), flat1.size(), flat2,
                                         ropts, &st2, &err)) {
    std::fprintf(stderr, "next_usdzconvert: remap pass failed: %s\n", err.c_str());
    return 1;
  }

  // Write the next-flattened USDC to a temp file for the packager.
  std::string tmp = Dirname(output) + "/.next_usdzconvert_flat.usdc";
  {
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) { std::fprintf(stderr, "cannot open temp %s\n", tmp.c_str()); return 1; }
    std::fwrite(flat2.data(), 1, flat2.size(), f);
    std::fclose(f);
  }

  // --- 4. shared texture packager (resize/encode/zip), no re-flatten ---------
  usdz::UsdzConvertOptions copts;
  copts.inputs = {tmp};
  copts.output = output;
  // The next-flattened input is self-contained, so this "flatten" is a no-op
  // compose -- but it makes the packager emit a compact USDC root (flatten=false
  // writes an expanded USDA root). Note: the legacy writer lacks next's
  // cross-spec array dedup, so the root is larger than the wasm stream-next
  // output (which uses the next writer).
  copts.flatten = true;
  copts.max_texture_size = resize;
  copts.texture_format = fmt;
  copts.num_threads = num_threads;
  copts.reencode = true;
  copts.verbose = verbose;

  usdz::UsdzConvertStats cstats;
  std::string warn;
  bool ok = usdz::Convert(copts, &cstats, &warn, &err);
#if !defined(_WIN32)
  ::unlink(tmp.c_str());
#endif
  if (!warn.empty()) std::fprintf(stderr, "%s", warn.c_str());
  if (!ok) {
    std::fprintf(stderr, "next_usdzconvert: package failed: %s\n", err.c_str());
    return 1;
  }

  // --- 5. re-serialize the packaged root through the next crate writer so it
  //        gets cross-spec value-block dedup (the legacy writer the packager uses
  //        does not); texture entries pass through verbatim. -------------------
  {
    next::USDZReader zr;
    if (zr.OpenFile(output)) {
      size_t root_idx = SIZE_MAX;
      for (size_t i = 0; i < zr.NumEntries(); i++) {
        std::string e = Ext(zr.EntryName(i));
        if (e == "usdc" || e == "usda" || e == "usd") { root_idx = i; break; }
      }
      std::vector<uint8_t> dedup_root;
      next::pipeline::FlattenOptions ro;
      ro.flatten = false;  // self-contained root -> re-serialize (dedup), no compose
      next::pipeline::FlattenStats rs;
      std::string rerr;
      if (root_idx != SIZE_MAX &&
          next::pipeline::FlattenUSDCToUSDC(zr.EntryData(root_idx),
                                            zr.EntrySize(root_idx), dedup_root,
                                            ro, &rs, &rerr) &&
          dedup_root.size() < zr.EntrySize(root_idx)) {
        std::vector<next::USDZEntry> ents(zr.NumEntries());
        for (size_t i = 0; i < zr.NumEntries(); i++) {
          ents[i].name = zr.EntryName(i);
          if (i == root_idx) {
            ents[i].data = dedup_root.data();
            ents[i].size = dedup_root.size();
          } else {
            ents[i].data = zr.EntryData(i);
            ents[i].size = zr.EntrySize(i);
          }
        }
        if (root_idx != 0) std::swap(ents[0], ents[root_idx]);  // root must be first
        std::vector<uint8_t> repacked;
        if (next::WriteUSDZFromEntriesToMemory(repacked, ents).success) {
          FILE* f = std::fopen(output.c_str(), "wb");
          if (f) {
            std::fwrite(repacked.data(), 1, repacked.size(), f);
            std::fclose(f);
            cstats.output_size = repacked.size();
            if (verbose) {
              std::fprintf(stderr,
                  "[dedup root] root %zu -> %zu bytes (next writer); usdz %zu bytes\n",
                  zr.EntrySize(root_idx), dedup_root.size(), repacked.size());
            }
          }
        }
      }
    }
  }
  std::fprintf(stderr,
      "next_usdzconvert: wrote %s (%zu bytes) — textures: %zu, resized: %zu, "
      "reencoded: %zu, passthrough: %zu\n",
      output.c_str(), cstats.output_size, cstats.num_textures,
      cstats.num_textures_resized, cstats.num_textures_reencoded,
      cstats.num_textures_passthrough);
  return 0;
}
