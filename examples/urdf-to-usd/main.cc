// Native MJCF/URDF-style physics scene to USD exporter.
//
// This example currently implements a native MJCF path for MuJoCo menagerie
// scenes. It parses XML with pugixml, bakes STL/OBJ and simple MJCF primitive
// geometry into the same compact JSON contract used by the JS/WASM demo, then
// reuses tydra::ConvertURDFJsonToUSDStage.

#include "jsonhpp/nlohmann/json.hpp"
#include "pugixml.hpp"
#include "tinyusdz.hh"
#include "tydra/urdf-to-usd.hh"
#include "usda-writer.hh"
#include "usdc-writer.hh"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct Options {
  std::string input_filename;
  std::string output_filename;
  std::string format = "usda";
  std::string input_format = "auto";
  std::string up_axis = "Z";
  std::string dump_json_filename;
  bool allow_missing = false;
  bool tessellate_collision_shapes = false;
  bool verbose = false;
};

struct MeshAsset {
  fs::path path;
  std::array<double, 3> scale{{1.0, 1.0, 1.0}};
};

struct MeshData {
  std::vector<float> positions;
  std::vector<float> normals;
  std::vector<int32_t> indices;
};

struct MeshPayload {
  std::string name;
  std::vector<double> matrix;
  MeshData mesh;
  nlohmann::json shape;
};

struct Stats {
  size_t links{0};
  size_t joints{0};
  size_t visuals{0};
  size_t collisions{0};
  size_t actuators{0};
};

void PrintHelp() {
  std::cout << R"(Native URDF/MJCF -> USD Physics + MuJoCo + Newton exporter

Usage:
  urdf-to-usd <scene.xml> --input-format mjcf --format usdc -o /tmp/robot.usdc

Options:
  --input-format <fmt>  Input format: auto, mjcf (default: auto)
  --format <fmt>        Output format: usda, usdc, usdz, all (default: usda)
  -o, --output <path>   Output file path, or base path when --format all
  --up-axis <axis>      Export up axis: Z or Y (default: Z)
  --dump-json <path>    Write the intermediate converter JSON payload
  --allow-missing       Skip unsupported or missing mesh assets
  --tessellate-collision-shapes
                       Tessellate primitive collision geoms to meshes.
                       Default: export primitive collisions as USD native shapes.
  -v, --verbose         Print details
  -h, --help            Show this help
)";
}

bool HasPrefix(const std::string &s, const std::string &prefix) {
  return s.rfind(prefix, 0) == 0;
}

std::string ToLower(std::string s) {
  for (char &c : s) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return s;
}

bool ParseArgs(int argc, char **argv, Options *opts, std::string *err) {
  for (int i = 1; i < argc; i++) {
    const std::string arg(argv[i]);
    auto need_value = [&](const std::string &name) -> std::string {
      if (i + 1 >= argc) {
        if (err) *err = name + " requires a value.";
        return std::string();
      }
      return std::string(argv[++i]);
    };

    if (arg == "-h" || arg == "--help") {
      PrintHelp();
      std::exit(EXIT_SUCCESS);
    } else if (arg == "--input-format") {
      opts->input_format = ToLower(need_value(arg));
    } else if (arg == "--format") {
      opts->format = ToLower(need_value(arg));
    } else if (arg == "-o" || arg == "--output") {
      opts->output_filename = need_value(arg);
    } else if (arg == "--up-axis") {
      opts->up_axis = need_value(arg);
      std::transform(opts->up_axis.begin(), opts->up_axis.end(),
                     opts->up_axis.begin(), [](unsigned char c) {
                       return static_cast<char>(std::toupper(c));
                     });
    } else if (arg == "--dump-json") {
      opts->dump_json_filename = need_value(arg);
    } else if (arg == "--allow-missing") {
      opts->allow_missing = true;
    } else if (arg == "--tessellate-collision-shapes") {
      opts->tessellate_collision_shapes = true;
    } else if (arg == "-v" || arg == "--verbose") {
      opts->verbose = true;
    } else if (!HasPrefix(arg, "-") && opts->input_filename.empty()) {
      opts->input_filename = arg;
    } else {
      if (err) *err = "Unknown argument: " + arg;
      return false;
    }

    if (err && !err->empty()) return false;
  }

  const std::set<std::string> formats{"usda", "usdc", "usdz", "all"};
  if (!formats.count(opts->format)) {
    if (err) *err = "--format must be one of: usda, usdc, usdz, all.";
    return false;
  }
  const std::set<std::string> input_formats{"auto", "mjcf"};
  if (!input_formats.count(opts->input_format)) {
    if (err) *err = "--input-format must be one of: auto, mjcf.";
    return false;
  }
  if (opts->up_axis != "Z" && opts->up_axis != "Y") {
    if (err) *err = "--up-axis must be Z or Y.";
    return false;
  }
  if (opts->input_filename.empty()) {
    if (err) *err = "Input file is required.";
    return false;
  }
  return true;
}

bool ReadFile(const fs::path &filename, std::string *text, std::string *err) {
  std::ifstream ifs(filename, std::ios::binary);
  if (!ifs) {
    if (err) *err = "Failed to open: " + filename.string();
    return false;
  }
  std::ostringstream ss;
  ss << ifs.rdbuf();
  (*text) = ss.str();
  return true;
}

