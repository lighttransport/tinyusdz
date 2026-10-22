// SPDX-License-Identifier: Apache-2.0
#include <algorithm>
#include <cctype>
#include <fstream>
#include <limits>

#include "external/filesystem/include/ghc/filesystem.hpp"
#include "import.hh"
namespace fs = ghc::filesystem;
namespace lightusd {
namespace tydra {
namespace import_asset {
bool Reader::Charge(size_t amount, std::string* err) {
  if (amount > options.max_memory - std::min(bytes, options.max_memory)) {
    failure = "Import memory budget exceeded";
    if (err) *err = failure;
    return false;
  }
  bytes += amount;
  return true;
}
bool Reader::ChargeArray(size_t count, size_t stride, std::string* err) {
  if (stride && count > SIZE_MAX / stride) {
    failure = "Import array size overflow (memory limit)";
    if (err) *err = failure;
    return false;
  }
  return Charge(count * stride, err);
}
void Reader::Warn(const std::string& message) {
  const size_t cap = 1024 * 1024;
  if (warnings.size() >= cap) return;
  warnings.append(message.data(), std::min(message.size(), cap - warnings.size()));
  if (warnings.size() < cap) warnings += '\n';
}
bool Reader::Read(const std::string& name, const std::string& anchor,
                  std::vector<uint8_t>* data, std::string* resolved,
                  std::string* err) {
  if (!allow_unanchored && !options.read) {
    if (err) *err = "External assets require a source anchor or resolver";
    return false;
  }
  if (options.read) {
    if (!options.read(name, anchor, data, resolved, err)) return false;
    if (data->size() > options.max_input) {
      if (err) *err = "Asset input limit exceeded";
      return false;
    }
    return Charge(data->size(), err);
  }
  std::string path = name;
  if (path.rfind("package://", 0) == 0) {
    size_t end = path.find('/', 10);
    std::string package =
        path.substr(10, end == std::string::npos ? end : end - 10);
    auto found = options.packages.find(package);
    if (found == options.packages.end()) {
      if (err) *err = "Unmapped URDF package: " + package;
      return false;
    }
    path = (fs::path(found->second) /
            (end == std::string::npos ? "" : path.substr(end + 1)))
               .string();
  } else if (path.rfind("file://", 0) == 0)
    path = path.substr(7);
  std::error_code ec;
  fs::path base =
      anchor.empty() ? fs::current_path(ec) : fs::path(anchor).parent_path();
  fs::path candidate =
      fs::path(path).is_absolute() ? fs::path(path) : base / path;
  if (!fs::exists(candidate, ec))
    for (const std::string& dir : options.asset_dirs) {
      fs::path p = fs::path(dir) / path;
      if (fs::exists(p, ec)) {
        candidate = p;
        break;
      }
    }
  candidate = fs::weakly_canonical(candidate, ec);
  // A dependency may be anywhere under the root document or explicitly
  // supplied asset/package roots. Canonicalization also rejects symlink
  // escapes.
  if (!anchor.empty()) {
    std::vector<std::string> roots = options.asset_dirs;
    for (const auto& p : options.packages) roots.push_back(p.second);
    roots.push_back(base.string());
    if (!root_source.empty())
      roots.push_back(fs::path(root_source).parent_path().string());
    bool allowed = false;
    for (const std::string& r : roots) {
      fs::path root = fs::weakly_canonical(r, ec);
      auto ci = candidate.begin(), ri = root.begin();
      for (; ri != root.end() && ci != candidate.end() && *ri == *ci;
           ++ri, ++ci) {
      }
      if (ri == root.end()) {
        allowed = true;
        break;
      }
    }
    if (!allowed) {
      if (err) *err = "Asset outside allowed roots: " + candidate.string();
      return false;
    }
  }
  uintmax_t size = fs::file_size(candidate, ec);
  if (ec) {
    if (err) *err = "Cannot read asset: " + candidate.string();
    return false;
  }
  if (size > options.max_input ||
      size > size_t(std::numeric_limits<std::streamsize>::max())) {
    if (err) *err = "Asset input limit exceeded";
    return false;
  }
  if (!Charge(static_cast<size_t>(size), err)) return false;
  std::ifstream file(candidate.string(), std::ios::binary);
  if (!file) {
    if (err) *err = "Cannot open asset: " + candidate.string();
    return false;
  }
  data->resize(static_cast<size_t>(size));
  if (size && !file.read(reinterpret_cast<char*>(data->data()),
                         static_cast<std::streamsize>(size))) {
    if (err) *err = "Failed to read asset: " + candidate.string();
    return false;
  }
  *resolved = candidate.generic_string();
  return true;
}

bool GatherTextures(Reader& reader, Result* result, std::string* err) {
  Json* mats = result->payload.find("materials");
  if (!mats) return true;
  size_t index = 0;
  for (Json& mat : *mats) {
    auto retain = [&](const std::string& file) -> std::string {
      if (file.empty()) return {};
      std::vector<uint8_t> data;
      std::string resolved, why;
      if (!reader.Read(file, reader.root_source, &data, &resolved, &why)) {
        reader.Warn("Texture: " + why);
        return {};
      }
      std::string name = "textures/" + std::to_string(index++) + "_" +
                         fs::path(file).filename().string();
      result->assets.emplace(name, std::move(data));
      return name;
    };
    if (mat.contains("texture_abs")) {
      std::string file = mat["texture_abs"].get<std::string>();
      std::string package = retain(file);
      if (!package.empty()) {
        mat["texture"] = file;
        mat["texturePackage"] = package;
      }
    }
    if (Json* maps = mat.find("maps"))
      for (Json& map : *maps) {
        std::string package = retain(map["file"].get<std::string>());
        if (!package.empty()) map["package"] = package;
      }
  }
  (void)err;
  return reader.failure.empty();
}
}  // namespace import_asset
}  // namespace tydra
}  // namespace lightusd
