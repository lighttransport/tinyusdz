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
#include "str-util.hh"

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
  std::array<double, 3> refpos{{0.0, 0.0, 0.0}};   // <mesh refpos="...">
  std::array<double, 4> refquat{{1.0, 0.0, 0.0, 0.0}};  // <mesh refquat="..."> wxyz
  bool has_refpos = false;
  bool has_refquat = false;
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
  size_t tendons{0};
  size_t equalities{0};
  size_t contact_excludes{0};
  size_t sites{0};
  size_t keyframes{0};
};

// Resolved attribute map (attr name -> value) for MuJoCo <default> classes.
using AttrMap = std::map<std::string, std::string>;

struct Defaults {
  std::map<std::string, AttrMap> geom;   // class name -> merged geom attrs
  std::map<std::string, AttrMap> joint;  // class name -> merged joint attrs
  AttrMap root_geom;                      // unnamed (top-level) default
  AttrMap root_joint;
};

struct Context {
  // <compiler angle="...">: degrees by default; radians otherwise.
  double angle_to_rad = 0.017453292519943295;  // pi/180
  std::string eulerseq = "xyz";                 // <compiler eulerseq="...">
  Defaults defaults;
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

// Quaternions are stored MuJoCo-style: {w, x, y, z}.
using Quat = std::array<double, 4>;

Quat QuatNormalize(const Quat &q) {
  const double n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
  if (n < 1.0e-12) return {{1.0, 0.0, 0.0, 0.0}};
  return {{q[0] / n, q[1] / n, q[2] / n, q[3] / n}};
}

// Hamilton product a * b ({w,x,y,z}).
Quat QuatMul(const Quat &a, const Quat &b) {
  return {{a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3],
           a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2],
           a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1],
           a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0]}};
}

Quat QuatFromAxisAngle(const std::array<double, 3> &axis, double angle) {
  const double len = std::sqrt(axis[0] * axis[0] + axis[1] * axis[1] +
                               axis[2] * axis[2]);
  if (len < 1.0e-12) return {{1.0, 0.0, 0.0, 0.0}};
  const double s = std::sin(angle * 0.5) / len;
  return {{std::cos(angle * 0.5), axis[0] * s, axis[1] * s, axis[2] * s}};
}

// Rotation that maps unit vector `from` onto unit vector `to`.
Quat QuatFromTwoVecs(const std::array<double, 3> &from,
                     const std::array<double, 3> &to) {
  auto norm = [](std::array<double, 3> v) -> std::array<double, 3> {
    const double n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (n < 1.0e-12) return {{0.0, 0.0, 1.0}};
    return {{v[0] / n, v[1] / n, v[2] / n}};
  };
  const std::array<double, 3> a = norm(from);
  const std::array<double, 3> b = norm(to);
  const double dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
  if (dot > 0.999999) return {{1.0, 0.0, 0.0, 0.0}};
  if (dot < -0.999999) {
    // Antiparallel: rotate pi about any axis orthogonal to `a`.
    const std::array<double, 3> ortho =
        std::abs(a[0]) < 0.9 ? std::array<double, 3>{{1.0, 0.0, 0.0}}
                             : std::array<double, 3>{{0.0, 1.0, 0.0}};
    const std::array<double, 3> axis{{a[1] * ortho[2] - a[2] * ortho[1],
                                      a[2] * ortho[0] - a[0] * ortho[2],
                                      a[0] * ortho[1] - a[1] * ortho[0]}};
    return QuatFromAxisAngle(axis, 3.14159265358979323846);
  }
  const std::array<double, 3> axis{{a[1] * b[2] - a[2] * b[1],
                                    a[2] * b[0] - a[0] * b[2],
                                    a[0] * b[1] - a[1] * b[0]}};
  return QuatNormalize({{1.0 + dot, axis[0], axis[1], axis[2]}});
}

// Quaternion from a rotation matrix given as its three column basis vectors.
Quat QuatFromBasis(const std::array<double, 3> &cx,
                   const std::array<double, 3> &cy,
                   const std::array<double, 3> &cz) {
  const double m00 = cx[0], m10 = cx[1], m20 = cx[2];
  const double m01 = cy[0], m11 = cy[1], m21 = cy[2];
  const double m02 = cz[0], m12 = cz[1], m22 = cz[2];
  const double trace = m00 + m11 + m22;
  Quat q{{1.0, 0.0, 0.0, 0.0}};
  if (trace > 0.0) {
    const double s = 0.5 / std::sqrt(trace + 1.0);
    q = {{0.25 / s, (m21 - m12) * s, (m02 - m20) * s, (m10 - m01) * s}};
  } else if (m00 > m11 && m00 > m22) {
    const double s = 2.0 * std::sqrt(1.0 + m00 - m11 - m22);
    q = {{(m21 - m12) / s, 0.25 * s, (m01 + m10) / s, (m02 + m20) / s}};
  } else if (m11 > m22) {
    const double s = 2.0 * std::sqrt(1.0 + m11 - m00 - m22);
    q = {{(m02 - m20) / s, (m01 + m10) / s, 0.25 * s, (m12 + m21) / s}};
  } else {
    const double s = 2.0 * std::sqrt(1.0 + m22 - m00 - m11);
    q = {{(m10 - m01) / s, (m02 + m20) / s, (m12 + m21) / s, 0.25 * s}};
  }
  return QuatNormalize(q);
}

// Resolve a MuJoCo orientation specifier (quat / axisangle / euler / xyaxes /
// zaxis) into a {w,x,y,z} quaternion, honoring <compiler angle/eulerseq>.
Quat OrientationQuat(const std::string &quat_str,
                     const std::string &axisangle_str,
                     const std::string &euler_str,
                     const std::string &xyaxes_str,
                     const std::string &zaxis_str, const Context &ctx) {
  const std::vector<double> q = ParseDoubles(quat_str);
  if (q.size() >= 4) return QuatNormalize({{q[0], q[1], q[2], q[3]}});

  const std::vector<double> aa = ParseDoubles(axisangle_str);
  if (aa.size() >= 4) {
    return QuatFromAxisAngle({{aa[0], aa[1], aa[2]}}, aa[3] * ctx.angle_to_rad);
  }

  const std::vector<double> xy = ParseDoubles(xyaxes_str);
  if (xy.size() >= 6) {
    auto norm = [](std::array<double, 3> v) -> std::array<double, 3> {
      const double n = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
      if (n < 1.0e-12) return {{1.0, 0.0, 0.0}};
      return {{v[0] / n, v[1] / n, v[2] / n}};
    };
    const std::array<double, 3> x = norm({{xy[0], xy[1], xy[2]}});
    std::array<double, 3> y{{xy[3], xy[4], xy[5]}};
    const double d = x[0] * y[0] + x[1] * y[1] + x[2] * y[2];
    y = norm({{y[0] - d * x[0], y[1] - d * x[1], y[2] - d * x[2]}});
    const std::array<double, 3> z{{x[1] * y[2] - x[2] * y[1],
                                   x[2] * y[0] - x[0] * y[2],
                                   x[0] * y[1] - x[1] * y[0]}};
    return QuatFromBasis(x, y, z);
  }

  const std::vector<double> za = ParseDoubles(zaxis_str);
  if (za.size() >= 3) {
    return QuatFromTwoVecs({{0.0, 0.0, 1.0}}, {{za[0], za[1], za[2]}});
  }

  const std::vector<double> e = ParseDoubles(euler_str);
  if (e.size() >= 3) {
    Quat out{{1.0, 0.0, 0.0, 0.0}};
    for (size_t i = 0; i < 3 && i < ctx.eulerseq.size(); i++) {
      const char c = ctx.eulerseq[i];
      const char lower =
          static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      std::array<double, 3> axis{{0.0, 0.0, 0.0}};
      if (lower == 'x') axis = {{1.0, 0.0, 0.0}};
      else if (lower == 'y') axis = {{0.0, 1.0, 0.0}};
      else if (lower == 'z') axis = {{0.0, 0.0, 1.0}};
      else continue;
      const Quat qi = QuatFromAxisAngle(axis, e[i] * ctx.angle_to_rad);
      // Lowercase = intrinsic (post-multiply); uppercase = extrinsic (pre).
      out = std::islower(static_cast<unsigned char>(c)) ? QuatMul(out, qi)
                                                        : QuatMul(qi, out);
    }
    return QuatNormalize(out);
  }

  return {{1.0, 0.0, 0.0, 0.0}};
}