bool WriteFile(const fs::path &filename, const std::vector<uint8_t> &bytes,
               std::string *err) {
  std::ofstream ofs(filename, std::ios::binary);
  if (!ofs) {
    if (err) *err = "Failed to open output: " + filename.string();
    return false;
  }
  ofs.write(reinterpret_cast<const char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  return bool(ofs);
}

std::vector<double> ParseDoubles(const std::string &text) {
  std::vector<double> values;
  std::stringstream ss(text);
  double v = 0.0;
  while (ss >> v) values.push_back(v);
  return values;
}

std::array<double, 3> ParseDouble3(const std::string &text,
                                   std::array<double, 3> fallback) {
  const std::vector<double> values = ParseDoubles(text);
  if (values.size() >= 3) {
    return {{values[0], values[1], values[2]}};
  }
  return fallback;
}

double ParseDoubleAttr(const pugi::xml_node &node,
                       const std::string &name, double fallback) {
  if (!node || !node.attribute(name.c_str())) return fallback;
  char *end = nullptr;
  const std::string s = node.attribute(name.c_str()).as_string();
  const double v = std::strtod(s.c_str(), &end);
  return (end && end != s.c_str()) ? v : fallback;
}

bool HasAttr(const pugi::xml_node &node, const std::string &name) {
  return node && node.attribute(name.c_str());
}

std::string Attr(const pugi::xml_node &node, const std::string &name,
                 const std::string &fallback = std::string()) {
  if (!node) return fallback;
  return node.attribute(name.c_str()).as_string(fallback.c_str());
}

std::vector<pugi::xml_node> Children(
    const pugi::xml_node &node, const std::string &name) {
  if (!node) return {};
  std::vector<pugi::xml_node> out;
  for (pugi::xml_node child : node.children(name.c_str())) {
    out.push_back(child);
  }
  return out;
}

pugi::xml_node Child(const pugi::xml_node &node,
                                 const std::string &name) {
  if (!node) return pugi::xml_node();
  return node.child(name.c_str());
}

std::string StripMujocoRoot(const std::string &xml) {
  std::string out = xml;
  const size_t decl = out.find("<?xml");
  if (decl != std::string::npos) {
    const size_t end = out.find("?>", decl);
    if (end != std::string::npos) out.erase(decl, end + 2 - decl);
  }

  const size_t open = out.find("<mujoco");
  const size_t close = out.rfind("</mujoco>");
  if (open == std::string::npos || close == std::string::npos) return out;
  const size_t open_end = out.find('>', open);
  if (open_end == std::string::npos || open_end >= close) return out;
  return out.substr(open_end + 1, close - open_end - 1);
}

std::string AttributeFromTag(const std::string &tag, const std::string &name) {
  const std::string needle = name + "=";
  size_t pos = tag.find(needle);
  if (pos == std::string::npos) return std::string();
  pos += needle.size();
  if (pos >= tag.size()) return std::string();
  const char quote = tag[pos];
  if (quote != '"' && quote != '\'') return std::string();
  const size_t end = tag.find(quote, pos + 1);
  if (end == std::string::npos) return std::string();
  return tag.substr(pos + 1, end - pos - 1);
}

bool ExpandIncludes(const std::string &xml, const fs::path &base_dir,
                    std::set<fs::path> *seen, std::string *expanded,
                    std::string *err) {
  std::string out;
  size_t cursor = 0;
  while (true) {
    const size_t pos = xml.find("<include", cursor);
    if (pos == std::string::npos) {
      out += xml.substr(cursor);
      break;
    }
    out += xml.substr(cursor, pos - cursor);
    const size_t end = xml.find('>', pos);
    if (end == std::string::npos) {
      if (err) *err = "Malformed <include> element.";
      return false;
    }
    const std::string tag = xml.substr(pos, end - pos + 1);
    const std::string file = AttributeFromTag(tag, "file");
    if (!file.empty()) {
      const fs::path include_path = fs::weakly_canonical(base_dir / file);
      if (seen->count(include_path)) {
        if (err) *err = "Recursive include: " + include_path.string();
        return false;
      }
      std::string child;
      if (!ReadFile(include_path, &child, err)) return false;
      seen->insert(include_path);
      std::string child_expanded;
      if (!ExpandIncludes(StripMujocoRoot(child), include_path.parent_path(),
                          seen, &child_expanded, err)) {
        return false;
      }
      seen->erase(include_path);
      out += child_expanded;
    }
    cursor = end + 1;
  }
  (*expanded) = std::move(out);
  return true;
}

std::vector<double> PoseMatrix(const pugi::xml_node &node) {
  const std::array<double, 3> pos =
      ParseDouble3(node ? Attr(node, "pos") : "", {{0.0, 0.0, 0.0}});
  std::array<double, 4> q{{1.0, 0.0, 0.0, 0.0}};  // MuJoCo wxyz.
  const std::vector<double> qvals =
      ParseDoubles(node ? Attr(node, "quat") : "");
  if (qvals.size() >= 4) {
    q = {{qvals[0], qvals[1], qvals[2], qvals[3]}};
  }

  const double w = q[0];
  const double x = q[1];
  const double y = q[2];
  const double z = q[3];
  const double x2 = x + x;
  const double y2 = y + y;
  const double z2 = z + z;
  const double xx = x * x2;
  const double xy = x * y2;
  const double xz = x * z2;
  const double yy = y * y2;
  const double yz = y * z2;
  const double zz = z * z2;
  const double wx = w * x2;
  const double wy = w * y2;
  const double wz = w * z2;

  // Match the JS tester's Three.js Matrix4.elements -> USD flat array.
  return {
      1.0 - (yy + zz), xy + wz,         xz - wy,         0.0,
      xy - wz,         1.0 - (xx + zz), yz + wx,         0.0,
      xz + wy,         yz - wx,         1.0 - (xx + yy), 0.0,
      pos[0],          pos[1],          pos[2],          1.0};
}

std::vector<double> MultiplyMatrix(const std::vector<double> &a,
                                   const std::vector<double> &b) {
  std::vector<double> out(16, 0.0);
  for (size_t r = 0; r < 4; r++) {
    for (size_t c = 0; c < 4; c++) {
      for (size_t k = 0; k < 4; k++) {
        out[r * 4 + c] += a[r * 4 + k] * b[k * 4 + c];
      }
    }
  }
  return out;
}

std::vector<double> ScaleMatrix(const std::array<double, 3> &scale) {
  return {scale[0], 0.0,      0.0,      0.0, 0.0, scale[1], 0.0,      0.0,
          0.0,      0.0,      scale[2], 0.0, 0.0, 0.0,      0.0,      1.0};
}

void AppendTri(MeshData *mesh, const std::array<float, 3> &a,
               const std::array<float, 3> &b,
               const std::array<float, 3> &c) {
  const int32_t base = static_cast<int32_t>(mesh->positions.size() / 3);
  const std::array<std::array<float, 3>, 3> tri{{a, b, c}};
  for (size_t i = 0; i < tri.size(); i++) {
    const auto &v = tri[i];
    mesh->positions.push_back(v[0]);
    mesh->positions.push_back(v[1]);
    mesh->positions.push_back(v[2]);
    mesh->indices.push_back(base + static_cast<int32_t>(i));
  }
}

MeshData MakeBoxMesh(const std::array<double, 3> &half) {
  MeshData mesh;
  const float x = static_cast<float>(std::max(half[0], 1.0e-6));
  const float y = static_cast<float>(std::max(half[1], 1.0e-6));
  const float z = static_cast<float>(std::max(half[2], 1.0e-6));
  const std::array<std::array<float, 3>, 8> v{{
      {{-x, -y, -z}}, {{x, -y, -z}}, {{x, y, -z}}, {{-x, y, -z}},
      {{-x, -y, z}},  {{x, -y, z}},  {{x, y, z}},  {{-x, y, z}},
  }};
  const int faces[12][3] = {{0, 1, 2}, {0, 2, 3}, {4, 6, 5}, {4, 7, 6},
                            {0, 4, 5}, {0, 5, 1}, {1, 5, 6}, {1, 6, 2},
                            {2, 6, 7}, {2, 7, 3}, {3, 7, 4}, {3, 4, 0}};
  for (const auto &f : faces) AppendTri(&mesh, v[f[0]], v[f[1]], v[f[2]]);
  return mesh;
}

MeshData MakeSphereMesh(double radius, int slices = 16, int stacks = 8) {
  MeshData mesh;
  const float r = static_cast<float>(std::max(radius, 1.0e-6));
  std::vector<std::array<float, 3>> verts;
  for (int iy = 0; iy <= stacks; iy++) {
    const double v = double(iy) / double(stacks);
    const double phi = v * 3.14159265358979323846;
    for (int ix = 0; ix <= slices; ix++) {
      const double u = double(ix) / double(slices);
      const double theta = u * 2.0 * 3.14159265358979323846;
      verts.push_back({{static_cast<float>(r * std::sin(phi) * std::cos(theta)),
                        static_cast<float>(r * std::sin(phi) * std::sin(theta)),
                        static_cast<float>(r * std::cos(phi))}});
    }
  }
  for (int iy = 0; iy < stacks; iy++) {
    for (int ix = 0; ix < slices; ix++) {
      const int a = iy * (slices + 1) + ix;
      const int b = a + 1;
      const int c = a + slices + 1;
      const int d = c + 1;
      AppendTri(&mesh, verts[a], verts[c], verts[b]);
      AppendTri(&mesh, verts[b], verts[c], verts[d]);
    }
  }
  return mesh;
}

MeshData MakeCylinderMesh(double radius, double half_length, int slices = 20) {
  MeshData mesh;
  const float r = static_cast<float>(std::max(radius, 1.0e-6));
  const float h = static_cast<float>(std::max(half_length, 1.0e-6));
  const std::array<float, 3> top{{0.0f, 0.0f, h}};
  const std::array<float, 3> bottom{{0.0f, 0.0f, -h}};
  for (int i = 0; i < slices; i++) {
    const double a0 = 2.0 * 3.14159265358979323846 * double(i) / double(slices);
    const double a1 = 2.0 * 3.14159265358979323846 * double(i + 1) / double(slices);
    const std::array<float, 3> p0{{static_cast<float>(r * std::cos(a0)),
                                   static_cast<float>(r * std::sin(a0)), -h}};
    const std::array<float, 3> p1{{static_cast<float>(r * std::cos(a1)),
                                   static_cast<float>(r * std::sin(a1)), -h}};
    const std::array<float, 3> p2{{p0[0], p0[1], h}};
    const std::array<float, 3> p3{{p1[0], p1[1], h}};
    AppendTri(&mesh, p0, p2, p1);
    AppendTri(&mesh, p1, p2, p3);
    AppendTri(&mesh, bottom, p1, p0);
    AppendTri(&mesh, top, p2, p3);
  }
  return mesh;
}

float ReadFloatLE(const std::vector<uint8_t> &bytes, size_t offset) {
  float v = 0.0f;
  std::memcpy(&v, bytes.data() + offset, sizeof(float));
  return v;
}

bool LoadBinarySTL(const fs::path &filename, MeshData *mesh, std::string *err) {
  std::ifstream ifs(filename, std::ios::binary);
  if (!ifs) {
    if (err) *err = "Failed to open STL: " + filename.string();
    return false;
  }
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(ifs)),
                             std::istreambuf_iterator<char>());
  if (bytes.size() < 84) {
    if (err) *err = "STL is too small: " + filename.string();
    return false;
  }
  uint32_t tri_count = 0;
  std::memcpy(&tri_count, bytes.data() + 80, sizeof(uint32_t));
  const uint64_t expected = 84ull + uint64_t(tri_count) * 50ull;
  if (expected != bytes.size()) {
    if (err) *err = "Only binary STL with exact triangle count is supported: " + filename.string();
    return false;
  }

  mesh->positions.reserve(size_t(tri_count) * 9);
  mesh->normals.reserve(size_t(tri_count) * 9);
  mesh->indices.reserve(size_t(tri_count) * 3);

  for (uint32_t i = 0; i < tri_count; i++) {
    const size_t base = 84ull + size_t(i) * 50ull;
    const std::array<float, 3> n{{ReadFloatLE(bytes, base + 0),
                                  ReadFloatLE(bytes, base + 4),
                                  ReadFloatLE(bytes, base + 8)}};
    for (size_t v = 0; v < 3; v++) {
      const size_t vo = base + 12 + v * 12;
      mesh->positions.push_back(ReadFloatLE(bytes, vo + 0));
      mesh->positions.push_back(ReadFloatLE(bytes, vo + 4));
      mesh->positions.push_back(ReadFloatLE(bytes, vo + 8));
      mesh->normals.push_back(n[0]);
      mesh->normals.push_back(n[1]);
      mesh->normals.push_back(n[2]);
      mesh->indices.push_back(static_cast<int32_t>(i * 3 + v));
    }
  }
  return true;
}

bool LoadOBJ(const fs::path &filename, MeshData *mesh, std::string *err) {
  std::ifstream ifs(filename);
  if (!ifs) {
    if (err) *err = "Failed to open OBJ: " + filename.string();
    return false;
  }

  std::vector<std::array<float, 3>> vertices;
  std::string line;
  while (std::getline(ifs, line)) {
    std::stringstream ss(line);
    std::string tag;
    ss >> tag;
    if (tag == "v") {
      std::array<float, 3> v{{0.0f, 0.0f, 0.0f}};
      ss >> v[0] >> v[1] >> v[2];
      vertices.push_back(v);
    } else if (tag == "f") {
      std::vector<int> face;
      std::string tok;
      while (ss >> tok) {
        const size_t slash = tok.find('/');
        const int idx = std::stoi(slash == std::string::npos ? tok : tok.substr(0, slash));
        face.push_back(idx > 0 ? idx - 1 : int(vertices.size()) + idx);
      }
      for (size_t i = 1; i + 1 < face.size(); i++) {
        const int tri[3] = {face[0], face[i], face[i + 1]};
        for (int vi : tri) {
          if (vi < 0 || size_t(vi) >= vertices.size()) continue;
          const auto &v = vertices[size_t(vi)];
          mesh->positions.push_back(v[0]);
          mesh->positions.push_back(v[1]);
          mesh->positions.push_back(v[2]);
          mesh->indices.push_back(static_cast<int32_t>(mesh->indices.size()));
        }
      }
    }
  }
  return !mesh->positions.empty();
}