// Three.js Matrix4.elements (column-major) flat array from pos + quat.
std::vector<double> MatrixFromPosQuat(const std::array<double, 3> &pos,
                                      const Quat &q) {
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
  return {
      1.0 - (yy + zz), xy + wz,         xz - wy,         0.0,
      xy - wz,         1.0 - (xx + zz), yz + wx,         0.0,
      xz + wy,         yz - wx,         1.0 - (xx + yy), 0.0,
      pos[0],          pos[1],          pos[2],          1.0};
}

// Pose matrix for a body/geom node, reading any MuJoCo orientation specifier
// directly off the node (orientation is authored on the element, not in
// <default> classes).
std::vector<double> PoseMatrix(const pugi::xml_node &node, const Context &ctx) {
  const std::array<double, 3> pos =
      ParseDouble3(node ? Attr(node, "pos") : "", {{0.0, 0.0, 0.0}});
  const Quat q =
      node ? OrientationQuat(Attr(node, "quat"), Attr(node, "axisangle"),
                             Attr(node, "euler"), Attr(node, "xyaxes"),
                             Attr(node, "zaxis"), ctx)
           : Quat{{1.0, 0.0, 0.0, 0.0}};
  return MatrixFromPosQuat(pos, q);
}

// Column-major (Three.js Matrix4.elements) product a * b: index = col*4 + row.
// As a transform this applies `b` first, then `a` (a*(b*v)) -- so the rightmost
// argument is the innermost/local transform, matching Three.js Matrix4.multiply.
std::vector<double> MultiplyMatrix(const std::vector<double> &a,
                                   const std::vector<double> &b) {
  std::vector<double> out(16, 0.0);
  for (size_t c = 0; c < 4; c++) {
    for (size_t r = 0; r < 4; r++) {
      double s = 0.0;
      for (size_t k = 0; k < 4; k++) {
        s += a[k * 4 + r] * b[c * 4 + k];
      }
      out[c * 4 + r] = s;
    }
  }
  return out;
}

// 4x4 identity in the column-major flat layout used throughout.
std::vector<double> IdentityMatrix() {
  return {1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
          0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0};
}

std::vector<double> ScaleMatrix(const std::array<double, 3> &scale) {
  return {scale[0], 0.0,      0.0,      0.0, 0.0, scale[1], 0.0,      0.0,
          0.0,      0.0,      scale[2], 0.0, 0.0, 0.0,      0.0,      1.0};
}

std::vector<double> TranslationMatrix(const std::array<double, 3> &t) {
  return {1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
          0.0, 0.0, 1.0, 0.0, t[0], t[1], t[2], 1.0};
}