bool LoadMeshFile(const fs::path &filename, MeshData *mesh, std::string *err) {
  const std::string ext = ToLower(filename.extension().string());
  if (ext == ".stl") return LoadBinarySTL(filename, mesh, err);
  if (ext == ".obj") return LoadOBJ(filename, mesh, err);
  if (err) *err = "Unsupported native mesh extension: " + ext;
  return false;
}

nlohmann::json MeshPayloadToJson(const MeshPayload &payload) {
  if (payload.shape.is_object()) {
    return {
        {"name", payload.name},
        {"matrix", payload.matrix},
        {"shape", payload.shape}};
  }
  return {
      {"name", payload.name},
      {"matrix", payload.matrix},
      {"geometry",
       {{"positions", payload.mesh.positions},
        {"normals", payload.mesh.normals},
        {"indices", payload.mesh.indices}}}};
}

void AddGeomPhysicsAttrs(const pugi::xml_node &geom_node,
                         nlohmann::json *geom_json) {
  if (!geom_json) return;
  if (HasAttr(geom_node, "group")) {
    (*geom_json)["group"] = static_cast<int32_t>(
        ParseDoubleAttr(geom_node, "group", 3.0));
  }
  if (HasAttr(geom_node, "condim")) {
    (*geom_json)["condim"] = static_cast<int32_t>(
        ParseDoubleAttr(geom_node, "condim", 3.0));
  }
  if (HasAttr(geom_node, "margin")) {
    (*geom_json)["mjc"]["margin"] = ParseDoubleAttr(geom_node, "margin", 0.0);
  }
  if (HasAttr(geom_node, "solmix")) {
    (*geom_json)["mjc"]["solmix"] = ParseDoubleAttr(geom_node, "solmix", 1.0);
  }
}

std::map<std::string, MeshAsset> CollectMujocoAssets(
    const pugi::xml_node &root, const fs::path &base_dir) {
  std::map<std::string, MeshAsset> assets;
  const auto compiler = Child(root, "compiler");
  const fs::path mesh_dir =
      compiler ? base_dir / Attr(compiler, "meshdir") : base_dir;

  for (const auto &asset_node : Children(root, "asset")) {
    for (const auto &mesh_node : Children(asset_node, "mesh")) {
      const std::string file = Attr(mesh_node, "file");
      if (file.empty()) continue;
      std::string name = Attr(mesh_node, "name");
      if (name.empty()) name = fs::path(file).stem().string();
      MeshAsset asset;
      asset.path = mesh_dir / file;
      asset.scale =
          ParseDouble3(Attr(mesh_node, "scale"), {{1.0, 1.0, 1.0}});
      assets[name] = asset;
    }
  }
  return assets;
}

bool BuildGeomPayload(const pugi::xml_node &geom_node,
                      const std::map<std::string, MeshAsset> &assets,
                      const Options &opts, MeshPayload *payload,
                      bool *is_visual, std::string *err) {
  const std::string type =
      HasAttr(geom_node, "type") ? Attr(geom_node, "type") :
      HasAttr(geom_node, "mesh") ? "mesh" : "sphere";
  payload->name = Attr(geom_node, "name");
  if (payload->name.empty()) payload->name = Attr(geom_node, "mesh", "geom");
  payload->matrix = PoseMatrix(geom_node);

  const std::string klass = Attr(geom_node, "class");
  (*is_visual) = (klass == "visual") ||
                 (Attr(geom_node, "group") == "2") ||
                 (Attr(geom_node, "contype") == "0" &&
                  Attr(geom_node, "conaffinity") == "0");

  if (type == "mesh") {
    const std::string mesh_name = Attr(geom_node, "mesh");
    const auto it = assets.find(mesh_name);
    if (it == assets.end()) {
      if (err) *err = "Missing MJCF mesh asset: " + mesh_name;
      return false;
    }
    if (!LoadMeshFile(it->second.path, &payload->mesh, err)) return false;

    const std::array<double, 3> scale =
        ParseDouble3(Attr(geom_node, "scale"), it->second.scale);
    payload->matrix = MultiplyMatrix(payload->matrix, ScaleMatrix(scale));
  } else if (type == "box") {
    const auto half = ParseDouble3(Attr(geom_node, "size"),
                                   {{0.05, 0.05, 0.05}});
    if (!(*is_visual) && !opts.tessellate_collision_shapes) {
      payload->shape = {{"type", "box"}};
      payload->matrix = MultiplyMatrix(payload->matrix, ScaleMatrix(half));
    } else {
      payload->mesh = MakeBoxMesh(half);
    }
  } else if (type == "sphere") {
    const auto size = ParseDoubles(Attr(geom_node, "size"));
    const double radius = size.empty() ? 0.05 : size[0];
    if (!(*is_visual) && !opts.tessellate_collision_shapes) {
      payload->shape = {{"type", "sphere"}, {"radius", radius}};
    } else {
      payload->mesh = MakeSphereMesh(radius);
    }
  } else if (type == "ellipsoid") {
    const auto radii = ParseDouble3(Attr(geom_node, "size"),
                                    {{0.05, 0.05, 0.05}});
    if (!(*is_visual) && !opts.tessellate_collision_shapes) {
      payload->shape = {{"type", "sphere"}, {"radius", 1.0}};
      payload->matrix = MultiplyMatrix(payload->matrix, ScaleMatrix(radii));
    } else {
      payload->mesh = MakeSphereMesh(1.0);
      payload->matrix = MultiplyMatrix(payload->matrix, ScaleMatrix(radii));
    }
  } else if (type == "cylinder" || type == "capsule") {
    const auto size = ParseDoubles(Attr(geom_node, "size"));
    const double radius = size.empty() ? 0.05 : size[0];
    double half_length = size.size() >= 2 ? size[1] : radius;
    const auto fromto = ParseDoubles(Attr(geom_node, "fromto"));
    if (fromto.size() >= 6) {
      const double dx = fromto[3] - fromto[0];
      const double dy = fromto[4] - fromto[1];
      const double dz = fromto[5] - fromto[2];
      half_length = 0.5 * std::sqrt(dx * dx + dy * dy + dz * dz);
      payload->matrix[12] = 0.5 * (fromto[0] + fromto[3]);
      payload->matrix[13] = 0.5 * (fromto[1] + fromto[4]);
      payload->matrix[14] = 0.5 * (fromto[2] + fromto[5]);
    }
    if (!(*is_visual) && !opts.tessellate_collision_shapes) {
      payload->shape = {{"type", type},
                        {"radius", radius},
                        {"height", half_length * 2.0},
                        {"axis", "Z"}};
    } else {
      payload->mesh = MakeCylinderMesh(radius, half_length);
    }
  } else if (type == "plane") {
    const auto size = ParseDoubles(Attr(geom_node, "size"));
    const double sx = size.size() >= 1 && size[0] > 0.0 ? size[0] : 1.0;
    const double sy = size.size() >= 2 && size[1] > 0.0 ? size[1] : 1.0;
    const double sz = size.size() >= 3 && size[2] > 0.0 ? size[2] : 0.001;
    if (!(*is_visual) && !opts.tessellate_collision_shapes) {
      payload->shape = {{"type", "plane"},
                        {"width", sx * 2.0},
                        {"length", sy * 2.0},
                        {"axis", "Z"}};
    } else {
      payload->mesh = MakeBoxMesh({{sx, sy, sz}});
    }
  } else {
    if (err) *err = "Unsupported native MJCF geom type: " + type;
    return false;
  }
  return true;
}