// Mesh frame transform from MJCF <mesh refpos/refquat>: vertices are first
// translated by -refpos, then rotated by conjugate(refquat). Returned as
// R(conj(refquat)) * T(-refpos) so it can left-compose with the geom matrix.
std::vector<double> MeshRefMatrix(const MeshAsset &asset) {
  std::vector<double> m = {1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0,
                           0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0};
  if (asset.has_refquat) {
    const Quat conj{{asset.refquat[0], -asset.refquat[1], -asset.refquat[2],
                     -asset.refquat[3]}};
    m = MatrixFromPosQuat({{0.0, 0.0, 0.0}}, QuatNormalize(conj));
  }
  if (asset.has_refpos) {
    m = MultiplyMatrix(m, TranslationMatrix({{-asset.refpos[0], -asset.refpos[1],
                                              -asset.refpos[2]}}));
  }
  return m;
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
        {
          nonstd::optional<int> idx_opt = tinyusdz::atoi(slash == std::string::npos ? tok : tok.substr(0, slash));
          if (!idx_opt.has_value()) {
            std::cerr << "Invalid face vertex index: " << tok << "\n";
            return false;
          }
          int idx_val = idx_opt.value();
          face.push_back(idx_val > 0 ? idx_val - 1 : static_cast<int>(vertices.size()) + idx_val);
        }
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

// --- MJCF <default> class resolution ---------------------------------------
//
// MuJoCo geoms/joints inherit attributes from a <default> class tree. The
// applicable class is the element's own `class`, else the enclosing body's
// `childclass`, else the unnamed/"main" default. Classes inherit from their
// parent <default>. We resolve an element's effective value as: own attribute
// wins, otherwise the resolved class default, otherwise a fallback.

std::string Eff(const pugi::xml_node &node, const AttrMap &cls,
                const std::string &name, const std::string &fallback = "") {
  if (node && node.attribute(name.c_str())) {
    return node.attribute(name.c_str()).as_string();
  }
  const auto it = cls.find(name);
  return it != cls.end() ? it->second : fallback;
}

bool HasEff(const pugi::xml_node &node, const AttrMap &cls,
            const std::string &name) {
  return (node && node.attribute(name.c_str())) || cls.count(name) > 0;
}

double EffDouble(const pugi::xml_node &node, const AttrMap &cls,
                 const std::string &name, double fallback) {
  if (!HasEff(node, cls, name)) return fallback;
  const std::string s = Eff(node, cls, name);
  char *end = nullptr;
  const double v = std::strtod(s.c_str(), &end);
  return (end && end != s.c_str()) ? v : fallback;
}

void WalkDefault(const pugi::xml_node &node, const AttrMap &parent_geom,
                 const AttrMap &parent_joint, Defaults *d) {
  AttrMap g = parent_geom;
  AttrMap j = parent_joint;
  if (const auto gn = Child(node, "geom")) {
    for (const auto &a : gn.attributes()) g[a.name()] = a.value();
  }
  if (const auto jn = Child(node, "joint")) {
    for (const auto &a : jn.attributes()) j[a.name()] = a.value();
  }
  const std::string cls = Attr(node, "class");
  if (cls.empty() || cls == "main") {
    d->root_geom = g;
    d->root_joint = j;
  }
  if (!cls.empty()) {
    d->geom[cls] = g;
    d->joint[cls] = j;
  }
  for (const auto &child : Children(node, "default")) {
    WalkDefault(child, g, j, d);
  }
}

Defaults ParseDefaults(const pugi::xml_node &root) {
  Defaults d;
  for (const auto &def : Children(root, "default")) {
    WalkDefault(def, AttrMap{}, AttrMap{}, &d);
  }
  return d;
}

const AttrMap &ResolveGeomClass(const pugi::xml_node &geom_node,
                                const Defaults &defaults,
                                const std::string &childclass) {
  std::string cls = Attr(geom_node, "class");
  if (cls.empty()) cls = childclass;
  if (!cls.empty()) {
    const auto it = defaults.geom.find(cls);
    if (it != defaults.geom.end()) return it->second;
  }
  return defaults.root_geom;
}

const AttrMap &ResolveJointClass(const pugi::xml_node &joint_node,
                                 const Defaults &defaults,
                                 const std::string &childclass) {
  std::string cls = joint_node ? Attr(joint_node, "class") : std::string();
  if (cls.empty()) cls = childclass;
  if (!cls.empty()) {
    const auto it = defaults.joint.find(cls);
    if (it != defaults.joint.end()) return it->second;
  }
  return defaults.root_joint;
}

void AddGeomPhysicsAttrs(const pugi::xml_node &geom_node, const AttrMap &cls,
                         nlohmann::json *geom_json) {
  if (!geom_json) return;
  if (HasEff(geom_node, cls, "group")) {
    (*geom_json)["group"] =
        static_cast<int32_t>(EffDouble(geom_node, cls, "group", 3.0));
  }
  if (HasEff(geom_node, cls, "condim")) {
    (*geom_json)["condim"] =
        static_cast<int32_t>(EffDouble(geom_node, cls, "condim", 3.0));
  }
  if (HasEff(geom_node, cls, "margin")) {
    (*geom_json)["mjc"]["margin"] = EffDouble(geom_node, cls, "margin", 0.0);
  }
  if (HasEff(geom_node, cls, "solmix")) {
    (*geom_json)["mjc"]["solmix"] = EffDouble(geom_node, cls, "solmix", 1.0);
  }
}

// Basename -> path index of mesh files under `root_dir`. Used as a last-resort
// fallback when a <mesh file="..."> path does not resolve against meshdir --
// e.g. MuJoCo scenes whose assets are referenced relative to an included
// file's directory (the include flattening here drops that provenance).
// Names that occur in more than one location are dropped to avoid ambiguity.
std::map<std::string, fs::path> BuildMeshIndex(const fs::path &root_dir) {
  std::map<std::string, fs::path> index;
  std::set<std::string> ambiguous;
  std::error_code ec;
  for (fs::recursive_directory_iterator it(root_dir, ec), end;
       !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    const std::string ext = ToLower(it->path().extension().string());
    if (ext != ".stl" && ext != ".obj" && ext != ".msh") continue;
    const std::string key = ToLower(it->path().filename().string());
    if (index.count(key)) {
      ambiguous.insert(key);
    } else {
      index[key] = it->path();
    }
  }
  for (const auto &a : ambiguous) index.erase(a);
  return index;
}

fs::path ResolveMeshPath(const fs::path &base_dir, const fs::path &mesh_dir,
                         const std::string &file,
                         const std::map<std::string, fs::path> &index) {
  std::error_code ec;
  const std::array<fs::path, 3> candidates{{mesh_dir / file,
                                            base_dir / "assets" / file,
                                            base_dir / file}};
  for (const auto &c : candidates) {
    if (fs::exists(c, ec)) return c;
  }
  const auto it = index.find(ToLower(fs::path(file).filename().string()));
  if (it != index.end()) return it->second;
  return mesh_dir / file;  // primary candidate; errors later with a clear msg
}

std::map<std::string, MeshAsset> CollectMujocoAssets(
    const pugi::xml_node &root, const fs::path &base_dir) {
  std::map<std::string, MeshAsset> assets;
  const auto compiler = Child(root, "compiler");
  // MuJoCo <compiler meshdir> takes precedence; assetdir is the shared
  // default for meshdir/texturedir when meshdir is absent.
  std::string dir_attr = compiler ? Attr(compiler, "meshdir") : std::string();
  if (dir_attr.empty() && compiler) dir_attr = Attr(compiler, "assetdir");
  const fs::path mesh_dir = dir_attr.empty() ? base_dir : base_dir / dir_attr;
  const std::map<std::string, fs::path> index = BuildMeshIndex(base_dir);

  for (const auto &asset_node : Children(root, "asset")) {
    for (const auto &mesh_node : Children(asset_node, "mesh")) {
      const std::string file = Attr(mesh_node, "file");
      if (file.empty()) continue;
      std::string name = Attr(mesh_node, "name");
      if (name.empty()) name = fs::path(file).stem().string();
      MeshAsset asset;
      asset.path = ResolveMeshPath(base_dir, mesh_dir, file, index);
      asset.scale =
          ParseDouble3(Attr(mesh_node, "scale"), {{1.0, 1.0, 1.0}});
      if (HasAttr(mesh_node, "refpos")) {
        asset.refpos = ParseDouble3(Attr(mesh_node, "refpos"), {{0, 0, 0}});
        asset.has_refpos = true;
      }
      const std::vector<double> rq = ParseDoubles(Attr(mesh_node, "refquat"));
      if (rq.size() >= 4) {
        asset.refquat = {{rq[0], rq[1], rq[2], rq[3]}};
        asset.has_refquat = true;
      }
      assets[name] = asset;
    }
  }
  return assets;
}

bool BuildGeomPayload(const pugi::xml_node &geom_node, const AttrMap &cls,
                      const std::string &cls_name,
                      const std::map<std::string, MeshAsset> &assets,
                      const Options &opts, const Context &ctx,
                      const std::vector<double> &body_world,
                      MeshPayload *payload, bool *is_visual, std::string *err) {
  const std::string type =
      HasEff(geom_node, cls, "type") ? Eff(geom_node, cls, "type") :
      HasEff(geom_node, cls, "mesh") ? "mesh" : "sphere";
  payload->name = Attr(geom_node, "name");
  if (payload->name.empty()) payload->name = Eff(geom_node, cls, "mesh", "geom");
  // Bake the accumulated body-chain world transform into the geom matrix: the
  // USD converter places every link Xform at identity, so each geom must carry
  // its full world placement (body_world * geom-local pose). The geom pose may
  // be inherited from a <default> class (e.g. allegro fingertip geoms get
  // pos="0 0 0.0267" from class="fingertip_visual"), so resolve via Eff.
  const std::array<double, 3> geom_pos =
      ParseDouble3(Eff(geom_node, cls, "pos"), {{0.0, 0.0, 0.0}});
  const Quat geom_quat = OrientationQuat(
      Eff(geom_node, cls, "quat"), Eff(geom_node, cls, "axisangle"),
      Eff(geom_node, cls, "euler"), Eff(geom_node, cls, "xyaxes"),
      Eff(geom_node, cls, "zaxis"), ctx);
  payload->matrix =
      MultiplyMatrix(body_world, MatrixFromPosQuat(geom_pos, geom_quat));

  // Classification mirrors the JS converter: a resolved geom group (own or
  // inherited from <default>) wins -- groups 0-2 are visible, 3-5 are
  // collision. With no group anywhere, fall back to the class name and then
  // MuJoCo's contype/conaffinity==0 (visual-only) convention.
  if (HasEff(geom_node, cls, "group")) {
    (*is_visual) =
        static_cast<int>(EffDouble(geom_node, cls, "group", 3.0)) < 3;
  } else {
    const std::string lname = ToLower(cls_name);
    if (lname.find("collision") != std::string::npos ||
        lname.find("collider") != std::string::npos) {
      (*is_visual) = false;
    } else if (Eff(geom_node, cls, "contype") == "0" &&
               Eff(geom_node, cls, "conaffinity") == "0") {
      (*is_visual) = true;
    } else {
      (*is_visual) = true;  // default group 0 is visible
    }
  }

  if (type == "mesh") {
    const std::string mesh_name = Eff(geom_node, cls, "mesh");
    const auto it = assets.find(mesh_name);
    if (it == assets.end()) {
      if (err) *err = "Missing MJCF mesh asset: " + mesh_name;
      return false;
    }
    if (!LoadMeshFile(it->second.path, &payload->mesh, err)) return false;

    const std::array<double, 3> scale =
        ParseDouble3(Eff(geom_node, cls, "scale"), it->second.scale);
    // G * refframe(refpos/refquat) * scale, matching the JS mesh handling.
    payload->matrix = MultiplyMatrix(payload->matrix, MeshRefMatrix(it->second));
    payload->matrix = MultiplyMatrix(payload->matrix, ScaleMatrix(scale));
  } else if (type == "box") {
    const auto half = ParseDouble3(Eff(geom_node, cls, "size"),
                                   {{0.05, 0.05, 0.05}});
    if (!(*is_visual) && !opts.tessellate_collision_shapes) {
      payload->shape = {{"type", "box"}};
      payload->matrix = MultiplyMatrix(payload->matrix, ScaleMatrix(half));
    } else {
      payload->mesh = MakeBoxMesh(half);
    }
  } else if (type == "sphere") {
    const auto size = ParseDoubles(Eff(geom_node, cls, "size"));
    const double radius = size.empty() ? 0.05 : size[0];
    if (!(*is_visual) && !opts.tessellate_collision_shapes) {
      payload->shape = {{"type", "sphere"}, {"radius", radius}};
    } else {
      payload->mesh = MakeSphereMesh(radius);
    }
  } else if (type == "ellipsoid") {
    const auto radii = ParseDouble3(Eff(geom_node, cls, "size"),
                                    {{0.05, 0.05, 0.05}});
    if (!(*is_visual) && !opts.tessellate_collision_shapes) {
      payload->shape = {{"type", "sphere"}, {"radius", 1.0}};
      payload->matrix = MultiplyMatrix(payload->matrix, ScaleMatrix(radii));
    } else {
      payload->mesh = MakeSphereMesh(1.0);
      payload->matrix = MultiplyMatrix(payload->matrix, ScaleMatrix(radii));
    }
  } else if (type == "cylinder" || type == "capsule") {
    const auto size = ParseDoubles(Eff(geom_node, cls, "size"));
    const double radius = size.empty() ? 0.05 : size[0];
    double half_length = size.size() >= 2 ? size[1] : radius;
    const auto fromto = ParseDoubles(Eff(geom_node, cls, "fromto"));
    if (fromto.size() >= 6) {
      const std::array<double, 3> dir{{fromto[3] - fromto[0],
                                       fromto[4] - fromto[1],
                                       fromto[5] - fromto[2]}};
      half_length =
          0.5 * std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
      // fromto fully specifies the geom frame (midpoint + Z aligned to dir),
      // overriding the geom's own pos/quat; still placed in body world space.
      const std::array<double, 3> center{{0.5 * (fromto[0] + fromto[3]),
                                          0.5 * (fromto[1] + fromto[4]),
                                          0.5 * (fromto[2] + fromto[5])}};
      payload->matrix = MultiplyMatrix(
          body_world,
          MatrixFromPosQuat(center, QuatFromTwoVecs({{0.0, 0.0, 1.0}}, dir)));
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
    const auto size = ParseDoubles(Eff(geom_node, cls, "size"));
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
  // MJCF inertia: <inertial fullinertia="Ixx Iyy Izz Ixy Ixz Iyz"> (full
  // symmetric tensor) or the more common <inertial diaginertia="Ixx Iyy Izz">.
  const std::vector<double> full =
      ParseDoubles(Attr(inertial_node, "fullinertia"));
  if (full.size() >= 6) {
    // Carry all 6 components so the converter can diagonalize them into
    // diagonalInertia (eigenvalues) + principalAxes (eigenvector quaternion).
    // Keep a diagonal fallback for consumers that ignore fullInertia.
    inertial["fullInertia"] = {full[0], full[1], full[2],
                               full[3], full[4], full[5]};
    inertial["diagonalInertia"] = {full[0], full[1], full[2]};
  } else if (full.size() >= 3) {
    inertial["diagonalInertia"] = {full[0], full[1], full[2]};
  } else {
    const std::vector<double> diag =
        ParseDoubles(Attr(inertial_node, "diaginertia"));
    if (diag.size() >= 3) {
      inertial["diagonalInertia"] = {diag[0], diag[1], diag[2]};
    }
  }
  return inertial;
}

// Build one joint JSON entry from an explicit <joint> node (or a null node for
// a fixed connection), wiring parent_name -> child_name with the given origin
// frame. `origin_matrix` (16 elems) and `origin_pos` are the joint frame; in a
// multi-DOF chain only the first joint carries the body offset (the rest are at
// identity). A null joint_node yields a PhysicsFixedJoint.
void AddJointFromNode(const pugi::xml_node &joint_node, const AttrMap &jcls,
                      const Context &ctx, const std::string &parent_name,
                      const std::string &child_name,
                      const std::vector<double> &origin_matrix,
                      const std::array<double, 3> &origin_pos,
                      nlohmann::json *joints) {
  nlohmann::json joint = nlohmann::json::object();
  joint["name"] = joint_node ? Attr(joint_node, "name", parent_name + "_to_" + child_name)
                             : parent_name + "_to_" + child_name + "_fixed";
  const std::string mj_type =
      joint_node ? Eff(joint_node, jcls, "type", "hinge") : "fixed";
  // MuJoCo ball (3-DOF rotation) -> USD PhysicsSphericalJoint. Matches the JS
  // parsers (web/js/cli/urdf-to-usd.js, web/js/urdf.js); the USD converter
  // already handles "spherical". (free/floating base is still emitted as
  // "fixed" here — handled separately as the articulation root.)
  joint["type"] = (mj_type == "hinge") ? "revolute" :
                  (mj_type == "slide") ? "prismatic" :
                  (mj_type == "ball")  ? "spherical" : "fixed";
  joint["parent"] = parent_name;
  joint["child"] = child_name;
  const auto axis = ParseDouble3(joint_node ? Eff(joint_node, jcls, "axis") : "",
                                 {{0.0, 0.0, 1.0}});
  joint["axis"] = {axis[0], axis[1], axis[2]};
  joint["axisToken"] = AxisToken(axis);
  joint["origin"] = {origin_pos[0], origin_pos[1], origin_pos[2]};
  joint["originMatrix"] = origin_matrix;

  if (joint_node) {
    const std::vector<double> range = ParseDoubles(Eff(joint_node, jcls, "range"));
    if (range.size() >= 2) {
      // The USD converter expects revolute limits in radians (it re-converts to
      // degrees). MJCF hinge ranges are in the compiler's angle unit (degrees by
      // default), so convert them; slide ranges are meters and pass through.
      const double scale = (mj_type == "hinge") ? ctx.angle_to_rad : 1.0;
      joint["limit"] = {{"lower", range[0] * scale}, {"upper", range[1] * scale}};
    }
    nlohmann::json dynamics = nlohmann::json::object();
    if (HasEff(joint_node, jcls, "damping")) {
      dynamics["damping"] = EffDouble(joint_node, jcls, "damping", 0.0);
    }
    if (HasEff(joint_node, jcls, "frictionloss")) {
      dynamics["friction"] = EffDouble(joint_node, jcls, "frictionloss", 0.0);
    }
    // MJCF <joint stiffness=> and <joint armature=> — propagate so the
    // USD-side converter can author physxLimit:*:stiffness and
    // physxJoint:armature alongside the canonical mjc:* fallbacks.
    if (HasEff(joint_node, jcls, "stiffness")) {
      dynamics["stiffness"] = EffDouble(joint_node, jcls, "stiffness", 0.0);
    }
    if (HasEff(joint_node, jcls, "armature")) {
      dynamics["armature"] = EffDouble(joint_node, jcls, "armature", 0.0);
    }
    joint["dynamics"] = dynamics;
    // MJCF <joint ref="..."> is the rest-angle (degrees for hinge under the
    // default <compiler angle="degree">, meters for slide). Forward it as a
    // generic `initPosition` (radians for hinge) so the C++ converter can
    // emit state:{angular,linear}:physics:position.
    if (HasEff(joint_node, jcls, "ref")) {
      double ref = EffDouble(joint_node, jcls, "ref", 0.0);
      if (mj_type == "hinge") ref *= ctx.angle_to_rad;
      joint["initPosition"] = ref;
    }
  }

  joints->push_back(joint);
}

void AddMujocoActuatorsJson(const pugi::xml_node &root,
                            nlohmann::json *actuators,
                            nlohmann::json *mjc_actuators, Stats *stats) {
  if (!actuators) return;
  const auto actuator_root = Child(root, "actuator");
  if (!actuator_root) return;

  for (const auto &act_node : actuator_root.children()) {
    const std::string type = act_node.name();
    const std::string joint = Attr(act_node, "joint");
    const std::string tendon = Attr(act_node, "tendon");
    const std::string site = Attr(act_node, "site");

    // Muscle / general / tendon- or site-targeted actuators -> MjcActuator,
    // preserving the MuJoCo gain/bias/lengthrange parameters (e.g. ms_human_700
    // muscles are `<general class="muscle" tendon=.. gainprm=.. biasprm=..>`).
    // Joint-targeted position/velocity/motor actuators keep the Newton PD path.
    const bool is_mjc = (type == "muscle" || type == "general" ||
                         !tendon.empty() || !site.empty());
    if (is_mjc && mjc_actuators) {
      const std::string fallback_target =
          !joint.empty() ? joint : (!tendon.empty() ? tendon : site);
      nlohmann::json a = nlohmann::json::object();
      a["name"] = Attr(act_node, "name", type + "_" + fallback_target);
      a["actuatorType"] = type;
      if (!joint.empty()) a["targetJoint"] = joint;
      if (!tendon.empty()) a["targetTendon"] = tendon;
      if (!site.empty()) a["targetSite"] = site;
      const auto gainprm = ParseDoubles(Attr(act_node, "gainprm"));
      if (!gainprm.empty()) a["gainPrm"] = gainprm;
      const auto biasprm = ParseDoubles(Attr(act_node, "biasprm"));
      if (!biasprm.empty()) a["biasPrm"] = biasprm;
      const auto lengthrange = ParseDoubles(Attr(act_node, "lengthrange"));
      if (lengthrange.size() >= 2)
        a["lengthRange"] = {lengthrange[0], lengthrange[1]};
      const auto ctrlrange = ParseDoubles(Attr(act_node, "ctrlrange"));
      if (ctrlrange.size() >= 2) a["ctrlRange"] = {ctrlrange[0], ctrlrange[1]};
      const auto forcerange = ParseDoubles(Attr(act_node, "forcerange"));
      if (forcerange.size() >= 2)
        a["forceRange"] = {forcerange[0], forcerange[1]};
      const auto gear = ParseDoubles(Attr(act_node, "gear"));
      if (!gear.empty()) a["gear"] = gear;
      if (HasAttr(act_node, "dyntype")) a["dynType"] = Attr(act_node, "dyntype");
      if (HasAttr(act_node, "gaintype")) a["gainType"] = Attr(act_node, "gaintype");
      if (HasAttr(act_node, "biastype")) a["biasType"] = Attr(act_node, "biastype");
      mjc_actuators->push_back(std::move(a));
      if (stats) stats->actuators++;
      continue;
    }

    if (joint.empty()) {
      continue;
    }
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

// MJCF <tendon><fixed> -> JSON tendon entries consumed by AddMjcTendonFromJson.
// A fixed tendon constrains a linear combination of joint coordinates; we carry
// the {joint, coef} list plus the spring/damper/range scalars. Spatial tendons
// (sites/pulleys) aren't representable without sites yet -> warn and skip.
void AddMujocoTendonsJson(const pugi::xml_node &root, const Context &ctx,
                          nlohmann::json *tendons, Stats *stats) {
  if (!tendons) return;
  const auto tendon_root = Child(root, "tendon");
  if (!tendon_root) return;
  for (const auto &t_node : tendon_root.children()) {
    const std::string kind = t_node.name();  // "fixed" or "spatial"
    if (kind != "fixed" && kind != "spatial") continue;
    nlohmann::json tendon = nlohmann::json::object();
    tendon["name"] = Attr(t_node, "name", "tendon_" + std::to_string(stats ? stats->tendons : 0));
    tendon["type"] = kind;
    if (kind == "fixed") {
      // Fixed tendon: linear combination of joint coordinates.
      nlohmann::json jlist = nlohmann::json::array();
      for (const auto &j_node : Children(t_node, "joint")) {
        const std::string jn = Attr(j_node, "joint");
        if (jn.empty()) continue;
        nlohmann::json je = nlohmann::json::object();
        je["joint"] = jn;
        je["coef"] = ParseDoubleAttr(j_node, "coef", 1.0);
        jlist.push_back(std::move(je));
      }
      if (jlist.empty()) continue;
      tendon["joints"] = std::move(jlist);
    } else {
      // Spatial tendon (muscle path): ordered <site>/<geom sidesite=...> waypoints.
      nlohmann::json waypoints = nlohmann::json::array();
      for (const auto &w : t_node.children()) {
        const std::string wn = w.name();
        if (wn == "site" && HasAttr(w, "site")) {
          waypoints.push_back({{"site", Attr(w, "site")}});
        } else if (wn == "geom") {
          nlohmann::json wp = {{"geom", Attr(w, "geom")}};
          if (HasAttr(w, "sidesite")) wp["sidesite"] = Attr(w, "sidesite");
          waypoints.push_back(std::move(wp));
        } else if (wn == "pulley" && HasAttr(w, "divisor")) {
          waypoints.push_back({{"pulley", ParseDoubleAttr(w, "divisor", 1.0)}});
        }
      }
      if (waypoints.size() < 2) continue;
      tendon["path"] = std::move(waypoints);
      if (HasAttr(t_node, "width"))
        tendon["width"] = ParseDoubleAttr(t_node, "width", 0.003);
      const auto rgba = ParseDoubles(Attr(t_node, "rgba"));
      if (rgba.size() >= 4) tendon["rgba"] = {rgba[0], rgba[1], rgba[2], rgba[3]};
    }
    if (HasAttr(t_node, "stiffness"))
      tendon["stiffness"] = ParseDoubleAttr(t_node, "stiffness", 0.0);
    if (HasAttr(t_node, "damping"))
      tendon["damping"] = ParseDoubleAttr(t_node, "damping", 0.0);
    if (HasAttr(t_node, "frictionloss"))
      tendon["frictionloss"] = ParseDoubleAttr(t_node, "frictionloss", 0.0);
    if (HasAttr(t_node, "margin"))
      tendon["margin"] = ParseDoubleAttr(t_node, "margin", 0.0);
    const auto range = ParseDoubles(Attr(t_node, "range"));
    if (range.size() >= 2) tendon["range"] = {range[0], range[1]};
    const auto springlen = ParseDoubles(Attr(t_node, "springlength"));
    if (!springlen.empty()) tendon["springlength"] = springlen;
    tendons->push_back(std::move(tendon));
    if (stats) stats->tendons++;
  }
  (void)ctx;
}

// MJCF <site> (in bodies) -> JSON site entries with a baked world transform,
// consumed by the converter to emit MjcSite marker prims. Sites are the routing
// points for spatial (muscle) tendons. Recurses the body tree.
void CollectMujocoSitesJson(const pugi::xml_node &body_node,
                            const std::vector<double> &parent_world,
                            const Context &ctx, nlohmann::json *sites,
                            Stats *stats) {
  const std::vector<double> body_world =
      MultiplyMatrix(parent_world, PoseMatrix(body_node, ctx));
  for (const auto &s_node : Children(body_node, "site")) {
    const std::string name = Attr(s_node, "name");
    if (name.empty()) continue;
    const std::vector<double> site_world =
        MultiplyMatrix(body_world, PoseMatrix(s_node, ctx));
    nlohmann::json site = {{"name", name}, {"matrix", site_world}};
    if (HasAttr(s_node, "group")) site["group"] = std::atoi(Attr(s_node, "group").c_str());
    const auto size = ParseDoubles(Attr(s_node, "size"));
    if (!size.empty()) site["size"] = size[0];
    sites->push_back(std::move(site));
    if (stats) stats->sites++;
  }
  for (const auto &child : Children(body_node, "body")) {
    CollectMujocoSitesJson(child, body_world, ctx, sites, stats);
  }
}

bool MjcBoolAttr(const std::string &v);  // defined below

// MJCF <light>/<camera> (in worldbody or nested bodies) -> JSON with a baked
// world matrix, consumed by the converter's AddLightFromJson/AddCameraFromJson.
// `frame_world` is the world transform of `frame_node`'s frame.
void CollectMujocoLightsCamerasJson(const pugi::xml_node &frame_node,
                                    const std::vector<double> &frame_world,
                                    const Context &ctx, nlohmann::json *lights,
                                    nlohmann::json *cameras) {
  for (const auto &l : Children(frame_node, "light")) {
    nlohmann::json light = nlohmann::json::object();
    light["name"] = Attr(l, "name", "light");
    std::string type = Attr(l, "type");
    if (type.empty())
      type = MjcBoolAttr(Attr(l, "directional", "false")) ? "directional" : "spot";
    light["type"] = type;
    light["matrix"] = MultiplyMatrix(frame_world, PoseMatrix(l, ctx));
    const auto dir = ParseDouble3(Attr(l, "dir"), {{0.0, 0.0, -1.0}});
    light["dir"] = {dir[0], dir[1], dir[2]};
    const auto diffuse = ParseDoubles(Attr(l, "diffuse"));
    if (diffuse.size() >= 3) light["color"] = {diffuse[0], diffuse[1], diffuse[2]};
    if (HasAttr(l, "castshadow"))
      light["castshadow"] = MjcBoolAttr(Attr(l, "castshadow"));
    if (HasAttr(l, "cutoff")) light["cutoff"] = ParseDoubleAttr(l, "cutoff", 45.0);
    lights->push_back(std::move(light));
  }
  for (const auto &c : Children(frame_node, "camera")) {
    nlohmann::json cam = nlohmann::json::object();
    cam["name"] = Attr(c, "name", "camera");
    cam["matrix"] = MultiplyMatrix(frame_world, PoseMatrix(c, ctx));
    if (HasAttr(c, "fovy")) cam["fovy"] = ParseDoubleAttr(c, "fovy", 45.0);
    const std::string proj = Attr(c, "projection");
    if (proj == "orthographic" || MjcBoolAttr(Attr(c, "orthographic", "false")))
      cam["orthographic"] = true;
    cameras->push_back(std::move(cam));
  }
  for (const auto &child : Children(frame_node, "body")) {
    const std::vector<double> body_world =
        MultiplyMatrix(frame_world, PoseMatrix(child, ctx));
    CollectMujocoLightsCamerasJson(child, body_world, ctx, lights, cameras);
  }
}

// MJCF <equality> -> JSON entries consumed by AddMjcEqualityFromJson.
void AddMujocoEqualityJson(const pugi::xml_node &root, nlohmann::json *equalities,
                           Stats *stats) {
  if (!equalities) return;
  const auto eq_root = Child(root, "equality");
  if (!eq_root) return;
  for (const auto &e_node : eq_root.children()) {
    const std::string kind = e_node.name();  // connect | weld | joint
    if (kind != "connect" && kind != "weld" && kind != "joint") continue;
    nlohmann::json eq = nlohmann::json::object();
    eq["name"] = Attr(e_node, "name", kind + "_" +
                      std::to_string(stats ? stats->equalities : 0));
    eq["type"] = kind;
    if (kind == "joint") {
      eq["joint1"] = Attr(e_node, "joint1");
      if (HasAttr(e_node, "joint2")) eq["joint2"] = Attr(e_node, "joint2");
      const auto poly = ParseDoubles(Attr(e_node, "polycoef"));
      if (!poly.empty()) eq["polycoef"] = poly;
    } else {
      eq["body1"] = Attr(e_node, "body1");
      if (HasAttr(e_node, "body2")) eq["body2"] = Attr(e_node, "body2");
      const auto anchor = ParseDoubles(Attr(e_node, "anchor"));
      if (!anchor.empty()) eq["anchor"] = anchor;
      if (kind == "weld" && HasAttr(e_node, "torquescale"))
        eq["torquescale"] = ParseDoubleAttr(e_node, "torquescale", 1.0);
    }
    const auto solref = ParseDoubles(Attr(e_node, "solref"));
    if (!solref.empty()) eq["solref"] = solref;
    const auto solimp = ParseDoubles(Attr(e_node, "solimp"));
    if (!solimp.empty()) eq["solimp"] = solimp;
    equalities->push_back(std::move(eq));
    if (stats) stats->equalities++;
  }
}

// MJCF <contact><exclude body1 body2> -> filteredPairs entries (maps to
// PhysicsFilteredPairsAPI in the converter, which already handles this array).
void AddMujocoContactExcludesJson(const pugi::xml_node &root,
                                  nlohmann::json *filtered_pairs, Stats *stats) {
  if (!filtered_pairs) return;
  const auto contact_root = Child(root, "contact");
  if (!contact_root) return;
  for (const auto &c_node : Children(contact_root, "exclude")) {
    const std::string b1 = Attr(c_node, "body1");
    const std::string b2 = Attr(c_node, "body2");
    if (b1.empty() || b2.empty()) continue;
    filtered_pairs->push_back({{"body1", b1}, {"body2", b2}});
    if (stats) stats->contact_excludes++;
  }
}

// Normalize a MuJoCo boolean/flag attribute: <flag x="enable|disable"> and
// <compiler x="true|false"> both occur; map enable/true/1 -> true.
bool MjcBoolAttr(const std::string &v) {
  return v == "true" || v == "enable" || v == "1";
}

// Read the full <option>/<option><flag>/<compiler> attribute set into a
// `mjcScene` JSON block (MJCF attr names) consumed by the converter's
// ApplyMjcSceneOptions -> MjcSceneAPI. (The MjcSceneAPI schema + USDC
// round-trip already support all of these; only timestep was emitted before.)
nlohmann::json BuildMjcSceneJson(const pugi::xml_node &root) {
  nlohmann::json ms = nlohmann::json::object();
  if (const auto option = Child(root, "option")) {
    nlohmann::json opt = nlohmann::json::object();
    auto num = [&](const char *k) { if (HasAttr(option, k)) opt[k] = ParseDoubleAttr(option, k, 0.0); };
    auto inum = [&](const char *k) { if (HasAttr(option, k)) opt[k] = static_cast<int>(ParseDoubleAttr(option, k, 0.0)); };
    auto tok = [&](const char *k) { if (HasAttr(option, k)) opt[k] = Attr(option, k); };
    auto vec = [&](const char *k) { auto v = ParseDoubles(Attr(option, k)); if (!v.empty()) opt[k] = v; };
    num("timestep"); num("impratio"); num("density"); num("viscosity"); num("o_margin");
    num("tolerance"); num("ls_tolerance"); num("noslip_tolerance"); num("ccd_tolerance");
    inum("iterations"); inum("ls_iterations"); inum("noslip_iterations");
    inum("ccd_iterations"); inum("sdf_iterations"); inum("sdf_initpoints");
    tok("integrator"); tok("cone"); tok("jacobian"); tok("solver");
    vec("wind"); vec("magnetic"); vec("o_solref"); vec("o_solimp"); vec("o_friction");
    if (!opt.empty()) ms["option"] = std::move(opt);
    if (const auto flag = Child(option, "flag")) {
      nlohmann::json fl = nlohmann::json::object();
      static const char *kFlags[] = {
          "constraint", "equality", "frictionloss", "limit", "contact",
          "gravity", "clampctrl", "warmstart", "filterparent", "actuation",
          "refsafe", "sensor", "midphase", "nativeccd", "eulerdamp",
          "autoreset", "island", "override", "energy", "fwdinv",
          "invdiscrete", "multiccd"};
      for (const char *k : kFlags)
        if (HasAttr(flag, k)) fl[k] = MjcBoolAttr(Attr(flag, k));
      if (!fl.empty()) ms["flag"] = std::move(fl);
    }
  }
  if (const auto compiler = Child(root, "compiler")) {
    nlohmann::json comp = nlohmann::json::object();
    auto num = [&](const char *k) { if (HasAttr(compiler, k)) comp[k] = ParseDoubleAttr(compiler, k, 0.0); };
    auto tok = [&](const char *k) { if (HasAttr(compiler, k)) comp[k] = Attr(compiler, k); };
    auto boolean = [&](const char *k) { if (HasAttr(compiler, k)) comp[k] = MjcBoolAttr(Attr(compiler, k)); };
    boolean("autolimits"); num("boundmass"); num("boundinertia"); num("settotalmass");
    boolean("usethread"); boolean("balanceinertia"); tok("angle"); boolean("fitaabb");
    boolean("fusestatic"); tok("inertiafromgeom"); boolean("alignfree"); boolean("saveinertial");
    const auto igr = ParseDoubles(Attr(compiler, "inertiagrouprange"));
    if (igr.size() >= 2) {
      comp["inertiagrouprange_min"] = static_cast<int>(igr[0]);
      comp["inertiagrouprange_max"] = static_cast<int>(igr[1]);
    }
    if (!comp.empty()) ms["compiler"] = std::move(comp);
  }
  return ms;
}

// MJCF <keyframe><key qpos/qvel/act/ctrl/mpos/mquat> -> JSON keyframe entries
// consumed by the converter's AddMjcKeyframeFromJson -> MjcKeyframe prim.
void AddMujocoKeyframesJson(const pugi::xml_node &root, nlohmann::json *keyframes,
                            Stats *stats) {
  if (!keyframes) return;
  const auto kf_root = Child(root, "keyframe");
  if (!kf_root) return;
  for (const auto &key : Children(kf_root, "key")) {
    nlohmann::json k = nlohmann::json::object();
    k["name"] = Attr(key, "name", "key_" + std::to_string(keyframes->size()));
    auto arr = [&](const char *attr) {
      auto v = ParseDoubles(Attr(key, attr));
      if (!v.empty()) k[attr] = v;
    };
    arr("qpos"); arr("qvel"); arr("act"); arr("ctrl"); arr("mpos"); arr("mquat");
    if (HasAttr(key, "time")) k["time"] = ParseDoubleAttr(key, "time", 0.0);
    keyframes->push_back(std::move(k));
    if (stats) stats->keyframes++;
  }
}

bool VisitMujocoBody(const pugi::xml_node &body_node,
                     const std::string &parent_name,
                     const std::string &childclass,
                     const std::vector<double> &parent_world,
                     const std::map<std::string, MeshAsset> &assets,
                     const Options &opts, const Context &ctx,
                     nlohmann::json *links, nlohmann::json *joints,
                     Stats *stats, std::string *err) {
  // A body's own `childclass` becomes the default class for its descendant
  // geoms/joints/bodies, unless overridden deeper.
  const std::string cc = HasAttr(body_node, "childclass")
                             ? Attr(body_node, "childclass")
                             : childclass;
  // Accumulate the body-chain world transform: this body's frame relative to
  // its parent, composed onto the parent's world frame.
  const std::vector<double> body_world =
      MultiplyMatrix(parent_world, PoseMatrix(body_node, ctx));
  const std::string body_name =
      Attr(body_node, "name", "body_" + std::to_string(stats->links));
  nlohmann::json link = {
      {"name", body_name},
      {"inertial", InertialToJson(body_node)},
      {"visuals", nlohmann::json::array()},
      {"collisions", nlohmann::json::array()}};

  for (const auto &geom_node : Children(body_node, "geom")) {
    const AttrMap &cls = ResolveGeomClass(geom_node, ctx.defaults, cc);
    std::string cls_name = Attr(geom_node, "class");
    if (cls_name.empty()) cls_name = cc;
    MeshPayload payload;
    bool visual = true;
    if (!BuildGeomPayload(geom_node, cls, cls_name, assets, opts, ctx,
                          body_world, &payload, &visual, err)) {
      if (opts.allow_missing) {
        std::cerr << "WARN: " << (err ? *err : "mesh skipped") << "\n";
        if (err) err->clear();
        continue;
      }
      return false;
    }
    if (visual) {
      nlohmann::json visual_json = MeshPayloadToJson(payload);
      AddGeomPhysicsAttrs(geom_node, cls, &visual_json);
      link["visuals"].push_back(std::move(visual_json));
      stats->visuals++;
    } else {
      nlohmann::json col = MeshPayloadToJson(payload);
      AddGeomPhysicsAttrs(geom_node, cls, &col);
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
    const std::vector<double> body_matrix = PoseMatrix(body_node, ctx);
    const std::array<double, 3> body_pos =
        ParseDouble3(Attr(body_node, "pos"), {{0.0, 0.0, 0.0}});
    const std::vector<double> identity = IdentityMatrix();
    const std::array<double, 3> zero_pos = {{0.0, 0.0, 0.0}};
    const std::vector<pugi::xml_node> joint_nodes =
        Children(body_node, "joint");

    if (joint_nodes.size() <= 1) {
      // 0 joints -> fixed connection; 1 joint -> single DOF (the common case).
      const pugi::xml_node jn =
          joint_nodes.empty() ? pugi::xml_node() : joint_nodes[0];
      const AttrMap &jcls = ResolveJointClass(jn, ctx.defaults, cc);
      AddJointFromNode(jn, jcls, ctx, parent_name, body_name, body_matrix,
                       body_pos, joints);
      stats->joints++;
    } else {
      // MuJoCo allows multiple <joint> per body (e.g. ball+slide, or a stack of
      // hinges) — they compose in sequence. Represent the N DOFs as a chain of
      // (N-1) massless intermediate link Xforms joined by single-DOF joints:
      //   parent --j0--> dof_1 --j1--> ... --j(N-1)--> body
      // Only the first joint carries the body offset; intermediates sit at the
      // body frame (identity). Geometry is world-baked on `body`, so the empty
      // intermediate links add no visible geometry.
      std::string prev = parent_name;
      for (size_t k = 0; k < joint_nodes.size(); k++) {
        const bool last = (k + 1 == joint_nodes.size());
        const std::string child =
            last ? body_name : (body_name + "__mjcdof_" + std::to_string(k + 1));
        if (!last) {
          // Emit a massless intermediate link (no geometry, no mass).
          nlohmann::json dof_link = {{"name", child},
                                     {"inertial", {{"mass", 0.0}}},
                                     {"visuals", nlohmann::json::array()},
                                     {"collisions", nlohmann::json::array()}};
          links->push_back(std::move(dof_link));
          stats->links++;
        }
        const AttrMap &jcls =
            ResolveJointClass(joint_nodes[k], ctx.defaults, cc);
        AddJointFromNode(joint_nodes[k], jcls, ctx, prev, child,
                         (k == 0) ? body_matrix : identity,
                         (k == 0) ? body_pos : zero_pos, joints);
        stats->joints++;
        prev = child;
      }
    }
  }

  for (const auto &child_body : Children(body_node, "body")) {
    if (!VisitMujocoBody(child_body, body_name, cc, body_world, assets, opts,
                         ctx, links, joints, stats, err)) {
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

  // <compiler angle/eulerseq> + <default> tree drive orientation units and
  // class inheritance for the whole scene.
  Context ctx;
  if (const auto compiler = Child(root, "compiler")) {
    if (ToLower(Attr(compiler, "angle", "degree")) == "radian") {
      ctx.angle_to_rad = 1.0;
    }
    const std::string eseq = Attr(compiler, "eulerseq");
    if (!eseq.empty()) ctx.eulerseq = eseq;
  }
  ctx.defaults = ParseDefaults(root);

  nlohmann::json links = nlohmann::json::array();
  nlohmann::json joints = nlohmann::json::array();
  nlohmann::json actuators = nlohmann::json::array();
  nlohmann::json tendons = nlohmann::json::array();
  nlohmann::json equalities = nlohmann::json::array();
  nlohmann::json filtered_pairs = nlohmann::json::array();
  nlohmann::json sites = nlohmann::json::array();
  nlohmann::json mjc_actuators = nlohmann::json::array();
  nlohmann::json keyframes = nlohmann::json::array();
  nlohmann::json lights = nlohmann::json::array();
  nlohmann::json cameras = nlohmann::json::array();
  for (const auto &body : Children(worldbody, "body")) {
    if (!VisitMujocoBody(body, "", "", IdentityMatrix(), assets, opts, ctx,
                         &links, &joints, stats, err)) {
      return false;
    }
    CollectMujocoSitesJson(body, IdentityMatrix(), ctx, &sites, stats);
  }
  AddMujocoActuatorsJson(root, &actuators, &mjc_actuators, stats);
  AddMujocoTendonsJson(root, ctx, &tendons, stats);
  AddMujocoEqualityJson(root, &equalities, stats);
  AddMujocoContactExcludesJson(root, &filtered_pairs, stats);
  AddMujocoKeyframesJson(root, &keyframes, stats);
  CollectMujocoLightsCamerasJson(worldbody, IdentityMatrix(), ctx, &lights,
                                 &cameras);

  (*payload) = {
      {"name", Attr(root, "model", input_filename.stem().string())},
      {"upAxis", opts.up_axis},
      {"sourceFormat", "mjcf"},
      {"gravity", opts.up_axis == "Z" ? nlohmann::json::array({0, 0, -1})
                                       : nlohmann::json::array({0, -1, 0})},
      {"links", std::move(links)},
      {"joints", std::move(joints)},
      {"actuators", std::move(actuators)}};
  if (!tendons.empty()) (*payload)["tendons"] = std::move(tendons);
  if (!equalities.empty()) (*payload)["equalities"] = std::move(equalities);
  if (!filtered_pairs.empty())
    (*payload)["filteredPairs"] = std::move(filtered_pairs);
  if (!sites.empty()) (*payload)["sites"] = std::move(sites);
  if (!mjc_actuators.empty()) (*payload)["mjcActuators"] = std::move(mjc_actuators);
  nlohmann::json mjc_scene = BuildMjcSceneJson(root);
  if (!mjc_scene.empty()) (*payload)["mjcScene"] = std::move(mjc_scene);
  if (!keyframes.empty()) (*payload)["keyframes"] = std::move(keyframes);
  if (!lights.empty()) (*payload)["lights"] = std::move(lights);
  if (!cameras.empty()) (*payload)["cameras"] = std::move(cameras);
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