std::string AxisToken(const std::array<double, 3> &axis) {
  const double ax = std::abs(axis[0]);
  const double ay = std::abs(axis[1]);
  const double az = std::abs(axis[2]);
  if (ay >= ax && ay >= az) return "Y";
  if (az >= ax && az >= ay) return "Z";
  return "X";
}

nlohmann::json InertialToJson(const pugi::xml_node &body_node) {
  nlohmann::json inertial = nlohmann::json::object();
  const auto inertial_node = Child(body_node, "inertial");
  if (!inertial_node) return inertial;
  inertial["mass"] = ParseDoubleAttr(inertial_node, "mass", 0.0);
  const auto com =
      ParseDouble3(Attr(inertial_node, "pos"), {{0.0, 0.0, 0.0}});
  inertial["centerOfMass"] = {com[0], com[1], com[2]};
  const std::vector<double> full =
      ParseDoubles(Attr(inertial_node, "fullinertia"));
  if (full.size() >= 3) {
    inertial["diagonalInertia"] = {full[0], full[1], full[2]};
  }
  return inertial;
}

void AddJointJson(const pugi::xml_node &body_node,
                  const std::string &parent_name, const std::string &child_name,
                  nlohmann::json *joints) {
  const auto joint_node = Child(body_node, "joint");
  nlohmann::json joint = nlohmann::json::object();
  joint["name"] = joint_node ? Attr(joint_node, "name", parent_name + "_to_" + child_name)
                             : parent_name + "_to_" + child_name + "_fixed";
  const std::string mj_type = joint_node ? Attr(joint_node, "type", "hinge") : "fixed";
  joint["type"] = (mj_type == "hinge") ? "revolute" :
                  (mj_type == "slide") ? "prismatic" : "fixed";
  joint["parent"] = parent_name;
  joint["child"] = child_name;
  const auto axis = ParseDouble3(joint_node ? Attr(joint_node, "axis") : "",
                                 {{0.0, 0.0, 1.0}});
  joint["axis"] = {axis[0], axis[1], axis[2]};
  joint["axisToken"] = AxisToken(axis);
  const auto origin = ParseDouble3(Attr(body_node, "pos"), {{0.0, 0.0, 0.0}});
  joint["origin"] = {origin[0], origin[1], origin[2]};
  joint["originMatrix"] = PoseMatrix(body_node);

  if (joint_node) {
    const std::vector<double> range = ParseDoubles(Attr(joint_node, "range"));
    if (range.size() >= 2) {
      joint["limit"] = {{"lower", range[0]}, {"upper", range[1]}};
    }
    nlohmann::json dynamics = nlohmann::json::object();
    if (HasAttr(joint_node, "damping")) {
      dynamics["damping"] = ParseDoubleAttr(joint_node, "damping", 0.0);
    }
    if (HasAttr(joint_node, "frictionloss")) {
      dynamics["friction"] = ParseDoubleAttr(joint_node, "frictionloss", 0.0);
    }
    // MJCF <joint stiffness=> and <joint armature=> — propagate so the
    // USD-side converter can author physxLimit:*:stiffness and
    // physxJoint:armature alongside the canonical mjc:* fallbacks.
    if (HasAttr(joint_node, "stiffness")) {
      dynamics["stiffness"] = ParseDoubleAttr(joint_node, "stiffness", 0.0);
    }
    if (HasAttr(joint_node, "armature")) {
      dynamics["armature"] = ParseDoubleAttr(joint_node, "armature", 0.0);
    }
    joint["dynamics"] = dynamics;
    // MJCF <joint ref="..."> is the rest-angle (degrees for hinge,
    // meters for slide). We forward it as a generic `initPosition`
    // (radians for hinge, since URDF uses radians internally) so the
    // C++ converter can emit state:{angular,linear}:physics:position.
    if (HasAttr(joint_node, "ref")) {
      double ref = ParseDoubleAttr(joint_node, "ref", 0.0);
      if (mj_type == "hinge") {
        // MJCF degrees → radians for our pipeline.
        // Use a literal (π/180) instead of M_PI: M_PI is a non-standard
        // GNU extension and is not defined by MSVC's <cmath> unless
        // _USE_MATH_DEFINES is set before include.
        ref *= 0.017453292519943295;
      }
      joint["initPosition"] = ref;
    }
  }

  joints->push_back(joint);
}

void AddMujocoActuatorsJson(const pugi::xml_node &root,
                            nlohmann::json *actuators, Stats *stats) {
  if (!actuators) return;
  const auto actuator_root = Child(root, "actuator");
  if (!actuator_root) return;

  for (const auto &act_node : actuator_root.children()) {
    const std::string joint = Attr(act_node, "joint");
    if (joint.empty()) {
      continue;
    }
    const std::string type = act_node.name();
    nlohmann::json act = nlohmann::json::object();
    act["name"] = Attr(act_node, "name", type + "_" + joint);
    act["joint"] = joint;
    act["control"] = "pd";

    if (HasAttr(act_node, "kp")) {
      act["kp"] = ParseDoubleAttr(act_node, "kp", 0.0);
    } else {
      const auto gain = ParseDoubles(Attr(act_node, "gainprm"));
      if (!gain.empty()) {
        act["kp"] = gain[0];
      }
    }
    if (HasAttr(act_node, "kv")) {
      act["kd"] = ParseDoubleAttr(act_node, "kv", 0.0);
    } else {
      const auto bias = ParseDoubles(Attr(act_node, "biasprm"));
      if (bias.size() >= 3) {
        act["kd"] = std::abs(bias[2]);
      }
    }
    const auto force_range = ParseDoubles(Attr(act_node, "forcerange"));
    if (force_range.size() >= 2) {
      act["maxEffort"] = std::max(std::abs(force_range[0]),
                                  std::abs(force_range[1]));
    }
    const auto ctrl_range = ParseDoubles(Attr(act_node, "ctrlrange"));
    if (type == "motor" && ctrl_range.size() >= 2) {
      act["constEffort"] = std::max(std::abs(ctrl_range[0]),
                                    std::abs(ctrl_range[1]));
    }
    if (HasAttr(act_node, "delay")) {
      act["delaySteps"] = static_cast<int32_t>(
          ParseDoubleAttr(act_node, "delay", 1.0));
    }
    actuators->push_back(std::move(act));
    if (stats) stats->actuators++;
  }
}

bool VisitMujocoBody(const pugi::xml_node &body_node,
                     const std::string &parent_name,
                     const std::map<std::string, MeshAsset> &assets,
                     const Options &opts, nlohmann::json *links,
                     nlohmann::json *joints, Stats *stats,
                     std::string *err) {
  const std::string body_name =
      Attr(body_node, "name", "body_" + std::to_string(stats->links));
  nlohmann::json link = {
      {"name", body_name},
      {"inertial", InertialToJson(body_node)},
      {"visuals", nlohmann::json::array()},
      {"collisions", nlohmann::json::array()}};

  for (const auto &geom_node : Children(body_node, "geom")) {
    MeshPayload payload;
    bool visual = true;
    if (!BuildGeomPayload(geom_node, assets, opts, &payload, &visual, err)) {
      if (opts.allow_missing) {
        std::cerr << "WARN: " << (err ? *err : "mesh skipped") << "\n";
        if (err) err->clear();
        continue;
      }
      return false;
    }
    if (visual) {
      nlohmann::json visual_json = MeshPayloadToJson(payload);
      AddGeomPhysicsAttrs(geom_node, &visual_json);
      link["visuals"].push_back(std::move(visual_json));
      stats->visuals++;
    } else {
      nlohmann::json col = MeshPayloadToJson(payload);
      AddGeomPhysicsAttrs(geom_node, &col);
      // Default approximation `convexHull` matches
      // `src/tydra/urdf-to-usd.cc::AddCollisionAPIs` and the
      // mujoco-usd-converter convention (one Mesh per geom +
      // `UsdPhysicsMeshCollisionAPI` with hull approximation). Authors
      // who need triangle-soup contact can override per-geom in JSON.
      col["approximation"] = "convexHull";
      link["collisions"].push_back(std::move(col));
      stats->collisions++;
    }
  }

  links->push_back(std::move(link));
  stats->links++;
  if (!parent_name.empty()) {
    AddJointJson(body_node, parent_name, body_name, joints);
    stats->joints++;
  }

  for (const auto &child_body : Children(body_node, "body")) {
    if (!VisitMujocoBody(child_body, body_name, assets, opts, links, joints,
                         stats, err)) {
      return false;
    }
  }
  return true;
}

bool BuildMujocoPayload(const std::string &xml, const fs::path &input_filename,
                        const Options &opts, nlohmann::json *payload,
                        Stats *stats, std::string *err) {
  std::set<fs::path> seen;
  std::string expanded;
  if (!ExpandIncludes(xml, input_filename.parent_path(), &seen, &expanded, err)) {
    return false;
  }

  pugi::xml_document doc;
  const pugi::xml_parse_result parse_result = doc.load_string(expanded.c_str());
  if (!parse_result) {
    if (err) *err = std::string("MJCF XML parse failed: ") + parse_result.description();
    return false;
  }
  const auto root = doc.child("mujoco");
  if (!root) {
    if (err) *err = "Expected <mujoco> root.";
    return false;
  }

  const auto assets = CollectMujocoAssets(root, input_filename.parent_path());
  const auto worldbody = Child(root, "worldbody");
  if (!worldbody) {
    if (err) *err = "MJCF has no <worldbody>.";
    return false;
  }

  nlohmann::json links = nlohmann::json::array();
  nlohmann::json joints = nlohmann::json::array();
  nlohmann::json actuators = nlohmann::json::array();
  for (const auto &body : Children(worldbody, "body")) {
    if (!VisitMujocoBody(body, "", assets, opts, &links, &joints, stats, err)) {
      return false;
    }
  }
  AddMujocoActuatorsJson(root, &actuators, stats);

  (*payload) = {
      {"name", Attr(root, "model", input_filename.stem().string())},
      {"upAxis", opts.up_axis},
      {"gravity", opts.up_axis == "Z" ? nlohmann::json::array({0, 0, -1})
                                       : nlohmann::json::array({0, -1, 0})},
      {"links", std::move(links)},
      {"joints", std::move(joints)},
      {"actuators", std::move(actuators)}};
  const auto option = Child(root, "option");
  if (option && HasAttr(option, "timestep")) {
    (*payload)["timestep"] = ParseDoubleAttr(option, "timestep", 0.0);
  }
  return true;
}

fs::path OutputPath(const Options &opts, const std::string &format) {
  if (!opts.output_filename.empty()) {
    fs::path out(opts.output_filename);
    if (opts.format == "all") {
      out.replace_extension("." + format);
      return out;
    }
    if (out.extension().empty()) out.replace_extension("." + format);
    return out;
  }
  fs::path out(opts.input_filename);
  out.replace_extension("." + format);
  return out;
}

bool SaveStage(const tinyusdz::Stage &stage, const fs::path &filename,
               const std::string &format, std::string *err) {
  std::string warn;
  bool ok = false;
  if (format == "usda") {
    ok = tinyusdz::usda::SaveAsUSDA(filename.string(), stage, &warn, err);
  } else if (format == "usdc") {
    ok = tinyusdz::usdc::SaveAsUSDCToFile(filename.string(), stage, &warn, err);
  } else if (format == "usdz") {
    const std::map<std::string, std::vector<uint8_t>> assets;
    ok = tinyusdz::SaveAsUSDZToFile(filename.string(), stage, assets, &warn, err);
  }
  if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";
  return ok;
}

}  // namespace

int main(int argc, char **argv) {
  Options opts;
  std::string err;
  if (!ParseArgs(argc, argv, &opts, &err)) {
    std::cerr << "urdf-to-usd: " << err << "\n";
    PrintHelp();
    return EXIT_FAILURE;
  }

  const fs::path input_path = fs::absolute(opts.input_filename);
  std::string xml;
  if (!ReadFile(input_path, &xml, &err)) {
    std::cerr << "urdf-to-usd: " << err << "\n";
    return EXIT_FAILURE;
  }

  const bool is_mjcf = opts.input_format == "mjcf" ||
                       (opts.input_format == "auto" &&
                        xml.find("<mujoco") != std::string::npos);
  if (!is_mjcf) {
    std::cerr << "urdf-to-usd: native example currently supports MJCF XML input.\n";
    return EXIT_FAILURE;
  }

  nlohmann::json payload;
  Stats stats;
  if (!BuildMujocoPayload(xml, input_path, opts, &payload, &stats, &err)) {
    std::cerr << "urdf-to-usd: " << err << "\n";
    return EXIT_FAILURE;
  }

  if (!opts.dump_json_filename.empty()) {
    std::ofstream ofs(opts.dump_json_filename);
    ofs << payload.dump(2) << "\n";
  }

  tinyusdz::Stage stage;
  std::string warn;
  if (!tinyusdz::tydra::ConvertURDFJsonToUSDStage(payload.dump(), &stage, &warn,
                                                  &err)) {
    std::cerr << "urdf-to-usd: " << err << "\n";
    return EXIT_FAILURE;
  }
  if (!warn.empty()) std::cerr << "WARN: " << warn << "\n";

  const std::vector<std::string> formats =
      opts.format == "all" ? std::vector<std::string>{"usda", "usdc", "usdz"}
                           : std::vector<std::string>{opts.format};
  for (const std::string &format : formats) {
    const fs::path out = OutputPath(opts, format);
    if (!SaveStage(stage, out, format, &err)) {
      std::cerr << "urdf-to-usd: failed to write " << out << ": " << err << "\n";
      return EXIT_FAILURE;
    }
    std::cout << "Wrote " << out << "\n";
  }

  std::cout << "Converted " << payload.value("name", std::string("scene")) << ": "
            << stats.links << " links, " << stats.joints << " joints, "
            << stats.visuals << " visual meshes, " << stats.collisions
            << " collisions, " << stats.actuators << " Newton actuators.\n";
  return EXIT_SUCCESS;
}
