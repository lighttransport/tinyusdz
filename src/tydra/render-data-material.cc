// SPDX-License-Identifier: Apache 2.0
// Copyright 2022 - 2023, Syoyo Fujita.
// Copyright 2023 - Present, Light Transport Entertainment Inc.
//
// TODO:
//   - [ ] Subdivision surface to polygon mesh conversion.
//     - [ ] Correctly handle primvar with 'vertex' interpolation(Use the basis
//     function of subd surface)
//   - [ ] Support Inbetween BlendShape
//   - [ ] Support material binding collection(Collection API)
//   - [ ] Support multiple skel animation
//   https://github.com/PixarAnimationStudios/OpenUSD/issues/2246
//   - [ ] Adjust normal vector computation with handness?
//   - [ ] Node xform animation
//   - [ ] Better build of index buffer
//     - [ ] Preserve the order of 'points' variable(mesh.points, Skin
//     indices/weights, BlendShape points, ...) as much as possible.
//     - Implement spatial hash
//
//
// Material and texture conversion routines split from render-data.cc
//
#include <numeric>
#include <set>

#include "common-utils.hh"
#include "common-types.hh"
#include "enum-handlers.hh"
#include "../tiny-hashmap.hh"
#include "image-loader.hh"
#include "image-util.hh"
#include "image-types.hh"
#include "linear-algebra.hh"
#include "math-util.inc"
#include "pprinter.hh"
#include "core/prim.hh"
#include "str-util.hh"
#include "tiny-format.hh"
#include "tinyusdz.hh"
#include "usdGeom.hh"
#include "usdShade.hh"
#include "safe-arithmetic.hh"
#include "usdLux.hh"
#include "usdMtlx.hh"
#include "value-pprint.hh"
#include "logger.hh"
#include "bone-util.hh"
#include "shape-to-mesh.hh"
#include "materialx-to-json.hh"
#include "mmap-array-ref.hh"
#include "materialx-to-json.hh"
#include "security-policy.hh"

//
#include "common-macros.inc"
#include "math-util.inc"


//
#include "tydra/attribute-eval.hh"
#include "tydra/render-data.hh"
#include "tydra/render-data-internal.hh"
#include "tydra/scene-access.hh"
#include "tydra/shader-network.hh"

namespace tinyusdz {

namespace tydra {

namespace {

template <typename T>
bool ResolveTypedAnimatableValue(
    const Stage &stage,
    const TypedAttributeWithFallback<Animatable<T>> &attr,
    const std::string &attr_name,
    const double timecode,
    const value::TimeSampleInterpolationType tinterp,
    T *value_out,
    std::string *err) {
  return EvaluateTypedAnimatableAttribute(stage, attr, attr_name, value_out,
                                          err, timecode, tinterp);
}

template <typename T>
bool ResolveTypedAnimatableValue(
    const Stage &stage,
    const TypedAttribute<Animatable<T>> &attr,
    const std::string &attr_name,
    const double timecode,
    const value::TimeSampleInterpolationType tinterp,
    T *value_out,
    std::string *err) {
  return EvaluateTypedAnimatableAttribute(stage, attr, attr_name, value_out, err,
                                          timecode, tinterp);
}

template <typename EnumTy, typename EnumHandler>
bool ResolveEnumTokenAnimatableValue(
    const Stage &stage,
    const TypedAttributeWithFallback<Animatable<EnumTy>> &attr,
    const std::string &attr_name,
    EnumHandler enum_handler,
    const double timecode,
    const value::TimeSampleInterpolationType tinterp,
    EnumTy *value_out,
    std::string *err) {
  if (!value_out) {
    if (err) {
      (*err) += "`value_out` argument is nullptr.\n";
    }
    return false;
  }

  if (attr.has_connections()) {
    Attribute conn_attr;
    conn_attr.variability() = Variability::Varying;
    conn_attr.set_type_name(value::kToken);
    conn_attr.set_connections(attr.connections());

    TerminalAttributeValue resolved;
    if (!EvaluateAttribute(stage, conn_attr, attr_name, &resolved, err,
                           timecode, tinterp)) {
      return false;
    }

    std::string token_value;
    if (const auto *tok = resolved.as<value::token>()) {
      token_value = tok->str();
    } else if (const auto *str = resolved.as<std::string>()) {
      token_value = *str;
    } else {
      if (err) {
        (*err) += fmt::format(
            "Type mismatch. Value-producing attribute for `{}` has type `{}`, "
            "but `token` was expected.\n",
            attr_name, resolved.type_name());
      }
      return false;
    }

    auto parsed = enum_handler(token_value);
    if (!parsed) {
      if (err) {
        (*err) += fmt::format(
            "Failed to resolve `{}` from connected value `{}`: {}\n", attr_name,
            token_value, parsed.error());
      }
      return false;
    }

    *value_out = parsed.value();
    return true;
  }

  const auto &value = attr.get_value();
  if (value.get(timecode, value_out, tinterp)) {
    return true;
  }

  if (err) {
    (*err) += fmt::format("Failed to get `{}` at the requested time.\n",
                          attr_name);
  }
  return false;
}

bool ResolveSourceColorSpace(
    const Stage &stage,
    const TypedAttributeWithFallback<Animatable<UsdUVTexture::SourceColorSpace>>
        &sourceColorSpace,
    const double timecode,
    const value::TimeSampleInterpolationType tinterp,
    UsdUVTexture::SourceColorSpace *value_out,
    std::string *err) {
  return ResolveEnumTokenAnimatableValue(
      stage, sourceColorSpace, "inputs:sourceColorSpace",
      enum_handler::SourceColorSpace, timecode, tinterp, value_out, err);
}

bool ResolveTextureWrap(
    const Stage &stage,
    const TypedAttributeWithFallback<Animatable<UsdUVTexture::Wrap>> &wrap_attr,
    const std::string &attr_name,
    const double timecode,
    const value::TimeSampleInterpolationType tinterp,
    UsdUVTexture::Wrap *value_out,
    std::string *err) {
  return ResolveEnumTokenAnimatableValue(stage, wrap_attr, attr_name,
                                         enum_handler::TextureWrap, timecode,
                                         tinterp, value_out, err);
}

}  // namespace

bool RawAssetRead(
    const value::AssetPath &assetPath, const AssetInfo &assetInfo,
    const AssetResolutionResolver &assetResolver,
    Asset *assetOut,
    std::string &resolvedPathOut,
    void *userdata, std::string *warn,
    std::string *err) {
  if (!assetOut) {
    if (err) {
      (*err) = "`assetOut` argument is nullptr\n";
    }
    return false;
  }

  // TODO: assetInfo
  (void)assetInfo;
  (void)userdata;
  (void)warn;

  std::string sanitized_path =
      utils::SanitizeAssetPath(assetPath.GetAssetPath());
  if (sanitized_path.empty()) {
    if (err) {
      (*err) += fmt::format("Unsafe asset path: {}\n", assetPath.GetAssetPath());
    }
    return false;
  }

  std::string resolvedPath = assetResolver.resolve(sanitized_path);

  if (resolvedPath.empty()) {
    if (err) {
      (*err) += fmt::format("Failed to resolve asset path: {}\n",
                            assetPath.GetAssetPath());
    }
    return false;
  }

  Asset asset;
  bool ret = assetResolver.open_asset(resolvedPath, sanitized_path,
                                      &asset, warn, err);
  if (!ret) {
    if (err) {
      (*err) += fmt::format("Failed to open asset: {}", resolvedPath);
    }
    return false;
  }

  if (asset.size() > security_policy::kResolverMaxAssetReadBytes) {
    if (err) {
      (*err) += fmt::format("Resolved asset exceeds max bytes ({} > {}).",
                            asset.size(), security_policy::kResolverMaxAssetReadBytes);
    }
    return false;
  }

  DCOUT("Resolved asset path = " << resolvedPath);

  resolvedPathOut = resolvedPath;
  (*assetOut) = std::move(asset);

  return true;
}

namespace {

// Structure to hold MaterialX NodeGraph info extracted from geometry_normal/geometry_tangent connections
// Used to capture tangent rotation and normal map scale for anisotropic materials
struct MtlxNodeGraphInfo {
  float tangent_rotation{0.0f};      // From ND_rotate3d_vector3 node's "amount" input (degrees)
  float normal_map_scale{1.0f};      // From ND_normalmap_float node's "scale" input
  bool has_normal_map{false};        // True if ND_normalmap node was found in the chain
  bool has_tangent_rotation{false};  // True if ND_rotate3d_vector3 node was found
  std::string normal_map_texture;    // Path to normal map texture asset
  std::string geomprop_name;         // From ND_geompropvalue node's "geomprop" input (primvar name)
  bool has_geomprop{false};          // True if ND_geompropvalue node was found
  int texcoord_index{0};             // From ND_texcoord node's "index" input (UV set index)
  std::array<float, 2> uvtiling{{1.0f, 1.0f}};  // From ND_tiledimage's "uvtiling" input
  std::array<float, 2> uvoffset{{0.0f, 0.0f}};  // From ND_tiledimage's "uvoffset" input
  bool has_uvtransform{false};       // True if non-default tiling/offset was found
  std::array<float, 4> constant_value{{0.0f, 0.0f, 0.0f, 0.0f}};  // From ND_constant terminal node
  int constant_components{0};       // Number of components: 1=float, 2=float2, 3=color3f/float3, 4=color4f/float4
  bool has_constant{false};         // True if ND_constant node was found
};

// Extract MaterialX NodeGraph info by traversing connections
// Returns the extracted info or an error message
// Multi-component constant value used by the MaterialX constant evaluator.
// Represents float (1 component) or color3f (3 components).
struct MtlxConstVal {
  std::array<float, 3> v{{0.0f, 0.0f, 0.0f}};
  int n{0};  // number of components: 1=float, 3=color3

  static MtlxConstVal Float(float f) { MtlxConstVal r; r.v[0]=f; r.n=1; return r; }
  static MtlxConstVal Color3(float r, float g, float b) {
    MtlxConstVal c; c.v = {{r, g, b}}; c.n = 3; return c;
  }

  bool is_float() const { return n == 1; }
  bool is_color3() const { return n == 3; }
  float as_float() const { return v[0]; }
};

// Resolve a shader node within a NodeGraph prim by its absolute path.
// Tries stage lookup first, then falls back to searching NodeGraph children.
static const Shader *FindShaderInNodeGraph(
    const Stage &stage, const Prim *ng_prim,
    const std::string &prim_path) {
  std::string err;
  const Prim *prim{nullptr};
  if (stage.find_prim_at_path(Path(prim_path, ""), prim, &err) && prim) {
    return prim->as<Shader>();
  }
  if (ng_prim) {
    size_t last_slash = prim_path.rfind('/');
    if (last_slash != std::string::npos) {
      std::string child_name = prim_path.substr(last_slash + 1);
      for (const auto &child : ng_prim->children()) {
        if (child.element_name() == child_name) {
          return child.as<Shader>();
        }
      }
    }
  }
  return nullptr;
}

// Forward declaration
static nonstd::expected<MtlxConstVal, std::string> EvaluateMtlxConstant(
    const Stage &stage, const Prim *ng_prim, const Shader *shader,
    int max_depth);

// Helper: get the property map for a shader (prefer ShaderNode props if present)
static const std::map<std::string, Property> &GetShaderProps(
    const Shader *shader) {
  const ShaderNode *sn = shader->value.as<ShaderNode>();
  if (sn && !sn->props.empty()) return sn->props;
  return shader->props;
}

// Helper: resolve a generic input to MtlxConstVal (constant or connected)
static nonstd::expected<MtlxConstVal, std::string> ResolveInput(
    const Stage &stage, const Prim *ng_prim,
    const std::map<std::string, Property> &props,
    const std::string &input_name, const std::string &node_type,
    int max_depth) {
  auto it = props.find(input_name);
  if (it == props.end()) {
    return nonstd::make_unexpected(
        fmt::format("{} not found in {}", input_name, node_type));
  }
  if (!it->second.is_attribute()) {
    return nonstd::make_unexpected(
        fmt::format("{} is not an attribute", input_name));
  }
  const Attribute &attr = it->second.get_attribute();

  // If connected, follow recursively
  if (attr.has_connections() && !attr.connections().empty()) {
    const Shader *next = FindShaderInNodeGraph(
        stage, ng_prim, attr.connections()[0].prim_part());
    if (!next) {
      return nonstd::make_unexpected(
          fmt::format("Cannot find shader at {}",
                      attr.connections()[0].prim_part()));
    }
    return EvaluateMtlxConstant(stage, ng_prim, next, max_depth - 1);
  }

  // Read constant value
  if (auto vf = attr.get_value<float>()) {
    return MtlxConstVal::Float(*vf);
  }
  if (auto vc = attr.get_value<value::color3f>()) {
    return MtlxConstVal::Color3((*vc)[0], (*vc)[1], (*vc)[2]);
  }
  if (auto vf3 = attr.get_value<value::float3>()) {
    return MtlxConstVal::Color3((*vf3)[0], (*vf3)[1], (*vf3)[2]);
  }
  if (auto vi = attr.get_value<int>()) {
    return MtlxConstVal::Float(static_cast<float>(*vi));
  }
  return nonstd::make_unexpected(
      fmt::format("Cannot read value from {}", input_name));
}

// Helper: convenience wrapper for ResolveInput
static nonstd::expected<MtlxConstVal, std::string> RI(
    const Stage &stage, const Prim *ng_prim,
    const std::map<std::string, Property> &props,
    const std::string &input_name, const std::string &node_type,
    int max_depth) {
  return ResolveInput(stage, ng_prim, props, input_name, node_type, max_depth);
}

// Per-component binary operation on MtlxConstVal
static MtlxConstVal BinOp(const MtlxConstVal &a, const MtlxConstVal &b,
                           float (*op)(float, float)) {
  int nc = (std::max)(a.n, b.n);
  MtlxConstVal r; r.n = nc;
  for (int i = 0; i < nc; ++i) {
    float va = (i < a.n) ? a.v[static_cast<size_t>(i)] : a.v[0];
    float vb = (i < b.n) ? b.v[static_cast<size_t>(i)] : b.v[0];
    r.v[static_cast<size_t>(i)] = op(va, vb);
  }
  return r;
}

// Simple RGB-to-HSV and HSV-to-RGB conversions (used by ND_hsv_adjust_color3).
static void RGBtoHSV(float r, float g, float b, float &h, float &s, float &v) {
  float mx = (std::max)({r, g, b});
  float mn = (std::min)({r, g, b});
  float d = mx - mn;
  v = mx;
  s = (mx > 0.0f) ? d / mx : 0.0f;
  if (d < 1e-7f) { h = 0.0f; return; }
  if (r >= mx)      h = (g - b) / d;
  else if (g >= mx) h = 2.0f + (b - r) / d;
  else              h = 4.0f + (r - g) / d;
  h /= 6.0f;
  if (h < 0.0f) h += 1.0f;
}

static void HSVtoRGB(float h, float s, float v, float &r, float &g, float &b) {
  if (s <= 0.0f) { r = g = b = v; return; }
  float hh = h * 6.0f;
  if (hh >= 6.0f) hh = 0.0f;
  int i = static_cast<int>(hh);
  float ff = hh - static_cast<float>(i);
  float p = v * (1.0f - s);
  float q = v * (1.0f - s * ff);
  float t = v * (1.0f - s * (1.0f - ff));
  switch (i) {
    case 0: r=v; g=t; b=p; break;
    case 1: r=q; g=v; b=p; break;
    case 2: r=p; g=v; b=t; break;
    case 3: r=p; g=q; b=v; break;
    case 4: r=t; g=p; b=v; break;
    default: r=v; g=p; b=q; break;
  }
}

// Evaluate a MaterialX node graph expression that produces a constant value.
// Supports float and color3 node types with constant or connected inputs.
// Returns the evaluated value or an error.
static nonstd::expected<MtlxConstVal, std::string> EvaluateMtlxConstant(
    const Stage &stage,
    const Prim *ng_prim,
    const Shader *shader,
    int max_depth = 10) {
  if (max_depth <= 0) {
    return nonstd::make_unexpected("Max evaluation depth exceeded");
  }
  if (!shader) {
    return nonstd::make_unexpected("Null shader");
  }

  const std::string &nt = shader->info_id;
  const auto &props = GetShaderProps(shader);

  // Macro-style shorthand for resolving inputs
  #define RESOLVE(name) RI(stage, ng_prim, props, name, nt, max_depth)

  // --- Constant nodes ---
  if (nt == "ND_constant_float" || nt == "ND_constant_color3" ||
      nt == "ND_constant_vector3") {
    auto r = RESOLVE("inputs:value");
    if (!r) return nonstd::make_unexpected(r.error());
    if (nt == "ND_constant_float") return MtlxConstVal::Float(r->as_float());
    return *r;
  }

  // --- Binary float/color ops ---
  if (nt.find("ND_add_") == 0 || nt.find("ND_subtract_") == 0 ||
      nt.find("ND_multiply_") == 0 || nt.find("ND_divide_") == 0 ||
      nt.find("ND_min_") == 0 || nt.find("ND_max_") == 0 ||
      nt.find("ND_power_") == 0 || nt.find("ND_modulo_") == 0 ||
      nt.find("ND_atan2_") == 0) {
    auto a = RESOLVE("inputs:in1");
    if (!a) return nonstd::make_unexpected(a.error());
    auto b = RESOLVE("inputs:in2");
    if (!b) return nonstd::make_unexpected(b.error());

    if (nt.find("ND_add_") == 0)
      return BinOp(*a, *b, [](float x, float y) { return x + y; });
    if (nt.find("ND_subtract_") == 0)
      return BinOp(*a, *b, [](float x, float y) { return x - y; });
    if (nt.find("ND_multiply_") == 0)
      return BinOp(*a, *b, [](float x, float y) { return x * y; });
    if (nt.find("ND_divide_") == 0)
      return BinOp(*a, *b, [](float x, float y) { return (y != 0.0f) ? x / y : 0.0f; });
    if (nt.find("ND_power_") == 0)
      return BinOp(*a, *b, [](float x, float y) { return std::pow(x, y); });
    if (nt.find("ND_min_") == 0)
      return BinOp(*a, *b, [](float x, float y) { return (std::min)(x, y); });
    if (nt.find("ND_max_") == 0)
      return BinOp(*a, *b, [](float x, float y) { return (std::max)(x, y); });
    if (nt.find("ND_modulo_") == 0)
      return BinOp(*a, *b, [](float x, float y) { return (y != 0.0f) ? std::fmod(x, y) : 0.0f; });
    if (nt.find("ND_atan2_") == 0)
      return BinOp(*a, *b, [](float x, float y) { return std::atan2(x, y); });
  }

  // --- Clamp (float or color3) ---
  if (nt.find("ND_clamp_") == 0) {
    auto v = RESOLVE("inputs:in");
    if (!v) return nonstd::make_unexpected(v.error());
    auto lo = RESOLVE("inputs:low");
    if (!lo) return nonstd::make_unexpected(lo.error());
    auto hi = RESOLVE("inputs:high");
    if (!hi) return nonstd::make_unexpected(hi.error());
    auto clamped = BinOp(*v, *lo, [](float x, float y) { return (std::max)(x, y); });
    return BinOp(clamped, *hi, [](float x, float y) { return (std::min)(x, y); });
  }

  // --- Unary math ops (single input → single output) ---
  // ND_absval, ND_sign, ND_floor, ND_ceil, ND_round, ND_fract,
  // ND_sqrt, ND_invsqrt, ND_exp, ND_log,
  // ND_sin, ND_cos, ND_tan, ND_asin, ND_acos
  if (nt.find("ND_absval_") == 0 || nt.find("ND_sign_") == 0 ||
      nt.find("ND_floor_") == 0 || nt.find("ND_ceil_") == 0 ||
      nt.find("ND_round_") == 0 || nt.find("ND_fract_") == 0 ||
      nt.find("ND_sqrt_") == 0 || nt.find("ND_invsqrt_") == 0 ||
      nt.find("ND_exp_") == 0 || nt.find("ND_log_") == 0 ||
      nt.find("ND_sin_") == 0 || nt.find("ND_cos_") == 0 ||
      nt.find("ND_tan_") == 0 || nt.find("ND_asin_") == 0 ||
      nt.find("ND_acos_") == 0) {
    auto v = RESOLVE("inputs:in");
    if (!v) return nonstd::make_unexpected(v.error());
    MtlxConstVal r; r.n = v->n;
    for (int i = 0; i < v->n; ++i) {
      float x = v->v[static_cast<size_t>(i)];
      float y = 0.0f;
      if (nt.find("ND_absval_") == 0) y = std::abs(x);
      else if (nt.find("ND_sign_") == 0) y = (x > 0.0f) ? 1.0f : ((x < 0.0f) ? -1.0f : 0.0f);
      else if (nt.find("ND_floor_") == 0) y = std::floor(x);
      else if (nt.find("ND_ceil_") == 0) y = std::ceil(x);
      else if (nt.find("ND_round_") == 0) y = std::round(x);
      else if (nt.find("ND_fract_") == 0) y = x - std::floor(x);
      else if (nt.find("ND_sqrt_") == 0) y = (x >= 0.0f) ? std::sqrt(x) : 0.0f;
      else if (nt.find("ND_invsqrt_") == 0) y = (x > 0.0f) ? 1.0f / std::sqrt(x) : 0.0f;
      else if (nt.find("ND_exp_") == 0) y = std::exp(x);
      else if (nt.find("ND_log_") == 0) y = (x > 0.0f) ? std::log(x) : 0.0f;
      else if (nt.find("ND_sin_") == 0) y = std::sin(x);
      else if (nt.find("ND_cos_") == 0) y = std::cos(x);
      else if (nt.find("ND_tan_") == 0) y = std::tan(x);
      else if (nt.find("ND_asin_") == 0) y = std::asin((std::max)(-1.0f, (std::min)(1.0f, x)));
      else if (nt.find("ND_acos_") == 0) y = std::acos((std::max)(-1.0f, (std::min)(1.0f, x)));
      r.v[static_cast<size_t>(i)] = y;
    }
    return r;
  }

  // --- Convert (type conversion passthrough) ---
  if (nt.find("ND_convert_") == 0) {
    auto v = RESOLVE("inputs:in");
    if (!v) return nonstd::make_unexpected(v.error());
    // float→color3: broadcast, color3→float: take first component
    if (nt.find("_float_color3") != std::string::npos ||
        nt.find("_float_vector3") != std::string::npos) {
      return MtlxConstVal::Color3(v->as_float(), v->as_float(), v->as_float());
    }
    if (nt.find("_color3_float") != std::string::npos ||
        nt.find("_vector3_float") != std::string::npos) {
      return MtlxConstVal::Float(v->v[0]);
    }
    // color3<->vector3 are the same layout
    return *v;
  }

  // --- Luminance (color3 → float via Rec.709 coefficients) ---
  if (nt == "ND_luminance_color3") {
    auto v = RESOLVE("inputs:in");
    if (!v) return nonstd::make_unexpected(v.error());
    // Rec.709 luminance coefficients
    float lum = 0.2126f * v->v[0] + 0.7152f * v->v[1] + 0.0722f * v->v[2];
    return MtlxConstVal::Color3(lum, lum, lum);
  }

  // --- Dot product (vector3 → float) ---
  if (nt.find("ND_dotproduct_") == 0) {
    auto a = RESOLVE("inputs:in1");
    if (!a) return nonstd::make_unexpected(a.error());
    auto b = RESOLVE("inputs:in2");
    if (!b) return nonstd::make_unexpected(b.error());
    float dot = 0.0f;
    int nc = (std::min)(a->n, b->n);
    for (int i = 0; i < nc; ++i) dot += a->v[static_cast<size_t>(i)] * b->v[static_cast<size_t>(i)];
    return MtlxConstVal::Float(dot);
  }

  // --- Cross product (vector3 × vector3 → vector3) ---
  if (nt.find("ND_crossproduct_") == 0) {
    auto a = RESOLVE("inputs:in1");
    if (!a) return nonstd::make_unexpected(a.error());
    auto b = RESOLVE("inputs:in2");
    if (!b) return nonstd::make_unexpected(b.error());
    return MtlxConstVal::Color3(
        a->v[1] * b->v[2] - a->v[2] * b->v[1],
        a->v[2] * b->v[0] - a->v[0] * b->v[2],
        a->v[0] * b->v[1] - a->v[1] * b->v[0]);
  }

  // --- Magnitude (vector3 → float) ---
  if (nt.find("ND_magnitude_") == 0) {
    auto v = RESOLVE("inputs:in");
    if (!v) return nonstd::make_unexpected(v.error());
    float mag = 0.0f;
    for (int i = 0; i < v->n; ++i) mag += v->v[static_cast<size_t>(i)] * v->v[static_cast<size_t>(i)];
    return MtlxConstVal::Float(std::sqrt(mag));
  }

  // --- Normalize (vector3 → vector3) ---
  if (nt.find("ND_normalize_") == 0) {
    auto v = RESOLVE("inputs:in");
    if (!v) return nonstd::make_unexpected(v.error());
    float mag = 0.0f;
    for (int i = 0; i < v->n; ++i) mag += v->v[static_cast<size_t>(i)] * v->v[static_cast<size_t>(i)];
    mag = std::sqrt(mag);
    if (mag < 1e-7f) return *v;
    MtlxConstVal r; r.n = v->n;
    for (int i = 0; i < v->n; ++i) r.v[static_cast<size_t>(i)] = v->v[static_cast<size_t>(i)] / mag;
    return r;
  }

  // --- Conditional: ifgreater (float) ---
  if (nt == "ND_ifgreater_float") {
    auto val1 = RESOLVE("inputs:value1");
    if (!val1) return nonstd::make_unexpected(val1.error());
    auto val2 = RESOLVE("inputs:value2");
    if (!val2) return nonstd::make_unexpected(val2.error());
    auto in1 = RESOLVE("inputs:in1");
    if (!in1) return nonstd::make_unexpected(in1.error());
    auto in2 = RESOLVE("inputs:in2");
    if (!in2) return nonstd::make_unexpected(in2.error());
    return (val1->as_float() > val2->as_float()) ? *in1 : *in2;
  }

  // --- Mix (lerp between bg and fg) ---
  if (nt.find("ND_mix_") == 0) {
    auto bg = RESOLVE("inputs:bg");
    if (!bg) return nonstd::make_unexpected(bg.error());
    auto fg = RESOLVE("inputs:fg");
    if (!fg) return nonstd::make_unexpected(fg.error());
    auto mx = RESOLVE("inputs:mix");
    if (!mx) return nonstd::make_unexpected(mx.error());
    float t = mx->as_float();
    int nc = (std::max)(bg->n, fg->n);
    MtlxConstVal r; r.n = nc;
    for (int i = 0; i < nc; ++i) {
      float a = (i < bg->n) ? bg->v[static_cast<size_t>(i)] : bg->v[0];
      float b = (i < fg->n) ? fg->v[static_cast<size_t>(i)] : fg->v[0];
      r.v[static_cast<size_t>(i)] = a * (1.0f - t) + b * t;
    }
    return r;
  }

  // --- Remap: linearly remap from [inlow,inhigh] to [outlow,outhigh] ---
  if (nt.find("ND_remap_") == 0) {
    auto v = RESOLVE("inputs:in");
    if (!v) return nonstd::make_unexpected(v.error());
    auto inlo = RESOLVE("inputs:inlow");
    if (!inlo) return nonstd::make_unexpected(inlo.error());
    auto inhi = RESOLVE("inputs:inhigh");
    if (!inhi) return nonstd::make_unexpected(inhi.error());
    auto outlo = RESOLVE("inputs:outlow");
    if (!outlo) return nonstd::make_unexpected(outlo.error());
    auto outhi = RESOLVE("inputs:outhigh");
    if (!outhi) return nonstd::make_unexpected(outhi.error());
    int nc = v->n;
    MtlxConstVal r; r.n = nc;
    for (int i = 0; i < nc; ++i) {
      size_t ui = static_cast<size_t>(i);
      float vi = v->v[ui];
      float il = (i < inlo->n) ? inlo->v[ui] : inlo->v[0];
      float ih = (i < inhi->n) ? inhi->v[ui] : inhi->v[0];
      float ol = (i < outlo->n) ? outlo->v[ui] : outlo->v[0];
      float oh = (i < outhi->n) ? outhi->v[ui] : outhi->v[0];
      float denom = ih - il;
      float t_val = (std::abs(denom) > 1e-7f) ? (vi - il) / denom : 0.0f;
      r.v[ui] = ol + t_val * (oh - ol);
    }
    return r;
  }

  // --- Combine3 (three floats → color3) ---
  if (nt.find("ND_combine3_") == 0) {
    auto c1 = RESOLVE("inputs:in1");
    if (!c1) return nonstd::make_unexpected(c1.error());
    auto c2 = RESOLVE("inputs:in2");
    if (!c2) return nonstd::make_unexpected(c2.error());
    auto c3 = RESOLVE("inputs:in3");
    if (!c3) return nonstd::make_unexpected(c3.error());
    return MtlxConstVal::Color3(c1->as_float(), c2->as_float(), c3->as_float());
  }

  // --- Extract (color3 → float by index) ---
  if (nt.find("ND_extract_") == 0) {
    auto in_v = RESOLVE("inputs:in");
    if (!in_v) return nonstd::make_unexpected(in_v.error());
    auto idx = RESOLVE("inputs:index");
    if (!idx) return nonstd::make_unexpected(idx.error());
    int i = static_cast<int>(idx->as_float());
    if (i < 0 || i >= in_v->n) i = 0;
    return MtlxConstVal::Float(in_v->v[static_cast<size_t>(i)]);
  }

  // --- Separate3 (color3 → 3 outputs, but we return the full color3) ---
  if (nt.find("ND_separate3_") == 0) {
    auto in_v = RESOLVE("inputs:in");
    if (!in_v) return nonstd::make_unexpected(in_v.error());
    return *in_v;
  }

  // --- HSV adjust (two variants) ---
  // ND_hsv_adjust_color3: separate hue/saturation/value/fac inputs
  // ND_hsvadjust_color3: Blender export variant with inputs:amount (float3: h,s,v)
  if (nt == "ND_hsv_adjust_color3") {
    auto in_v = RESOLVE("inputs:in");
    if (!in_v) return nonstd::make_unexpected(in_v.error());
    auto hue = RESOLVE("inputs:hue");
    if (!hue) return nonstd::make_unexpected(hue.error());
    auto sat = RESOLVE("inputs:saturation");
    if (!sat) return nonstd::make_unexpected(sat.error());
    auto val = RESOLVE("inputs:value");
    if (!val) return nonstd::make_unexpected(val.error());
    auto fac = RESOLVE("inputs:fac");
    if (!fac) return nonstd::make_unexpected(fac.error());

    float h, s, v_hsv;
    RGBtoHSV(in_v->v[0], in_v->v[1], in_v->v[2], h, s, v_hsv);
    h += hue->as_float();
    if (h > 1.0f) h -= 1.0f;
    if (h < 0.0f) h += 1.0f;
    s *= sat->as_float();
    v_hsv *= val->as_float();

    float or_r, or_g, or_b;
    HSVtoRGB(h, s, v_hsv, or_r, or_g, or_b);

    float f = fac->as_float();
    return MtlxConstVal::Color3(
        in_v->v[0] * (1.0f - f) + or_r * f,
        in_v->v[1] * (1.0f - f) + or_g * f,
        in_v->v[2] * (1.0f - f) + or_b * f);
  }

  if (nt == "ND_hsvadjust_color3") {
    auto in_v = RESOLVE("inputs:in");
    if (!in_v) return nonstd::make_unexpected(in_v.error());
    auto amount = RESOLVE("inputs:amount");
    if (!amount) return nonstd::make_unexpected(amount.error());

    float h, s, v_hsv;
    RGBtoHSV(in_v->v[0], in_v->v[1], in_v->v[2], h, s, v_hsv);
    // amount.v[0]=hue shift, amount.v[1]=saturation mult, amount.v[2]=value mult
    h += amount->v[0];
    if (h > 1.0f) h -= 1.0f;
    if (h < 0.0f) h += 1.0f;
    s *= amount->v[1];
    v_hsv *= amount->v[2];

    float or_r, or_g, or_b;
    HSVtoRGB(h, s, v_hsv, or_r, or_g, or_b);
    return MtlxConstVal::Color3(or_r, or_g, or_b);
  }

  #undef RESOLVE

  return nonstd::make_unexpected(
      fmt::format("Unsupported node type for constant evaluation: {}", nt));
}

// Resolve a NodeGraph connection path to the NodeGraph prim and target shader.
// Shared logic for both float and color3 evaluation entry points.
static nonstd::expected<std::pair<const Prim *, const Shader *>, std::string>
ResolveNodeGraphTarget(const Stage &stage, const Path &connection_path) {
  const std::string prim_part = connection_path.prim_part();
  const std::string prop_part = connection_path.prop_part();

  std::string err;
  const Prim *ng_prim{nullptr};
  bool found = stage.find_prim_at_path(Path(prim_part, ""), ng_prim, &err);

  if (!found || !ng_prim) {
    size_t last_slash = prim_part.rfind('/');
    if (last_slash == std::string::npos) {
      return nonstd::make_unexpected("Invalid path");
    }
    std::string parent_path = prim_part.substr(0, last_slash);
    std::string ng_name = prim_part.substr(last_slash + 1);
    const Prim *parent_prim{nullptr};
    if (stage.find_prim_at_path(Path(parent_path, ""), parent_prim, &err) &&
        parent_prim) {
      for (const auto &child : parent_prim->children()) {
        if (child.element_name() == ng_name ||
            child.data().type_name() == "NodeGraph") {
          ng_prim = &child;
          break;
        }
      }
    }
    if (!ng_prim) {
      return nonstd::make_unexpected(
          fmt::format("NodeGraph not found at {}", prim_part));
    }
  }

  const NodeGraph *ng = ng_prim->as<NodeGraph>();
  if (!ng) {
    return nonstd::make_unexpected("Not a NodeGraph");
  }

  std::string output_name = prop_part;
  if (startsWith(output_name, "outputs:")) {
    output_name = output_name.substr(8);
  }

  Path target_path;
  bool found_conn = false;
  for (const auto &suffix : {"outputs:" + output_name + ".connect",
                              "outputs:" + output_name}) {
    auto it = ng->props.find(suffix);
    if (it != ng->props.end()) {
      if (it->second.is_attribute()) {
        const Attribute &attr = it->second.get_attribute();
        if (attr.has_connections() && !attr.connections().empty()) {
          target_path = attr.connections()[0];
          found_conn = true;
          break;
        }
      } else if (it->second.is_relationship()) {
        auto targets = it->second.get_relationTargets();
        if (!targets.empty()) {
          target_path = targets[0];
          found_conn = true;
          break;
        }
      }
    }
  }

  if (!found_conn) {
    return nonstd::make_unexpected("No output connection found");
  }

  const Shader *target_shader =
      FindShaderInNodeGraph(stage, ng_prim, target_path.prim_part());
  if (!target_shader) {
    return nonstd::make_unexpected(
        fmt::format("Shader not found at {}", target_path.prim_part()));
  }

  return std::make_pair(ng_prim, target_shader);
}

// Evaluate a MaterialX NodeGraph connection as a constant value (float or color3).
static nonstd::expected<MtlxConstVal, std::string> EvaluateMtlxNodeGraphAsConstant(
    const Stage &stage, const Path &connection_path) {
  auto target = ResolveNodeGraphTarget(stage, connection_path);
  if (!target) return nonstd::make_unexpected(target.error());
  return EvaluateMtlxConstant(stage, target->first, target->second);
}

static nonstd::expected<MtlxNodeGraphInfo, std::string> ExtractMtlxNodeGraphInfo(
    const Stage &stage,
    const Prim *material_prim,
    const std::vector<Path> &connections,
    std::string *err) {

  MtlxNodeGraphInfo info;

  if (connections.empty()) {
    return info;  // No connections, return default
  }

  // Follow the first connection (we only support single connection)
  Path current_path = connections[0];

  // Maximum depth to prevent infinite loops
  int max_depth = 15;

  while (max_depth-- > 0) {
    std::string current_prim_part = current_path.prim_part();
    std::string current_prop_part = current_path.prop_part();
    if (err) {
      *err += "DEBUG: current_prim_part=" + current_prim_part + ", prop_part=" + current_prop_part + "\n";
    }

    const Prim *current_prim{nullptr};

    // Try to find the prim in the stage
    std::string lookup_err;
    bool found = stage.find_prim_at_path(Path(current_prim_part, ""), current_prim, &lookup_err);

    // If not found in stage, try looking in material's children (NodeGraph case)
    if (!found || !current_prim) {
      if (err) {
        *err += "DEBUG: Not found in stage, looking in material children. material_prim=" + std::string(material_prim ? "valid" : "null") + "\n";
      }
      if (material_prim) {
        if (err) {
          *err += "DEBUG: material_prim has " + std::to_string(material_prim->children().size()) + " children\n";
        }
        // Look for NodeGraph child
        for (const auto& child : material_prim->children()) {
          if (err) {
            *err += "DEBUG: Checking child: '" + child.element_name() + "' type='" + child.type_name() + "' is_nodegraph=" + (child.as<NodeGraph>() ? "true" : "false") + "\n";
          }
          // Try to match by type if the path contains "NodeGraph" and this child is a NodeGraph
          if (current_prim_part.find("NodeGraph") != std::string::npos && child.as<NodeGraph>()) {
            if (err) {
              *err += "DEBUG: Found NodeGraph by type matching\n";
            }
            const NodeGraph* ng = child.as<NodeGraph>();

            // Extract target name from path
            size_t last_slash = current_prim_part.rfind('/');
            std::string target_name = (last_slash != std::string::npos)
                ? current_prim_part.substr(last_slash + 1)
                : current_prim_part;

            // If target_name matches the NodeGraph name OR is "NodeGraph" itself
            if (target_name == child.element_name() || target_name == "NodeGraph") {
              // The path points to the NodeGraph itself
              // Set current_prim to the NodeGraph's prim so we can handle it below
              if (err) {
                *err += "DEBUG: Path points to NodeGraph itself, setting current_prim\n";
              }
              current_prim = &child;
              break;
            }

            // Found NodeGraph, look for the target node in NodeGraph children
            if (err) {
              *err += "DEBUG: Looking for '" + target_name + "' in NodeGraph with " + std::to_string(child.children().size()) + " children\n";
            }
            for (const auto& ng_child : child.children()) {
              if (err) {
                *err += "DEBUG: NodeGraph child: '" + ng_child.element_name() + "'\n";
              }
              if (ng_child.element_name() == target_name) {
                if (err) {
                  *err += "DEBUG: Found target in NodeGraph children\n";
                }
                current_prim = &ng_child;
                break;
              }
            }
            (void)ng;  // suppress unused variable warning
            if (current_prim) break;
          }
        }
      }
    }

    if (!current_prim) {
      // Can't find the prim, stop traversal
      break;
    }

    // Check if it's a Shader
    const Shader *shader = current_prim->as<Shader>();
    if (err) {
      *err += "DEBUG: Checking prim type: type_name='" + current_prim->type_name() + "' is_shader=" + (shader ? "true" : "false") + "\n";
    }
    if (!shader) {
      // Not a shader, might be a NodeGraph - try to follow its output
      if (const NodeGraph *ng = current_prim->as<NodeGraph>()) {
        // Get the output property specified in the connection
        std::string prop_part = current_path.prop_part();
        if (err) {
          std::string props_list;
          for (const auto& kv : ng->props) {
            props_list += " '" + kv.first + "'";
          }
          *err += "DEBUG NodeGraph props:" + props_list + ", looking for: '" + prop_part + "'\n";
        }
        auto it = ng->props.find(prop_part);
        if (it != ng->props.end() && it->second.is_attribute()) {
          const Attribute &attr = it->second.get_attribute();
          if (attr.has_connections()) {
            const auto &conns = attr.connections();
            if (!conns.empty()) {
              if (err) {
                *err += "DEBUG: Found property, following connection to: " + conns[0].full_path_name() + "\n";
              }
              current_path = conns[0];
              continue;
            }
          }
        } else {
          if (err) {
            *err += "DEBUG: Property '" + prop_part + "' not found in ng->props\n";
          }
        }
      }
      break;
    }

    // Check for specific MaterialX node types
    const std::string &node_type = shader->info_id;
    if (err) {
      *err += "DEBUG: Shader info_id='" + node_type + "'\n";
    }

    // For generic shaders (MaterialX nodes), properties are stored in ShaderNode inside shader->value
    // We need to get the correct props map
    const std::map<std::string, Property> *shader_props = &shader->props;
    if (const ShaderNode *shader_node = shader->value.as<ShaderNode>()) {
      shader_props = &shader_node->props;
      DCOUT("Using ShaderNode props (size=" << shader_props->size() << ")");
    }

    if (node_type == "ND_normalmap_float" || node_type == "ND_normalmap") {
      info.has_normal_map = true;
      DCOUT("Found ND_normalmap shader, props=" << shader_props->size());

      // Extract scale input
      auto scale_it = shader_props->find("inputs:scale");
      if (scale_it != shader_props->end() && scale_it->second.is_attribute()) {
        const Attribute &scale_attr = scale_it->second.get_attribute();
        if (auto scale_val = scale_attr.get_value<float>()) {
          info.normal_map_scale = scale_val.value();
        }
      }

      // Follow inputs:in to find the texture
      auto in_it = shader_props->find("inputs:in");
      if (in_it != shader_props->end() && in_it->second.is_attribute()) {
        const Attribute &in_attr = in_it->second.get_attribute();
        if (in_attr.has_connections()) {
          current_path = in_attr.connections()[0];
          DCOUT("Following inputs:in connection to: " << current_path.full_path_name());
          continue;
        }
      }
      DCOUT("No inputs:in connection, breaking");
      break;  // No more connections to follow
    } else if (node_type == "ND_rotate3d_vector3") {
      info.has_tangent_rotation = true;

      // Extract amount input (rotation angle in degrees)
      auto amount_it = shader_props->find("inputs:amount");
      if (amount_it != shader_props->end() && amount_it->second.is_attribute()) {
        const Attribute &amount_attr = amount_it->second.get_attribute();
        if (auto amount_val = amount_attr.get_value<float>()) {
          info.tangent_rotation = amount_val.value();
        }
      }

      // Follow inputs:in to continue traversal
      auto in_it = shader_props->find("inputs:in");
      if (in_it != shader_props->end() && in_it->second.is_attribute()) {
        const Attribute &in_attr = in_it->second.get_attribute();
        if (in_attr.has_connections()) {
          current_path = in_attr.connections()[0];
          continue;
        }
      }
    } else if (node_type == "ND_image_vector3" || node_type == "ND_image_vector4" ||
               node_type == "ND_image_color3" || node_type == "ND_image_color4") {
      // Found the texture node - extract file path
      DCOUT("Found ND_image node, props=" << shader_props->size());
      auto file_it = shader_props->find("inputs:file");
      if (file_it != shader_props->end() && file_it->second.is_attribute()) {
        const Attribute &file_attr = file_it->second.get_attribute();
        if (auto asset_val = file_attr.get_value<value::AssetPath>()) {
          info.normal_map_texture = asset_val.value().GetAssetPath();
          DCOUT("Found normal_map_texture: " << info.normal_map_texture);
        }
      }
      break;  // End of chain
    } else if (node_type == "ND_normalize_vector3" ||
               node_type == "ND_convert_vector4_vector3" ||
               node_type == "ND_convert_color4_vector3") {
      // Conversion/normalization nodes - follow inputs:in
      auto in_it = shader_props->find("inputs:in");
      if (in_it != shader_props->end() && in_it->second.is_attribute()) {
        const Attribute &in_attr = in_it->second.get_attribute();
        if (in_attr.has_connections()) {
          current_path = in_attr.connections()[0];
          continue;
        }
      }
      break;
    } else if (node_type.find("ND_geompropvalue_") == 0) {
      // GeomPropValue node - reads an arbitrary primvar from mesh geometry.
      // Extract the primvar name from inputs:geomprop.
      auto geom_it = shader_props->find("inputs:geomprop");
      if (geom_it != shader_props->end() && geom_it->second.is_attribute()) {
        const Attribute &geom_attr = geom_it->second.get_attribute();
        if (geom_attr.has_value()) {
          auto geom_val = geom_attr.get_value<std::string>();
          if (geom_val) {
            info.geomprop_name = *geom_val;
            info.has_geomprop = true;
          }
        }
      }
      break;  // Terminal node
    } else if (node_type == "ND_tangent_vector3" || node_type == "ND_normal_vector3" ||
               node_type == "ND_position_vector3" || node_type == "ND_geomcolor_color3" ||
               node_type == "ND_geomcolor_color4" || node_type == "ND_bitangent_vector3" ||
               node_type == "ND_viewdirection_vector3") {
      // Geometry nodes - end of chain (no input connections)
      break;
    } else if (node_type == "ND_texcoord_vector2" || node_type == "ND_texcoord_vector3") {
      // Texcoord node - extract inputs:index for UV set selection
      auto idx_it = shader_props->find("inputs:index");
      if (idx_it != shader_props->end() && idx_it->second.is_attribute()) {
        const Attribute &idx_attr = idx_it->second.get_attribute();
        if (idx_attr.has_value()) {
          auto idx_val = idx_attr.get_value<int>();
          if (idx_val) {
            info.texcoord_index = *idx_val;
          }
        }
      }
      break;
    //
    // Unary operations (single input: inputs:in)
    // These nodes process a single input value
    //
    } else if (node_type.find("ND_sqrt_") == 0 ||
               node_type.find("ND_absval_") == 0 ||
               node_type.find("ND_sign_") == 0 ||
               node_type.find("ND_floor_") == 0 ||
               node_type.find("ND_ceil_") == 0 ||
               node_type.find("ND_round_") == 0 ||
               node_type.find("ND_sin_") == 0 ||
               node_type.find("ND_cos_") == 0 ||
               node_type.find("ND_tan_") == 0 ||
               node_type.find("ND_asin_") == 0 ||
               node_type.find("ND_acos_") == 0 ||
               node_type.find("ND_atan_") == 0 ||
               node_type.find("ND_exp_") == 0 ||
               node_type.find("ND_ln_") == 0 ||
               node_type.find("ND_log2_") == 0 ||
               node_type.find("ND_magnitude_") == 0 ||
               node_type.find("ND_luminance_") == 0 ||
               node_type.find("ND_normalize_") == 0 ||
               node_type.find("ND_invert_") == 0 ||
               node_type.find("ND_saturate_") == 0 ||
               node_type.find("ND_hueshift_") == 0) {
      // Unary operations - follow inputs:in
      auto in_it = shader_props->find("inputs:in");
      if (in_it != shader_props->end() && in_it->second.is_attribute()) {
        const Attribute &in_attr = in_it->second.get_attribute();
        if (in_attr.has_connections()) {
          current_path = in_attr.connections()[0];
          continue;
        }
      }
      break;
    //
    // Binary operations (two inputs: inputs:in1, inputs:in2)
    // Typically follow in1 for texture chains
    //
    } else if (node_type.find("ND_add_") == 0 ||
               node_type.find("ND_subtract_") == 0 ||
               node_type.find("ND_multiply_") == 0 ||
               node_type.find("ND_divide_") == 0 ||
               node_type.find("ND_power_") == 0 ||
               node_type.find("ND_min_") == 0 ||
               node_type.find("ND_max_") == 0 ||
               node_type.find("ND_modulo_") == 0 ||
               node_type.find("ND_atan2_") == 0 ||
               node_type.find("ND_dotproduct_") == 0 ||
               node_type.find("ND_crossproduct_") == 0) {
      // Binary operations - prefer following the input with a connection (likely leads to texture).
      // Try in1 first, fall back to in2. If neither has connections, break normally.
      auto in1_it = shader_props->find("inputs:in1");
      if (in1_it != shader_props->end() && in1_it->second.is_attribute()) {
        const Attribute &in1_attr = in1_it->second.get_attribute();
        if (in1_attr.has_connections()) {
          current_path = in1_attr.connections()[0];
          continue;
        }
      }
      // If in1 has no connection, try in2
      auto in2_it = shader_props->find("inputs:in2");
      if (in2_it != shader_props->end() && in2_it->second.is_attribute()) {
        const Attribute &in2_attr = in2_it->second.get_attribute();
        if (in2_attr.has_connections()) {
          current_path = in2_attr.connections()[0];
          continue;
        }
      }
      break;
    //
    // Mix/blend operations (fg, bg, mix inputs)
    // Follow fg (foreground) as it typically has the texture
    //
    } else if (node_type.find("ND_mix_") == 0) {
      // Mix nodes - follow inputs:fg (foreground typically has the texture)
      auto fg_it = shader_props->find("inputs:fg");
      if (fg_it != shader_props->end() && fg_it->second.is_attribute()) {
        const Attribute &fg_attr = fg_it->second.get_attribute();
        if (fg_attr.has_connections()) {
          current_path = fg_attr.connections()[0];
          continue;
        }
      }
      // If fg has no connection, try bg
      auto bg_it = shader_props->find("inputs:bg");
      if (bg_it != shader_props->end() && bg_it->second.is_attribute()) {
        const Attribute &bg_attr = bg_it->second.get_attribute();
        if (bg_attr.has_connections()) {
          current_path = bg_attr.connections()[0];
          continue;
        }
      }
      break;
    //
    // Clamp operation (in, low, high inputs)
    //
    } else if (node_type.find("ND_clamp_") == 0) {
      // Clamp nodes - follow inputs:in
      auto in_it = shader_props->find("inputs:in");
      if (in_it != shader_props->end() && in_it->second.is_attribute()) {
        const Attribute &in_attr = in_it->second.get_attribute();
        if (in_attr.has_connections()) {
          current_path = in_attr.connections()[0];
          continue;
        }
      }
      break;
    //
    // Remap/range operations (in, inlow, inhigh, outlow, outhigh inputs)
    //
    } else if (node_type.find("ND_remap_") == 0 ||
               node_type.find("ND_range_") == 0) {
      // Remap/range nodes - follow inputs:in
      auto in_it = shader_props->find("inputs:in");
      if (in_it != shader_props->end() && in_it->second.is_attribute()) {
        const Attribute &in_attr = in_it->second.get_attribute();
        if (in_attr.has_connections()) {
          current_path = in_attr.connections()[0];
          continue;
        }
      }
      break;
    //
    // Extract operations (extracts component from color3/vector3)
    //
    } else if (node_type.find("ND_extract_") == 0) {
      // Extract nodes - follow inputs:in
      auto in_it = shader_props->find("inputs:in");
      if (in_it != shader_props->end() && in_it->second.is_attribute()) {
        const Attribute &in_attr = in_it->second.get_attribute();
        if (in_attr.has_connections()) {
          current_path = in_attr.connections()[0];
          continue;
        }
      }
      break;
    //
    // HSV/color adjustment operations
    //
    } else if (node_type.find("ND_hsvadjust_") == 0 ||
               node_type.find("ND_rgbtohsv_") == 0 ||
               node_type.find("ND_hsvtorgb_") == 0) {
      // HSV adjust - follow inputs:in
      auto in_it = shader_props->find("inputs:in");
      if (in_it != shader_props->end() && in_it->second.is_attribute()) {
        const Attribute &in_attr = in_it->second.get_attribute();
        if (in_attr.has_connections()) {
          current_path = in_attr.connections()[0];
          continue;
        }
      }
      break;
    //
    // Type conversion operations (convert color3 to vector3 etc.)
    //
    } else if (node_type.find("ND_convert_") == 0) {
      // Conversion nodes - follow inputs:in
      auto in_it = shader_props->find("inputs:in");
      if (in_it != shader_props->end() && in_it->second.is_attribute()) {
        const Attribute &in_attr = in_it->second.get_attribute();
        if (in_attr.has_connections()) {
          current_path = in_attr.connections()[0];
          continue;
        }
      }
      break;
    //
    // Combine operations (combines in1, in2, in3 to color3/vector3)
    // Terminal nodes - they produce values from scalars
    //
    } else if (node_type.find("ND_separate3_") == 0 ||
               node_type.find("ND_separate2_") == 0 ||
               node_type.find("ND_separate4_") == 0) {
      // Separate (multi-output) nodes - split vector into components.
      // Outputs: outr, outg, outb (for separate3), outx, outy (for separate2), etc.
      // Follow inputs:in to continue traversal.
      auto in_it = shader_props->find("inputs:in");
      if (in_it != shader_props->end() && in_it->second.is_attribute()) {
        const Attribute &in_attr = in_it->second.get_attribute();
        if (in_attr.has_connections()) {
          current_path = in_attr.connections()[0];
          continue;
        }
      }
      break;
    } else if (node_type.find("ND_combine3_") == 0 ||
               node_type.find("ND_combine2_") == 0 ||
               node_type.find("ND_combine4_") == 0) {
      // Combine nodes - terminal (produce vector from components)
      // Could follow inputs if needed, but typically these are terminals
      break;
    //
    // Constant nodes - terminal (provide constant values)
    //
    } else if (node_type.find("ND_constant_") == 0) {
      // Constant nodes - terminal, extract the constant value
      auto val_it = shader_props->find("inputs:value");
      if (val_it != shader_props->end() && val_it->second.is_attribute()) {
        const Attribute &val_attr = val_it->second.get_attribute();
        std::array<float, 4> cv = {{0.0f, 0.0f, 0.0f, 0.0f}};
        if (auto vf = val_attr.get_value<float>()) {
          cv[0] = *vf;
          info.constant_components = 1;
          info.has_constant = true;
        } else if (auto vf3 = val_attr.get_value<value::float3>()) {
          cv[0] = (*vf3)[0]; cv[1] = (*vf3)[1]; cv[2] = (*vf3)[2];
          info.constant_components = 3;
          info.has_constant = true;
        } else if (auto vc3 = val_attr.get_value<value::color3f>()) {
          cv[0] = (*vc3)[0]; cv[1] = (*vc3)[1]; cv[2] = (*vc3)[2];
          info.constant_components = 3;
          info.has_constant = true;
        } else if (auto vf2 = val_attr.get_value<value::float2>()) {
          cv[0] = (*vf2)[0]; cv[1] = (*vf2)[1];
          info.constant_components = 2;
          info.has_constant = true;
        } else if (auto vc4 = val_attr.get_value<value::color4f>()) {
          cv[0] = (*vc4)[0]; cv[1] = (*vc4)[1]; cv[2] = (*vc4)[2]; cv[3] = (*vc4)[3];
          info.constant_components = 4;
          info.has_constant = true;
        } else if (auto vf4 = val_attr.get_value<value::float4>()) {
          cv[0] = (*vf4)[0]; cv[1] = (*vf4)[1]; cv[2] = (*vf4)[2]; cv[3] = (*vf4)[3];
          info.constant_components = 4;
          info.has_constant = true;
        }
        if (info.has_constant) {
          info.constant_value = cv;
        }
      }
      break;
    //
    // Tiledimage/image nodes (texture sampling)
    //
    } else if (node_type.find("ND_tiledimage_") == 0) {
      // Tiled image node - extract file path, uvtiling, uvoffset
      auto file_it = shader_props->find("inputs:file");
      if (file_it != shader_props->end() && file_it->second.is_attribute()) {
        const Attribute &file_attr = file_it->second.get_attribute();
        if (auto asset_val = file_attr.get_value<value::AssetPath>()) {
          info.normal_map_texture = asset_val.value().GetAssetPath();
        }
      }
      // Extract UV tiling (scale)
      auto tiling_it = shader_props->find("inputs:uvtiling");
      if (tiling_it != shader_props->end() && tiling_it->second.is_attribute()) {
        const Attribute &tiling_attr = tiling_it->second.get_attribute();
        if (auto tiling_val = tiling_attr.get_value<value::float2>()) {
          info.uvtiling[0] = (*tiling_val)[0];
          info.uvtiling[1] = (*tiling_val)[1];
          info.has_uvtransform = true;
        }
      }
      // Extract UV offset
      auto offset_it = shader_props->find("inputs:uvoffset");
      if (offset_it != shader_props->end() && offset_it->second.is_attribute()) {
        const Attribute &offset_attr = offset_it->second.get_attribute();
        if (auto offset_val = offset_attr.get_value<value::float2>()) {
          info.uvoffset[0] = (*offset_val)[0];
          info.uvoffset[1] = (*offset_val)[1];
          info.has_uvtransform = true;
        }
      }
      break;  // End of chain
    //
    // Swizzle operations
    //
    } else if (node_type.find("ND_swizzle_") == 0) {
      // Swizzle - follow inputs:in
      auto in_it = shader_props->find("inputs:in");
      if (in_it != shader_props->end() && in_it->second.is_attribute()) {
        const Attribute &in_attr = in_it->second.get_attribute();
        if (in_attr.has_connections()) {
          current_path = in_attr.connections()[0];
          continue;
        }
      }
      break;
    //
    // Ifgreater/ifless/ifequal conditional operations
    //
    } else if (node_type.find("ND_ifgreater_") == 0 ||
               node_type.find("ND_ifgreatereq_") == 0 ||
               node_type.find("ND_ifless_") == 0 ||
               node_type.find("ND_iflesseq_") == 0 ||
               node_type.find("ND_ifequal_") == 0) {
      // Conditional - follow inputs:in1 (value1)
      auto in1_it = shader_props->find("inputs:in1");
      if (in1_it != shader_props->end() && in1_it->second.is_attribute()) {
        const Attribute &in1_attr = in1_it->second.get_attribute();
        if (in1_attr.has_connections()) {
          current_path = in1_attr.connections()[0];
          continue;
        }
      }
      break;
    //
    // Noise operations
    //
    } else if (node_type.find("ND_noise2d_") == 0 ||
               node_type.find("ND_noise3d_") == 0 ||
               node_type.find("ND_cellnoise2d_") == 0 ||
               node_type.find("ND_cellnoise3d_") == 0 ||
               node_type.find("ND_worleynoise2d_") == 0 ||
               node_type.find("ND_worleynoise3d_") == 0 ||
               node_type.find("ND_fractal3d_") == 0) {
      // Noise nodes - terminal (procedural generation)
      break;
    //
    // Place2d texture coordinate transformation
    //
    } else if (node_type.find("ND_place2d_") == 0) {
      // Place2d - follow inputs:texcoord if connected
      auto texcoord_it = shader_props->find("inputs:texcoord");
      if (texcoord_it != shader_props->end() && texcoord_it->second.is_attribute()) {
        const Attribute &texcoord_attr = texcoord_it->second.get_attribute();
        if (texcoord_attr.has_connections()) {
          current_path = texcoord_attr.connections()[0];
          continue;
        }
      }
      break;
    } else {
      // Unknown node type, try to follow inputs:in if it exists
      auto in_it = shader_props->find("inputs:in");
      if (in_it != shader_props->end() && in_it->second.is_attribute()) {
        const Attribute &in_attr = in_it->second.get_attribute();
        if (in_attr.has_connections()) {
          current_path = in_attr.connections()[0];
          continue;
        }
      }
      // Also try inputs:in1 for binary-style nodes
      auto in1_it = shader_props->find("inputs:in1");
      if (in1_it != shader_props->end() && in1_it->second.is_attribute()) {
        const Attribute &in1_attr = in1_it->second.get_attribute();
        if (in1_attr.has_connections()) {
          current_path = in1_attr.connections()[0];
          continue;
        }
      }
      break;
    }
  }

  return info;
}


}  // namespace

struct UVConnectionResolveCacheEntry {
  bool found{false};
  Path tex_abs_path;
  const UsdUVTexture *texture{nullptr};
  const Shader *shader{nullptr};
};

struct MtlxConnectionResolveCacheEntry {
  Path tex_abs_path;
  const Shader *image_shader{nullptr};
  std::string st_varname;
  const AssetInfo *asset_info{nullptr};
};

struct ConnectionResolveCache {
  const Stage *stage{nullptr};
  tinyusdz::HashMap<std::string, UVConnectionResolveCacheEntry,
                    FNV1StringHash>
      uv_texture_by_connection;
  tinyusdz::HashMap<std::string, MtlxConnectionResolveCacheEntry,
                    FNV1StringHash>
      mtlx_texture_by_connection;
};

static ConnectionResolveCache &GetConnectionResolveCache(const Stage &stage) {
  static thread_local ConnectionResolveCache cache;
  if (cache.stage != &stage) {
    cache.stage = &stage;
    cache.uv_texture_by_connection.clear();
    cache.mtlx_texture_by_connection.clear();
  }
  return cache;
}

void ResetConnectionResolveCache(const Stage &stage) {
  ConnectionResolveCache &cache = GetConnectionResolveCache(stage);
  // Swap with empty maps to release bucket memory (clear() keeps capacity)
  {
    tinyusdz::HashMap<std::string, UVConnectionResolveCacheEntry, FNV1StringHash> tmp;
    cache.uv_texture_by_connection.swap(tmp);
  }
  {
    tinyusdz::HashMap<std::string, MtlxConnectionResolveCacheEntry, FNV1StringHash> tmp;
    cache.mtlx_texture_by_connection.swap(tmp);
  }
}

namespace {

std::vector<const tinyusdz::GeomSubset *> GetMaterialBindGeomSubsets(
    const tinyusdz::Prim &prim) {
  std::vector<const tinyusdz::GeomSubset *> dst;

  // GeomSubet Prim must be a child Prim of GeomMesh.
  for (const auto &child : prim.children()) {
    if (const tinyusdz::GeomSubset *psubset =
            child.as<tinyusdz::GeomSubset>()) {
      value::token tok;
      if (!psubset->familyName.get_value(&tok)) {
        continue;
      }

      if (tok.str() != "materialBind") {
        continue;
      }

      dst.push_back(psubset);
    }
  }

  return dst;
}

/// Try to read array attribute directly from mmap. Returns true if successful.

// Convert UsdTransform2d -> PrimvarReader_float2 shader network.
nonstd::expected<bool, std::string> ConvertTexTransform2d(
    const Stage &stage, const Path &tx_abs_path, const UsdTransform2d &tx,
    UVTexture *tex_out, double timecode) {
  float rotation;  // in angles
  std::string resolve_err;
  if (!ResolveTypedAnimatableValue(stage, tx.rotation, "inputs:rotation",
                                   timecode,
                                   value::TimeSampleInterpolationType::Held,
                                   &rotation, &resolve_err)) {
    return nonstd::make_unexpected(
        fmt::format("Failed to resolve rotation attribute from {}: {}",
                    tx_abs_path.full_path_name(), resolve_err));
  }

  value::float2 scale;
  resolve_err.clear();
  if (!ResolveTypedAnimatableValue(stage, tx.scale, "inputs:scale", timecode,
                                   value::TimeSampleInterpolationType::Held,
                                   &scale, &resolve_err)) {
    return nonstd::make_unexpected(
        fmt::format("Failed to resolve scale attribute from {}: {}",
                    tx_abs_path.full_path_name(), resolve_err));
  }

  value::float2 translation;
  resolve_err.clear();
  if (!ResolveTypedAnimatableValue(stage, tx.translation, "inputs:translation",
                                   timecode,
                                   value::TimeSampleInterpolationType::Held,
                                   &translation, &resolve_err)) {
    return nonstd::make_unexpected(
        fmt::format("Failed to resolve translation attribute from {}: {}",
                    tx_abs_path.full_path_name(), resolve_err));
  }

  // must be authored and connected to PrimvarReader.
  if (!tx.in.authored()) {
    return nonstd::make_unexpected("`inputs:in` must be authored.\n");
  }

  if (!tx.in.is_connection()) {
    return nonstd::make_unexpected("`inputs:in` must be a connection.\n");
  }

  const auto &paths = tx.in.get_connections();
  if (paths.size() != 1) {
    return nonstd::make_unexpected(
        "`inputs:in` must be a single connection Path.\n");
  }

  std::string prim_part = paths[0].prim_part();
  std::string prop_part = paths[0].prop_part();

  if (prop_part != "outputs:result") {
    return nonstd::make_unexpected(
        "`inputs:in` connection Path's property part must be "
        "`outputs:result`\n");
  }

  std::string err;

  const Prim *pprim{nullptr};
  if (!stage.find_prim_at_path(Path(prim_part, ""), pprim, &err)) {
    return nonstd::make_unexpected(fmt::format(
        "`inputs:in` connection Path not found in the Stage. {}\n", prim_part));
  }

  if (!pprim) {
    return nonstd::make_unexpected(
        fmt::format("[InternalError] Prim is nullptr: {}\n", prim_part));
  }

  const Shader *pshader = pprim->as<Shader>();
  if (!pshader) {
    return nonstd::make_unexpected(
        fmt::format("{} must be Shader Prim, but got {}\n", prim_part,
                    pprim->prim_type_name()));
  }

  const UsdPrimvarReader_float2 *preader =
      pshader->value.as<UsdPrimvarReader_float2>();
  if (!preader) {
    return nonstd::make_unexpected(fmt::format(
        "Shader {} must be UsdPrimvarReader_float2 type, but got {}(internal type {})\n",
        prim_part, pshader->info_id, pshader->value.type_name()));
  }

  // Get value producing attribute(i.e, follow .connection and return
  // terminal Attribute value)
  //value::token varname;

  // 'string' for inputs:varname preferred.
  std::string varname;
  TerminalAttributeValue attr;
  if (!tydra::EvaluateAttribute(stage, *pprim, "inputs:varname", &attr, &err)) {
    return nonstd::make_unexpected(
        "`inputs:varname` evaluation failed: " + err + "\n");
  }
  if (auto pvt = attr.as<value::token>()) {
    varname = pvt->str();
  } else if (auto pvs = attr.as<std::string>()) {
    varname = *pvs;
  } else if (auto pvsd = attr.as<value::StringData>()) {
    varname = (*pvsd).value;
  } else {
    return nonstd::make_unexpected(
        "`inputs:varname` must be `token` or `string` type, but got " + attr.type_name() +
        "\n");
  }
  if (varname.empty()) {
    return nonstd::make_unexpected("`inputs:varname` is empty token\n");
  }
  DCOUT("inputs:varname = " << varname);

  // Build transform matrix.
  // https://github.com/KhronosGroup/glTF/tree/main/extensions/2.0/Khronos/KHR_texture_transform
  // Since USD uses post-multiply,
  //
  // matrix = scale * rotate * translate
  //
  {
    mat3 s;
    s.set_scale(scale[0], scale[1], 1.0f);

    mat3 r = mat3::identity();

    r.m[0][0] = std::cos(math::radian(rotation));
    r.m[0][1] = std::sin(math::radian(rotation));

    r.m[1][0] = -std::sin(math::radian(rotation));
    r.m[1][1] = std::cos(math::radian(rotation));

    mat3 t = mat3::identity();
    t.set_translation(translation[0], translation[1], 1.0f);

    tex_out->transform = s * r * t;
  }

  tex_out->tx_rotation = rotation;
  tex_out->tx_translation = translation;
  tex_out->tx_scale = scale;
  tex_out->has_transform2d = true;

  tex_out->varname_uv = varname;

  return true;
}

template <typename T>
nonstd::expected<bool, std::string> GetConnectedUVTexture(
    const Stage &stage, const TypedAnimatableAttributeWithFallback<T> &src,
    Path *tex_abs_path, const UsdUVTexture **dst, const Shader **shader_out) {
  if (!dst) {
    return nonstd::make_unexpected("[InternalError] dst is nullptr.\n");
  }

  if (!src.is_connection()) {
    return nonstd::make_unexpected("Attribute must be connection.\n");
  }

  if (src.get_connections().size() != 1) {
    return nonstd::make_unexpected(
        "Attribute connections must be single connection Path.\n");
  }

  //
  // Example: color3f inputs:diffuseColor.connect = </path/to/tex.outputs:rgb>
  //
  // => path.prim_part : /path/to/tex
  // => path.prop_part : outputs:rgb
  //

  const Path &path = src.get_connections()[0];

  const std::string prim_part = path.prim_part();
  const std::string prop_part = path.prop_part();
  const std::string cache_key = path.full_path_name();
  ConnectionResolveCache &resolve_cache = GetConnectionResolveCache(stage);

  if (shader_out) {
    *shader_out = nullptr;
  }
  *dst = nullptr;

  auto cache_it = resolve_cache.uv_texture_by_connection.find(cache_key);
  if (cache_it != resolve_cache.uv_texture_by_connection.end()) {
    if (tex_abs_path) {
      *tex_abs_path = cache_it->second.tex_abs_path;
    }
    *dst = cache_it->second.texture;
    if (shader_out) {
      *shader_out = cache_it->second.shader;
    }
    return cache_it->second.found;
  }

  auto cache_result = [&](bool found, const Path &resolved_path,
                          const UsdUVTexture *texture,
                          const Shader *shader) {
    UVConnectionResolveCacheEntry entry;
    entry.found = found;
    entry.tex_abs_path = resolved_path;
    entry.texture = texture;
    entry.shader = shader;
    resolve_cache.uv_texture_by_connection[cache_key] = std::move(entry);
  };

  // NOTE: no `outputs:rgba` in the spec.
  constexpr auto kOutputsRGB = "outputs:rgb";
  constexpr auto kOutputsR = "outputs:r";
  constexpr auto kOutputsG = "outputs:g";
  constexpr auto kOutputsB = "outputs:b";
  constexpr auto kOutputsA = "outputs:a";

  TUSDZ_LOG_I("path: " << path);

  // Check if prop_part is a standard UsdUVTexture output
  bool is_standard_output = (prop_part == kOutputsRGB) ||
                            (prop_part == kOutputsR) ||
                            (prop_part == kOutputsG) ||
                            (prop_part == kOutputsB) ||
                            (prop_part == kOutputsA);

  const Prim *prim{nullptr};
  std::string err;
  bool found_in_stage = stage.find_prim_at_path(Path(prim_part, ""), prim, &err);

  // If not found in stage lookup, try to navigate through Material's children
  // This handles the case where NodeGraph is a child of Material but not in the Stage index
  if (!found_in_stage || !prim) {
    DCOUT("Prim not found in stage lookup, trying Material children approach");

    // Extract Material path - it should be everything before the last element
    size_t last_slash = prim_part.rfind('/');
    if (last_slash == std::string::npos) {
      return nonstd::make_unexpected(
          fmt::format("Prim {} not found in the Stage: {}\n", prim_part, err));
    }

    std::string material_path = prim_part.substr(0, last_slash);
    std::string child_name = prim_part.substr(last_slash + 1);

    DCOUT("Looking for Material at: " << material_path);
    DCOUT("Child name: " << child_name);

    // Find the Material
    const Prim *mat_prim{nullptr};
    if (!stage.find_prim_at_path(Path(material_path, ""), mat_prim, &err)) {
      return nonstd::make_unexpected(
          fmt::format("Prim {} not found (material lookup also failed): {}\n", prim_part, err));
    }

    // Look for child prim
    if (mat_prim) {
      std::string children_info = "Material has " + std::to_string(mat_prim->children().size()) + " children: ";
      for (const auto& child : mat_prim->children()) {
        std::string elem_name = child.element_name();
        std::string child_type = child.data().type_name();
        children_info += "'" + elem_name + "'(" + child_type + ") ";

        // Check by name match
        if (elem_name == child_name) {
          prim = &child;
          break;
        }
        // Also check if it's a NodeGraph/Shader by type name
        // This handles cases where element_name might not be set properly
        // e.g., looking for "NodeGraphs" and finding type "NodeGraph" with empty name
        if (child_type == "NodeGraph" && (child_name == "NodeGraphs" || child_name == "NodeGraph")) {
          prim = &child;
          break;
        }
        if (child_type == "Shader" && child_name == "Shader") {
          prim = &child;
          break;
        }
      }

      if (!prim) {
        DCOUT(children_info);
        return nonstd::make_unexpected(
            fmt::format("Child prim '{}' not found in Material {}. {}\n", child_name, material_path, children_info));
      }
    } else {
      return nonstd::make_unexpected(
          fmt::format("Material prim {} is null\n", material_path));
    }
  }

  if (!prim) {
    return nonstd::make_unexpected("[InternalError] Prim ptr is null.\n");
  }

  // Check if this is a NodeGraph - if so, we need to traverse through it
  if (const NodeGraph *ng = prim->as<NodeGraph>()) {
    DCOUT("Connection goes through NodeGraph: " << prim_part);

    // Look for the output property in the NodeGraph's props
    const auto &props = ng->props;
    auto it = props.find(prop_part);
    if (it == props.end()) {
      return nonstd::make_unexpected(
          fmt::format("NodeGraph {} does not have output property {}", prim_part, prop_part));
    }

    const Property &output_prop = it->second;
    if (!output_prop.is_attribute()) {
      return nonstd::make_unexpected(
          fmt::format("NodeGraph output {} is not an attribute", prop_part));
    }

    const Attribute &output_attr = output_prop.get_attribute();
    if (!output_attr.has_connections()) {
      return nonstd::make_unexpected(
          fmt::format("NodeGraph output {} has no connections", prop_part));
    }

    // Get the connection from the NodeGraph output
    const auto &output_conns = output_attr.connections();
    if (output_conns.size() != 1) {
      return nonstd::make_unexpected(
          fmt::format("NodeGraph output {} must have exactly one connection, got {}",
                      prop_part, output_conns.size()));
    }

    const Path &ng_output_path = output_conns[0];
    DCOUT("NodeGraph output connects to: " << ng_output_path);

    // Recursively follow the connection through the NodeGraph
    // We need to traverse to the next node in the chain
    std::string next_prim_part = ng_output_path.prim_part();
    std::string next_prop_part = ng_output_path.prop_part();

    // Find the next prim in the chain
    // It might be a child of the NodeGraph, so use the same child lookup logic
    const Prim *next_prim{nullptr};
    bool found_next = stage.find_prim_at_path(Path(next_prim_part, ""), next_prim, &err);

    // If not found in stage, it might be a child of the current NodeGraph
    if (!found_next || !next_prim) {
      DCOUT("Next prim not found in stage, checking NodeGraph children");

      // Check if it's a child of this NodeGraph
      size_t last_slash = next_prim_part.rfind('/');
      if (last_slash != std::string::npos) {
        std::string parent_path = next_prim_part.substr(0, last_slash);
        std::string child_name = next_prim_part.substr(last_slash + 1);

        // If the parent is this NodeGraph, look in its children
        if (parent_path == prim_part) {
          for (const auto& child : prim->children()) {
            std::string elem_name = child.element_name();
            if (elem_name == child_name) {
              next_prim = &child;
              break;
            }
          }

          if (!next_prim) {
            return nonstd::make_unexpected(
                fmt::format("Child prim '{}' not found in NodeGraph {}\n", child_name, prim_part));
          }
        } else {
          return nonstd::make_unexpected(
              fmt::format("Prim {} not found in the Stage: {}\n", next_prim_part, err));
        }
      } else {
        return nonstd::make_unexpected(
            fmt::format("Prim {} not found in the Stage: {}\n", next_prim_part, err));
      }
    }

    if (!next_prim) {
      return nonstd::make_unexpected("[InternalError] next_prim is null.\n");
    }

    // For nested NodeGraphs or other intermediate nodes, we would need to continue traversing
    // For now, we only support the common pattern: NodeGraph -> Shader(UsdUVTexture)
    // Nested NodeGraphs are rare and can be handled if needed

    // Check if it's a Shader with UsdUVTexture
    if (const Shader *pshader = next_prim->as<Shader>()) {
      if (const UsdUVTexture *ptex = pshader->value.as<UsdUVTexture>()) {
        // Verify the property part is valid for UsdUVTexture
        if (next_prop_part != kOutputsRGB && next_prop_part != kOutputsR &&
            next_prop_part != kOutputsG && next_prop_part != kOutputsB &&
            next_prop_part != kOutputsA) {
          return nonstd::make_unexpected(fmt::format(
              "UsdUVTexture connection property part must be outputs:rgb/r/g/b/a, got {}",
              next_prop_part));
        }

        DCOUT("Found UsdUVTexture through NodeGraph: " << next_prim_part);
        (*dst) = ptex;

        if (shader_out) {
          (*shader_out) = pshader;
        }

        if (tex_abs_path) {
          (*tex_abs_path) = ng_output_path;
        }

        cache_result(true, ng_output_path, ptex, pshader);
        return true;
      }
      // Shader exists but it's not a UsdUVTexture - this is OK, NodeGraph might connect to other shader types
      // Return false (not found) rather than error
      DCOUT(fmt::format("NodeGraph {} output {} connects to Shader {} but it's not UsdUVTexture",
                        prim_part, prop_part, next_prim_part));
      cache_result(false, ng_output_path, nullptr, pshader);
      return false;
    }

    // If we get here, the NodeGraph doesn't connect to a UsdUVTexture
    // This is not necessarily an error - the connection might be to a MaterialX shader or other node type
    DCOUT(fmt::format("NodeGraph {} output {} connects to {} (type: {}), not a UsdUVTexture",
                      prim_part, prop_part, next_prim_part, next_prim->prim_type_name()));
    cache_result(false, ng_output_path, nullptr, nullptr);
    return false;
  }

  // Not a NodeGraph - must be a direct UsdUVTexture connection
  if (!is_standard_output) {
    return nonstd::make_unexpected(fmt::format(
        "connection Path's property part must be `{}`, `{}`, `{}`, `{}` or `{}` "
        "for UsdUVTexture, but got `{}`(prim_part: {}).",
        kOutputsRGB, kOutputsR, kOutputsG, kOutputsB, kOutputsA, prop_part, prim_part));
  }

  if (tex_abs_path) {
    (*tex_abs_path) = Path(prim_part, prop_part);
  }

  if (const Shader *pshader = prim->as<Shader>()) {
    if (const UsdUVTexture *ptex = pshader->value.as<UsdUVTexture>()) {
      DCOUT("ptex = " << ptex);
      (*dst) = ptex;

      if (shader_out) {
        (*shader_out) = pshader;
      }

      cache_result(true, Path(prim_part, prop_part), ptex, pshader);
      return true;
    }
  }

  return nonstd::make_unexpected(
      fmt::format("Prim {} must be `Shader` Prim type, but got `{}`", prim_part,
                  prim->prim_type_name()));
}

// Helper function to find ND_image_color4 texture nodes in a MaterialX NodeGraph
// by traversing connections from the given output
template <typename T>
nonstd::expected<bool, std::string> GetConnectedMtlxTexture(
    const Stage &stage, const TypedAnimatableAttributeWithFallback<T> &src,
    Path *tex_abs_path, const Shader **image_shader_out,
    std::string *st_varname_out, const AssetInfo **assetInfo_out,
    const std::string &default_texcoords_primvar_name = "st") {

  if (!src.is_connection()) {
    return nonstd::make_unexpected("Attribute must be connection.\n");
  }

  if (src.get_connections().size() != 1) {
    return nonstd::make_unexpected(
        "Attribute connections must be single connection Path.\n");
  }

  const Path &path = src.get_connections()[0];
  const std::string prim_part = path.prim_part();
  const std::string prop_part = path.prop_part();
  const std::string cache_key =
      path.full_path_name() + "|" + default_texcoords_primvar_name;
  ConnectionResolveCache &resolve_cache = GetConnectionResolveCache(stage);

  auto mtlx_cache_it =
      resolve_cache.mtlx_texture_by_connection.find(cache_key);
  if (mtlx_cache_it != resolve_cache.mtlx_texture_by_connection.end()) {
    if (tex_abs_path) {
      *tex_abs_path = mtlx_cache_it->second.tex_abs_path;
    }
    if (image_shader_out) {
      *image_shader_out = mtlx_cache_it->second.image_shader;
    }
    if (st_varname_out) {
      *st_varname_out = mtlx_cache_it->second.st_varname;
    }
    if (assetInfo_out) {
      *assetInfo_out = mtlx_cache_it->second.asset_info;
    }
    return true;
  }

  auto cache_result = [&](const Path &resolved_path, const Shader *image_shader,
                          const std::string &st_varname,
                          const AssetInfo *asset_info) {
    MtlxConnectionResolveCacheEntry entry;
    entry.tex_abs_path = resolved_path;
    entry.image_shader = image_shader;
    entry.st_varname = st_varname;
    entry.asset_info = asset_info;
    resolve_cache.mtlx_texture_by_connection[cache_key] = std::move(entry);
  };

  DCOUT("Checking MaterialX connection: " << path.full_path_name());
  DCOUT("  prim_part: " << prim_part);
  DCOUT("  prop_part: " << prop_part);

  // The prim_part should be the NodeGraph path itself
  // For </root/_materials/Material/NodeGraphs.outputs:node_out>,
  // prim_part = "/root/_materials/Material/NodeGraphs"

  // First, try to find via stage lookup
  const Prim *ng_prim{nullptr};
  std::string err;
  bool found_in_stage = stage.find_prim_at_path(Path(prim_part, ""), ng_prim, &err);

  // If not found in stage lookup, try to navigate through Material's children
  if (!found_in_stage || !ng_prim) {
    DCOUT("Prim not found in stage lookup, trying Material children approach");

    // Extract Material path - it should be everything before the last element
    size_t last_slash = prim_part.rfind('/');
    if (last_slash == std::string::npos) {
      return nonstd::make_unexpected(
          fmt::format("Invalid NodeGraph path structure: {}\n", prim_part));
    }

    std::string material_path = prim_part.substr(0, last_slash);
    std::string nodegraph_name = prim_part.substr(last_slash + 1);

    DCOUT("Looking for Material at: " << material_path);
    DCOUT("NodeGraph name: " << nodegraph_name);

    // Find the Material
    const Prim *mat_prim{nullptr};
    if (!stage.find_prim_at_path(Path(material_path, ""), mat_prim, &err)) {
      return nonstd::make_unexpected(
          fmt::format("Material {} not found: {}\n", material_path, err));
    }

    // Look for NodeGraph child
    if (mat_prim) {
      std::string children_info = "Material has " + std::to_string(mat_prim->children().size()) + " children: ";
      for (const auto& child : mat_prim->children()) {
        std::string child_name = child.element_name();
        std::string child_type = child.data().type_name();
        children_info += "'" + child_name + "'(" + child_type + ") ";

        // Check if this is a NodeGraph (by type, since name might be empty)
        if (child_type == "NodeGraph") {
          // If the child has no name but is the right type, use it
          // This handles the case where the NodeGraph doesn't have element_name set
          ng_prim = &child;
          break;
        } else if (child_name == nodegraph_name) {
          // Also check by exact name match
          ng_prim = &child;
          break;
        }
      }

      if (!ng_prim) {
        return nonstd::make_unexpected(
            fmt::format("NodeGraph '{}' not found. {}\n", nodegraph_name, children_info));
      }
    } else {
      return nonstd::make_unexpected(
          fmt::format("Material prim is null\n"));
    }
  }

  DCOUT("Found prim: " << prim_part << ", type: " << (ng_prim ? ng_prim->data().type_name() : "null"));

  const NodeGraph *ng = ng_prim ? ng_prim->as<NodeGraph>() : nullptr;
  if (!ng) {
    // Debug output to understand why it's not a NodeGraph
    if (ng_prim) {
      return nonstd::make_unexpected(
          fmt::format("{} is not a NodeGraph, prim_type: {}\n", prim_part, ng_prim->data().type_name()));
    }
    return nonstd::make_unexpected(
        fmt::format("{} is not a NodeGraph\n", prim_part));
  }

  // Find the output connection we're looking for
  // The prop_part should be like "outputs:node_out"
  std::string output_name = prop_part;
  if (startsWith(output_name, "outputs:")) {
    output_name = output_name.substr(8); // Remove "outputs:" prefix
  }

  // Look for the connection in props
  // Try both with and without ".connect" suffix
  std::string conn_prop_name = "outputs:" + output_name + ".connect";
  auto it = ng->props.find(conn_prop_name);

  if (it == ng->props.end()) {
    // Try without .connect suffix
    conn_prop_name = "outputs:" + output_name;
    it = ng->props.find(conn_prop_name);

    if (it == ng->props.end()) {
      // List available props for debugging
      std::string available_props = "Available props: ";
      for (const auto& prop : ng->props) {
        available_props += prop.first + " ";
      }
      return nonstd::make_unexpected(
          fmt::format("Output connection '{}' not found in NodeGraph. {}\n",
                      conn_prop_name, available_props));
    }
  }

  // NodeGraph outputs can be stored as attributes or relationships
  Path current_path;
  bool found_connection = false;

  if (it->second.is_attribute()) {
    // It's an attribute - look for connections on the attribute
    const Attribute &attr = it->second.get_attribute();
    if (attr.has_connections() && !attr.connections().empty()) {
      current_path = attr.connections()[0];
      found_connection = true;
    }
  } else if (it->second.is_relationship()) {
    // Also support relationship format
    auto targets = it->second.get_relationTargets();
    if (!targets.empty()) {
      current_path = targets[0];
      found_connection = true;
    }
  }

  if (!found_connection) {
    return nonstd::make_unexpected(
        fmt::format("Output {} has no connection targets\n", conn_prop_name));
  }
  const Shader *image_shader = nullptr;

  // Traverse the node connections to find ND_image_color4
  // Maximum depth to prevent infinite loops
  int max_depth = 10;
  std::string traversal_log = "Traversal: ";
  while (max_depth-- > 0) {
    std::string current_prim_part = current_path.prim_part();

    const Prim *current_prim{nullptr};

    // First, try regular stage lookup
    bool current_found_in_stage = stage.find_prim_at_path(Path(current_prim_part, ""), current_prim, &err);

    // If not found and this is under a NodeGraph, look in NodeGraph children
    if (!current_found_in_stage || !current_prim) {
      // Check if this path is under the NodeGraph we found earlier
      size_t last_slash = current_prim_part.rfind('/');
      if (last_slash != std::string::npos) {
        std::string parent_path = current_prim_part.substr(0, last_slash);
        std::string child_name = current_prim_part.substr(last_slash + 1);

        // Check if parent is our NodeGraph
        if (ng_prim && parent_path.find("NodeGraphs") != std::string::npos) {
          // Look for the child in the NodeGraph prim
          for (const auto& child : ng_prim->children()) {
            if (child.element_name() == child_name) {
              current_prim = &child;
              break;
            }
          }
        }
      }

      if (!current_prim) {
        return nonstd::make_unexpected(
            fmt::format("Shader {} not found\n", current_prim_part));
      }
    }

    const Shader *current_shader = current_prim ? current_prim->as<Shader>() : nullptr;
    if (!current_shader) {
      return nonstd::make_unexpected(
          fmt::format("{} is not a Shader. {}\n", current_prim_part, traversal_log));
    }

    // Log this node
    traversal_log += current_shader->info_id + " -> ";

    // Check if this is an ND_image node (color or vector variants)
    if (current_shader->info_id == "ND_image_color4" ||
        current_shader->info_id == "ND_image_color3" ||
        current_shader->info_id == "ND_image_vector4" ||
        current_shader->info_id == "ND_image_vector3" ||
        current_shader->info_id == "ND_image_float") {
      image_shader = current_shader;
      if (tex_abs_path) {
        *tex_abs_path = current_path;
      }
      if (image_shader_out) {
        *image_shader_out = image_shader;
      }
      if (assetInfo_out) {
        // get_assetInfo_struct returns AssetInfo converted from customData/assetInfo
        // Note: We only check if assetInfo is authored, but we don't return the pointer
        // since the storage has changed. The caller should use get_assetInfo_struct() directly.
        if (current_shader->metas().has_assetInfo()) {
          // AssetInfo is authored - caller should query it directly if needed
          *assetInfo_out = nullptr;
        }
      }

      // For MaterialX ND_texcoord_vector2 node, use configured default primvar name
      // (similar to OpenUSD's USDMTLX_PRIMARY_UV_NAME environment setting)
      if (st_varname_out) {
        *st_varname_out = default_texcoords_primvar_name.empty() ? "st" : default_texcoords_primvar_name;
      }

      cache_result(current_path, image_shader,
                   default_texcoords_primvar_name.empty() ? "st" : default_texcoords_primvar_name,
                   nullptr);
      return true;
    }

    // Check if this node has an input connection we should follow
    // For ND_convert_color4_color3, follow inputs:in
    bool found_next = false;
    DCOUT("Checking shader " << current_shader->info_id << " at " << current_prim_part);

    // Debug: log all properties from both Shader and ShaderNode
    std::string props_list = "ShaderProps: ";
    for (const auto& prop : current_shader->props) {
      props_list += prop.first + " ";
    }

    // Check if the shader has a ShaderNode value with properties
    const ShaderNode *shader_node = current_shader->value.as<ShaderNode>();
    if (shader_node && !shader_node->props.empty()) {
      props_list += " NodeProps: ";
      for (const auto& prop : shader_node->props) {
        props_list += prop.first + " ";
      }
    }
    traversal_log += "[" + props_list + "] ";

    // Helper lambda to check for connections in a property map
    auto find_connection = [&](const std::map<std::string, Property>& props_map) -> bool {
      for (const auto& prop : props_map) {
        if (startsWith(prop.first, "inputs:")) {
          bool is_connection = false;
          Path next_path;

          if (endsWith(prop.first, ".connect")) {
            // Explicit .connect suffix
            is_connection = true;
            if (prop.second.is_relationship()) {
              auto next_targets = prop.second.get_relationTargets();
              if (!next_targets.empty()) {
                next_path = next_targets[0];
              }
            }
          } else if (prop.second.is_attribute()) {
            // Check if attribute has connections
            const Attribute &attr = prop.second.get_attribute();
            if (attr.has_connections() && !attr.connections().empty()) {
              is_connection = true;
              next_path = attr.connections()[0];
            }
          }

          if (is_connection && !next_path.full_path_name().empty()) {
            DCOUT("  Following connection from " << prop.first << " to " << next_path);
            current_path = next_path;
            return true;
          }
        }
      }
      return false;
    };

    // Try shader_node->props first, then fall back to current_shader->props
    if (shader_node && !shader_node->props.empty()) {
      found_next = find_connection(shader_node->props);
    }
    if (!found_next) {
      found_next = find_connection(current_shader->props);
    }

    if (!found_next) {
      break;
    }
  }

  return nonstd::make_unexpected(
      fmt::format("No ND_image texture node found (supported: ND_image_color4/color3/vector4/vector3/float). {}\n", traversal_log));
}

}  // namespace

// Convert UsdUVTexture shader node.
// @return true upon conversion success(textures.back() contains the converted
// UVTexture)
//
// Possible network configuration
//
// - UsdUVTexture -> UsdPrimvarReader
// - UsdUVTexture -> UsdTransform2d -> UsdPrimvarReader
bool RenderSceneConverter::ConvertUVTexture(const RenderSceneConverterEnv &env,
                                            const Path &tex_abs_path,
                                            const AssetInfo &assetInfo,
                                            const UsdUVTexture &texture,
                                            UVTexture *tex_out) {
  DCOUT("ConvertUVTexture " << tex_abs_path);

  if (!tex_out) {
    PUSH_ERROR_AND_RETURN("tex_out arg is nullptr.");
  }
  std::string err;

  UVTexture tex;

  // Workaround for Blender export bug: UsdUVTexture without asset:file
  // This happens when Blender exports materials incorrectly
  bool has_file = texture.file.authored();

  value::AssetPath assetPath;
  if (has_file) {
    std::string asset_eval_err;
    if (!ResolveTypedAnimatableValue(env.stage, texture.file, "inputs:file",
                                     env.timecode, env.tinterp, &assetPath,
                                     &asset_eval_err)) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Failed to resolve `asset:file` from {}: {}",
                      tex_abs_path.prim_part(), asset_eval_err));
    }
  } else {
    // `asset:file` not authored (known Blender export bug). If the caller
    // tolerates missing assets, downgrade this to a warning and return
    // a default-constructed UVTexture so higher-level conversion can
    // continue; otherwise behave as before.
    if (env.material_config.allow_missing_asset) {
      PushWarn(fmt::format(
          "`asset:file` is not authored for UsdUVTexture at {}. "
          "Returning empty texture (allow_missing_asset=true).",
          tex_abs_path.prim_part()));
      *tex_out = tex;
      return true;
    }
    PUSH_ERROR_AND_RETURN(fmt::format(
        "`asset:file` is not authored for UsdUVTexture at {}.",
        tex_abs_path.prim_part()));
  }

  // TextureImage and BufferData
  {
    // Check image cache first - if the same asset path was already loaded,
    // reuse the existing image to avoid redundant I/O and memory usage
    std::string cacheKey = assetPath.GetAssetPath();
    const auto cachedImageIt = imageMap.find(cacheKey);
    if (cachedImageIt != imageMap.s_end()) {
      // Image already loaded, reuse it
      tex.texture_image_id = int64_t(cachedImageIt->second);
      DCOUT("Reusing cached image for: " << cacheKey << " (image_id=" << tex.texture_image_id << ")");
    } else {
      // Image not in cache, need to load it

    TextureImage texImage;
    BufferData assetImageBuffer;

    // Texel data is treated as byte array
    assetImageBuffer.componentType = ComponentType::UInt8;

    bool tex_loaded{false};

    if (env.scene_config.load_texture_assets) {
      DCOUT("load texture : " << assetPath.GetAssetPath());
      std::string warn;

      TextureImageLoaderFunction tex_loader_fun =
          env.material_config.texture_image_loader_function;

      if (!tex_loader_fun) {
        tex_loader_fun = DefaultTextureImageLoaderFunction;
      }

      tex_loaded = tex_loader_fun(
          assetPath, assetInfo, env.asset_resolver, &texImage,
          &assetImageBuffer.data,
          env.material_config.texture_image_loader_function_userdata, &warn,
          &err);

      if (warn.size()) {
        DCOUT("WARN: " << warn);
        PushWarn(warn);
      }

      if (!tex_loaded) {
        if (!env.material_config.allow_texture_load_failure) {
          PUSH_ERROR_AND_RETURN(fmt::format(
              "Failed to load texture image: `{}` err = {}",
              assetPath.GetAssetPath(), err));
        }

        const std::string load_err =
            err.empty() ? std::string("loader returned failure") : err;
        PUSH_WARN(fmt::format(
            "Failed to load texture image: `{}`. Skip loading. reason = {}",
            assetPath.GetAssetPath(), load_err));
      }

      // store unresolved asset path.
      texImage.asset_identifier = assetPath.GetAssetPath();
      texImage.decoded = tex_loaded;

    } else {

      Asset asset;
      std::string resolvedPath;
      if (RawAssetRead(assetPath, assetInfo, env.asset_resolver, &asset, resolvedPath, /* userdata */nullptr, /* warn */nullptr, &err )) {

        // store resolved asset path.
        texImage.asset_identifier = resolvedPath;


        BufferData imageBuffer;
        imageBuffer.componentType = tydra::ComponentType::UInt8;

        imageBuffer.data.resize(asset.size());
        memcpy(imageBuffer.data.data(), asset.data(), asset.size());

        // Assign buffer id
        texImage.buffer_id = int64_t(buffers.size());

        // TODO: Share image data as much as possible.
        // e.g. Texture A and B uses same image file, but texturing parameter is
        // different.
        buffers.emplace_back(imageBuffer);

        texImage.decoded = false;
        DCOUT("texture image is read, but not decoded.");

      } else {
        // store resolved asset path.
        texImage.asset_identifier = env.asset_resolver.resolve(assetPath.GetAssetPath());
        texImage.decoded = false;

        DCOUT("store asset path.");
      }

    }

    // colorSpace.
    // First look into `colorSpace` metadata of asset, then
    // look into `inputs:sourceColorSpace' attribute.
    // When both `colorSpace` metadata and `inputs:sourceColorSpace' attribute
    // exists, `colorSpace` metadata supercedes.
    // NOTE: `inputs:sourceColorSpace` attribute should be deprecated in favor of `colorSpace` metadata.
    bool inferColorSpaceFailed = false;
    if (has_file && texture.file.metas().has_colorSpace()) {
      ColorSpace cs;
      value::token cs_token = texture.file.metas().get_colorSpace();
      if (InferColorSpace(cs_token, &cs)) {
        texImage.usdColorSpace = cs;
        DCOUT("Inferred colorSpace: " << to_string(cs));
      } else {
        inferColorSpaceFailed = true;
      }
    }

    bool sourceColorSpaceSet = false;
    if (inferColorSpaceFailed || !has_file || !texture.file.metas().has_colorSpace()) {
      if (texture.sourceColorSpace.authored()) {
        UsdUVTexture::SourceColorSpace cs;
        std::string source_color_space_err;
        if (ResolveSourceColorSpace(env.stage, texture.sourceColorSpace,
                                    env.timecode, env.tinterp, &cs,
                                    &source_color_space_err)) {
          if (cs == UsdUVTexture::SourceColorSpace::SRGB) {
            texImage.usdColorSpace = tydra::ColorSpace::sRGB;
            sourceColorSpaceSet = true;
          } else if (cs == UsdUVTexture::SourceColorSpace::Raw) {
            texImage.usdColorSpace = tydra::ColorSpace::Raw;
            sourceColorSpaceSet = true;
          } else if (cs == UsdUVTexture::SourceColorSpace::Auto) {

            if (tex_loaded) {

              // The spec says: https://openusd.org/release/spec_usdpreviewsurface.html
              //
              // auto : Check for gamma/color space metadata in the texture file itself; if metadata is indicative of sRGB, mark texture as sRGB . If no relevant metadata is found, mark texture as sRGB if it is either 8-bit and has 3 channels or if it is 8-bit and has 4 channels. Otherwise, do not mark texture as sRGB and use texture data as it was read from the texture.
              //
              if (((texImage.assetTexelComponentType == ComponentType::UInt8) ||
                  (texImage.assetTexelComponentType == ComponentType::Int8)) &&
                ((texImage.channels == 3) || (texImage.channels ==4))) {
                texImage.usdColorSpace = tydra::ColorSpace::sRGB;
                sourceColorSpaceSet = true;
              } else {
                // For auto mode, non-8bit RGB(A) textures should be used as
                // read rather than warned about as ambiguous sRGB candidates.
                texImage.usdColorSpace = tydra::ColorSpace::Raw;
                sourceColorSpaceSet = true;
              }
            } else {
              texImage.usdColorSpace = tydra::ColorSpace::Unknown;
              sourceColorSpaceSet = true;
            }
          }
        } else if (!source_color_space_err.empty()) {
          PUSH_WARN(fmt::format(
              "Failed to resolve `inputs:sourceColorSpace`: {}",
              source_color_space_err));
        }
      }
    }

    if (!sourceColorSpaceSet && inferColorSpaceFailed && has_file) {
      value::token cs_token = texture.file.metas().get_colorSpace();
      PUSH_ERROR_AND_RETURN(
          fmt::format("Invalid or unknown colorSpace metadataum: {}. Please "
                      "report an issue to TinyUSDZ github repo.",
                      cs_token.str()));
    }

    if (tex_loaded) {
      BufferData imageBuffer;

      // Linearlization and widen texel bit depth if required.
      if (env.material_config.linearize_color_space) {
        // TODO: Support ACEScg and Lin_DisplayP3
        DCOUT("linearlize colorspace.");
        size_t width = size_t(texImage.width);
        size_t height = size_t(texImage.height);
        size_t channels = size_t(texImage.channels);

        if (channels > 4) {
          PUSH_ERROR_AND_RETURN(
              fmt::format("TODO: Multiband color channels(5 or more) are not "
                          "supported(yet)."));
        }

        // Helper: convert u8 image data to f32 buffer
        auto u8_data_to_f32_buf = [&](std::vector<float> &buf) -> bool {
          bool ret = u8_to_f32_image(assetImageBuffer.data, width, height,
                                     channels, &buf, &_err);
          if (!ret) {
            PUSH_ERROR_AND_RETURN("Failed to convert u8 image to f32 image.");
          }
          return true;
        };

        // Helper: store f32 buffer into imageBuffer
        auto store_f32_buf = [&](const std::vector<float> &buf) {
          imageBuffer.componentType = tydra::ComponentType::Float;
          size_t resize_size;
          if (!safe::mul(buf.size(), sizeof(float), &resize_size)) {
            return;  // Overflow - skip
          }
          imageBuffer.data.resize(resize_size);
          size_t memcpy_size;
          if (!safe::mul(buf.size(), sizeof(float), &memcpy_size)) {
            return;  // Overflow - skip
          }
          memcpy(imageBuffer.data.data(), buf.data(), memcpy_size);
        };

        // Helper: extract f32 buffer from assetImageBuffer
        auto asset_data_to_f32_buf = [&](std::vector<float> &buf) {
          buf.resize(assetImageBuffer.data.size() / sizeof(float));
          size_t memcpy_size;
          if (!safe::mul(buf.size(), sizeof(float), &memcpy_size)) {
            return;  // Overflow - skip
          }
          memcpy(buf.data(), assetImageBuffer.data.data(), memcpy_size);
        };

        if (assetImageBuffer.componentType == tydra::ComponentType::UInt8) {
          if (texImage.usdColorSpace == tydra::ColorSpace::sRGB ||
              texImage.usdColorSpace == tydra::ColorSpace::sRGB_Texture) {
            if (env.material_config.preserve_texel_bitdepth) {
              imageBuffer.componentType = tydra::ComponentType::UInt8;
              bool ret = srgb_8bit_to_linear_8bit(
                  assetImageBuffer.data, width, height, channels,
                  channels, &imageBuffer.data, &_err);
              if (!ret) {
                PUSH_ERROR_AND_RETURN("Failed to convert sRGB u8 image to Linear u8 image.");
              }
            } else {
              std::vector<float> buf;
              bool ret = srgb_8bit_to_linear_f32(
                  assetImageBuffer.data, width, height, channels,
                  channels, &buf, &_err);
              if (!ret) {
                PUSH_ERROR_AND_RETURN("Failed to convert sRGB u8 image to Linear f32 image.");
              }
              store_f32_buf(buf);
            }
            texImage.colorSpace = tydra::ColorSpace::Lin_sRGB;

          } else if (texImage.usdColorSpace == tydra::ColorSpace::Lin_sRGB ||
                     texImage.usdColorSpace == tydra::ColorSpace::Lin_Rec709) {
            if (env.material_config.preserve_texel_bitdepth) {
              imageBuffer = std::move(assetImageBuffer);
            } else {
              std::vector<float> buf;
              if (!u8_data_to_f32_buf(buf)) return false;
              store_f32_buf(buf);
            }
            texImage.colorSpace = tydra::ColorSpace::Lin_sRGB;

          } else if (texImage.usdColorSpace == tydra::ColorSpace::Raw) {
            // Raw data — no color conversion, just optional bit depth change
            if (env.material_config.preserve_texel_bitdepth) {
              imageBuffer = std::move(assetImageBuffer);
            } else {
              std::vector<float> buf;
              if (!u8_data_to_f32_buf(buf)) return false;
              store_f32_buf(buf);
            }
            texImage.colorSpace = tydra::ColorSpace::Raw;

          } else if (texImage.usdColorSpace == tydra::ColorSpace::g22_Rec709) {
            // Gamma 2.2 u8 -> linear f32 (via gamma removal)
            std::vector<float> buf;
            if (!u8_data_to_f32_buf(buf)) return false;
            std::vector<float> out_buf;
            if (!gamma22_f32_to_linear_f32(buf, width, height, channels, channels, &out_buf, &_err)) {
              PUSH_ERROR_AND_RETURN("Failed to convert gamma 2.2 image to linear.");
            }
            store_f32_buf(out_buf);
            texImage.colorSpace = tydra::ColorSpace::Lin_sRGB;

          } else if (texImage.usdColorSpace == tydra::ColorSpace::g18_Rec709) {
            std::vector<float> buf;
            if (!u8_data_to_f32_buf(buf)) return false;
            std::vector<float> out_buf;
            if (!gamma18_f32_to_linear_f32(buf, width, height, channels, channels, &out_buf, &_err)) {
              PUSH_ERROR_AND_RETURN("Failed to convert gamma 1.8 image to linear.");
            }
            store_f32_buf(out_buf);
            texImage.colorSpace = tydra::ColorSpace::Lin_sRGB;

          } else {
            PUSH_ERROR(fmt::format("Unsupported color space for u8 textures: {}",
                                   to_string(texImage.usdColorSpace)));
          }

        } else if (assetImageBuffer.componentType == tydra::ComponentType::Float) {
          std::vector<float> in_buf;
          asset_data_to_f32_buf(in_buf);

          if (texImage.usdColorSpace == tydra::ColorSpace::sRGB ||
              texImage.usdColorSpace == tydra::ColorSpace::sRGB_Texture) {
            std::vector<float> out_buf(in_buf.size());
            float scale_factor = 1.0f, bias = 0.0f;
            float alpha_scale_factor = 1.0f, alpha_bias = 0.0f;
            if (!srgb_f32_to_linear_f32(in_buf, width, height, channels, channels,
                                        &out_buf, scale_factor, bias,
                                        alpha_scale_factor, alpha_bias, &_err)) {
              PUSH_ERROR_AND_RETURN("Failed to convert sRGB f32 image to Linear f32 image.");
            }
            store_f32_buf(out_buf);
            texImage.colorSpace = tydra::ColorSpace::Lin_sRGB;

          } else if (texImage.usdColorSpace == tydra::ColorSpace::Lin_sRGB ||
                     texImage.usdColorSpace == tydra::ColorSpace::Lin_Rec709) {
            imageBuffer = std::move(assetImageBuffer);
            texImage.colorSpace = tydra::ColorSpace::Lin_sRGB;

          } else if (texImage.usdColorSpace == tydra::ColorSpace::Raw) {
            imageBuffer = std::move(assetImageBuffer);
            texImage.colorSpace = tydra::ColorSpace::Raw;

          } else if (texImage.usdColorSpace == tydra::ColorSpace::Lin_ACEScg) {
            // ACEScg (AP1 linear) -> linear sRGB
            std::vector<float> out_buf;
            if (!ACEScg_to_linear_sRGB(in_buf, width, height, channels,
                                       &out_buf, &_err)) {
              PUSH_ERROR_AND_RETURN("Failed to convert ACEScg to linear sRGB.");
            }
            store_f32_buf(out_buf);
            texImage.colorSpace = tydra::ColorSpace::Lin_sRGB;

          } else if (texImage.usdColorSpace == tydra::ColorSpace::ACES2065_1) {
            // ACES 2065-1 (AP0 linear) -> linear sRGB
            std::vector<float> out_buf;
            if (!ACES2065_1_to_linear_sRGB(in_buf, width, height, channels,
                                           &out_buf, &_err)) {
              PUSH_ERROR_AND_RETURN("Failed to convert ACES 2065-1 to linear sRGB.");
            }
            store_f32_buf(out_buf);
            texImage.colorSpace = tydra::ColorSpace::Lin_sRGB;

          } else if (texImage.usdColorSpace == tydra::ColorSpace::Lin_DisplayP3) {
            // Linear Display P3 -> linear sRGB
            std::vector<float> out_buf;
            if (!linear_displayp3_to_linear_sRGB(in_buf, width, height, channels,
                                                 &out_buf, &_err)) {
              PUSH_ERROR_AND_RETURN("Failed to convert Linear DisplayP3 to linear sRGB.");
            }
            store_f32_buf(out_buf);
            texImage.colorSpace = tydra::ColorSpace::Lin_sRGB;

          } else if (texImage.usdColorSpace == tydra::ColorSpace::sRGB_DisplayP3) {
            // sRGB DisplayP3: first sRGB EOTF, then DisplayP3 -> sRGB gamut
            std::vector<float> linear_p3(in_buf.size());
            float sf = 1.0f, b = 0.0f, asf = 1.0f, ab = 0.0f;
            if (!srgb_f32_to_linear_f32(in_buf, width, height, channels, channels,
                                        &linear_p3, sf, b, asf, ab, &_err)) {
              PUSH_ERROR_AND_RETURN("Failed to linearize sRGB DisplayP3.");
            }
            std::vector<float> out_buf;
            if (!linear_displayp3_to_linear_sRGB(linear_p3, width, height, channels,
                                                 &out_buf, &_err)) {
              PUSH_ERROR_AND_RETURN("Failed to convert DisplayP3 to linear sRGB.");
            }
            store_f32_buf(out_buf);
            texImage.colorSpace = tydra::ColorSpace::Lin_sRGB;

          } else if (texImage.usdColorSpace == tydra::ColorSpace::Lin_Rec2020) {
            std::vector<float> out_buf;
            if (!linear_rec2020_to_linear_sRGB(in_buf, width, height, channels,
                                               &out_buf, &_err)) {
              PUSH_ERROR_AND_RETURN("Failed to convert Linear Rec.2020 to linear sRGB.");
            }
            store_f32_buf(out_buf);
            texImage.colorSpace = tydra::ColorSpace::Lin_sRGB;

          } else if (texImage.usdColorSpace == tydra::ColorSpace::g22_Rec709) {
            std::vector<float> out_buf;
            if (!gamma22_f32_to_linear_f32(in_buf, width, height, channels, channels,
                                           &out_buf, &_err)) {
              PUSH_ERROR_AND_RETURN("Failed to convert gamma 2.2 f32 to linear.");
            }
            store_f32_buf(out_buf);
            texImage.colorSpace = tydra::ColorSpace::Lin_sRGB;

          } else if (texImage.usdColorSpace == tydra::ColorSpace::g18_Rec709) {
            std::vector<float> out_buf;
            if (!gamma18_f32_to_linear_f32(in_buf, width, height, channels, channels,
                                           &out_buf, &_err)) {
              PUSH_ERROR_AND_RETURN("Failed to convert gamma 1.8 f32 to linear.");
            }
            store_f32_buf(out_buf);
            texImage.colorSpace = tydra::ColorSpace::Lin_sRGB;

          } else {
            PUSH_ERROR(fmt::format("Unsupported color space for f32 textures: {}",
                                   to_string(texImage.usdColorSpace)));
          }

        } else {
          PUSH_ERROR(fmt::format("Unsupported asset texture texel format: {}",
                                 to_string(assetImageBuffer.componentType)));
        }

      } else {
        // Same color space.
        DCOUT("assetImageBuffer.sz = " << assetImageBuffer.data.size());

        if (assetImageBuffer.componentType == tydra::ComponentType::UInt8) {
          if (env.material_config.preserve_texel_bitdepth) {
            // Do nothing.
            imageBuffer = std::move(assetImageBuffer);

          } else {
            size_t width = size_t(texImage.width);
            size_t height = size_t(texImage.height);
            size_t channels = size_t(texImage.channels);

            // u8 to f32, but no sRGB -> linear conversion(this would break
            // UsdPreviewSurface's spec though)
            PUSH_WARN(
                "8bit sRGB texture is converted to fp32 sRGB texture(without "
                "linearlization)");
            std::vector<float> buf;
            bool ret = u8_to_f32_image(assetImageBuffer.data, width, height,
                                       channels, &buf, &_err);
            if (!ret) {
              PUSH_ERROR_AND_RETURN("Failed to convert u8 image to f32 image.");
            }
            imageBuffer.componentType = tydra::ComponentType::Float;

            size_t resize_size;
            if (!safe::mul(buf.size(), sizeof(float), &resize_size)) {
              PUSH_ERROR_AND_RETURN("Integer overflow: buf.size() * sizeof(float)");
            }
            imageBuffer.data.resize(resize_size);
            size_t memcpy_size;
            if (!safe::mul(buf.size(), sizeof(float), &memcpy_size)) {
              PUSH_ERROR_AND_RETURN("Integer overflow in memcpy");
            }
            memcpy(imageBuffer.data.data(), buf.data(), memcpy_size);
          }

          texImage.colorSpace = texImage.usdColorSpace;

        } else if (assetImageBuffer.componentType ==
                   tydra::ComponentType::Float) {
          // ignore preserve_texel_bitdepth

          // f32 to f32, so no op
          imageBuffer = std::move(assetImageBuffer);

        } else {
          PUSH_ERROR(fmt::format("TODO: asset texture texel format {}",
                                 to_string(assetImageBuffer.componentType)));
        }
      }

      // Assign buffer id
      texImage.buffer_id = int64_t(buffers.size());

      buffers.emplace_back(imageBuffer);

      tex.texture_image_id = int64_t(images.size());

      // Add to image cache for reuse by other textures with same asset path
      imageMap.add(cacheKey, uint64_t(tex.texture_image_id));

      images.emplace_back(texImage);

      std::stringstream ss;
      ss << "Loaded texture image " << assetPath.GetAssetPath()
         << " : buffer_id " + std::to_string(texImage.buffer_id) << "\n";
      ss << "  width x height x components " << texImage.width << " x "
         << texImage.height << " x " << texImage.channels << "\n";
      ss << "  colorSpace " << tinyusdz::tydra::to_string(texImage.colorSpace)
         << "\n";
      PushInfo(ss.str());
    } else {

      tex.texture_image_id = int64_t(images.size());

      // Add to image cache for reuse by other textures with same asset path
      imageMap.add(cacheKey, uint64_t(tex.texture_image_id));

      images.emplace_back(texImage);

      std::stringstream ss;
      ss << "Loaded texture image " << assetPath.GetAssetPath()
         << " : buffer_id " + std::to_string(texImage.buffer_id) << "\n";
      ss << "  width x height x components " << texImage.width << " x "
         << texImage.height << " x " << texImage.channels << "\n";
      ss << "  colorSpace " << tinyusdz::tydra::to_string(texImage.colorSpace)
         << "\n";
      PushInfo(ss.str());

    }
    } // end of image cache else block (image not in cache)
  }

  //
  // Set authored outputChannels
  //
  if (texture.outputsRGB.authored()) {
    tex.authoredOutputChannels.insert(UVTexture::Channel::RGB);
  }

  if (texture.outputsA.authored()) {
    tex.authoredOutputChannels.insert(UVTexture::Channel::A);
  }

  if (texture.outputsR.authored()) {
    tex.authoredOutputChannels.insert(UVTexture::Channel::R);
  }

  if (texture.outputsG.authored()) {
    tex.authoredOutputChannels.insert(UVTexture::Channel::G);
  }

  if (texture.outputsB.authored()) {
    tex.authoredOutputChannels.insert(UVTexture::Channel::B);
  }


  //
  // Convert other UVTexture parameters
  //

  if (texture.bias.authored()) {
    tex.bias = texture.bias.get_value();
  }

  if (texture.scale.authored()) {
    tex.scale = texture.scale.get_value();
  }

  if (texture.st.authored()) {
    if (texture.st.is_connection()) {
      const auto &paths = texture.st.get_connections();
      if (paths.size() != 1) {
        PUSH_ERROR_AND_RETURN(
            "UsdUVTexture inputs:st connection must be single Path.");
      }
      const Path &path = paths[0];

      const Prim *readerPrim{nullptr};
      if (!env.stage.find_prim_at_path(Path(path.prim_part(), ""), readerPrim,
                                       &err)) {
        PUSH_ERROR_AND_RETURN(
            "UsdUVTexture inputs:st connection targetPath not found in the "
            "Stage: " +
            err);
      }

      if (!readerPrim) {
        PUSH_ERROR_AND_RETURN(
            "[InternlError] Invalid Prim connected to inputs:st");
      }

      const Shader *pshader = readerPrim->as<Shader>();
      if (!pshader) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("UsdUVTexture inputs:st connected Prim must be "
                        "Shader Prim, but got {} Prim",
                        readerPrim->prim_type_name()));
      }

      // currently UsdTranform2d or PrimvarReaer_float2 only for inputs:st
      if (const UsdPrimvarReader_float2 *preader =
              pshader->value.as<UsdPrimvarReader_float2>()) {
        if (!preader) {
          PUSH_ERROR_AND_RETURN(
              fmt::format("Shader's info:id must be UsdPrimvarReader_float2, "
                          "but got {}",
                          pshader->info_id));
        }

        // Get value producing attribute(i.e, follow .connection and return
        // terminal Attribute value)
        std::string varname;
        TerminalAttributeValue attr;
        if (!tydra::EvaluateAttribute(env.stage, *readerPrim, "inputs:varname",
                                      &attr, &err)) {
          PUSH_ERROR_AND_RETURN(
              fmt::format("Failed to evaluate UsdPrimvarReader_float2's "
                          "inputs:varname.\n{}",
                          err));
        }

        if (auto pv = attr.as<value::token>()) {
          varname = (*pv).str();
        } else if (auto pvs = attr.as<std::string>()) {
          varname = (*pvs);
        } else if (auto pvsd = attr.as<value::StringData>()) {
          varname = (*pvsd).value;
        } else {
          PUSH_ERROR_AND_RETURN(
              "`inputs:varname` must be `string` or `token` type, but got " +
              attr.type_name());
        }
        if (varname.empty()) {
          PUSH_ERROR_AND_RETURN("`inputs:varname` is empty token.");
        }
        DCOUT("inputs:varname = " << varname);

        tex.varname_uv = varname;
      } else if (const UsdTransform2d *ptransform =
                     pshader->value.as<UsdTransform2d>()) {
        auto result = ConvertTexTransform2d(env.stage, path, *ptransform, &tex,
                                            env.timecode);
        if (!result) {
          PUSH_ERROR_AND_RETURN(result.error());
        }
      } else {
        PUSH_ERROR_AND_RETURN(
            "Unsupported Shader type for `inputs:st` connection: " +
            pshader->info_id + "\n");
      }

    } else {
      //TUSDZ_LOG_I("get_value");
      Animatable<value::texcoord2f> fallbacks = texture.st.get_value();
      value::texcoord2f uv;
      if (fallbacks.get(env.timecode, &uv)) {
        tex.fallback_uv[0] = uv[0];
        tex.fallback_uv[1] = uv[1];
      } else {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Failed to get fallback `st` texcoord attribute for "
            "UsdUVTexture at {}.",
            tex_abs_path.prim_part()));
      }
      //TUSDZ_LOG_I("uv done");
    }
  }

  if (texture.wrapS.authored()) {
    tinyusdz::UsdUVTexture::Wrap wrap;
    std::string wrap_err;

    if (!ResolveTextureWrap(env.stage, texture.wrapS, "inputs:wrapS",
                            env.timecode, env.tinterp, &wrap, &wrap_err)) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Invalid UsdUVTexture `inputs:wrapS` value: {}", wrap_err));
    }

    if (wrap == UsdUVTexture::Wrap::Repeat) {
      tex.wrapS = UVTexture::WrapMode::REPEAT;
    } else if (wrap == UsdUVTexture::Wrap::Mirror) {
      tex.wrapS = UVTexture::WrapMode::MIRROR;
    } else if (wrap == UsdUVTexture::Wrap::Clamp) {
      tex.wrapS = UVTexture::WrapMode::CLAMP_TO_EDGE;
    } else if (wrap == UsdUVTexture::Wrap::Black) {
      tex.wrapS = UVTexture::WrapMode::CLAMP_TO_BORDER;
    } else {
      tex.wrapS = UVTexture::WrapMode::CLAMP_TO_EDGE;
    }
  }

  if (texture.wrapT.authored()) {
    tinyusdz::UsdUVTexture::Wrap wrap;
    std::string wrap_err;

    if (!ResolveTextureWrap(env.stage, texture.wrapT, "inputs:wrapT",
                            env.timecode, env.tinterp, &wrap, &wrap_err)) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Invalid UsdUVTexture `inputs:wrapT` value: {}", wrap_err));
    }

    if (wrap == UsdUVTexture::Wrap::Repeat) {
      tex.wrapT = UVTexture::WrapMode::REPEAT;
    } else if (wrap == UsdUVTexture::Wrap::Mirror) {
      tex.wrapT = UVTexture::WrapMode::MIRROR;
    } else if (wrap == UsdUVTexture::Wrap::Clamp) {
      tex.wrapT = UVTexture::WrapMode::CLAMP_TO_EDGE;
    } else if (wrap == UsdUVTexture::Wrap::Black) {
      tex.wrapT = UVTexture::WrapMode::CLAMP_TO_BORDER;
    } else {
      tex.wrapT = UVTexture::WrapMode::CLAMP_TO_EDGE;
    }
  }

  DCOUT("Converted UVTexture.");

  (*tex_out) = tex;
  return true;
}

template <typename T, typename Dty>
bool RenderSceneConverter::ConvertPreviewSurfaceShaderParam(
    const RenderSceneConverterEnv &env, const Path &shader_abs_path,
    const TypedAttributeWithFallback<Animatable<T>> &param,
    const std::string &param_name, ShaderParam<Dty> &dst_param,
    bool is_materialx) {
  if (!param.authored()) {
    return true;
  }

  if (param.is_blocked()) {
    PUSH_ERROR_AND_RETURN(fmt::format("{} attribute is blocked.", param_name));
  } else if (param.is_connection()) {
    DCOUT(fmt::format("{} is attribute connection.", param_name));

    // Check if this is a MaterialX connection to a NodeGraph
    if (is_materialx && param.get_connections().size() == 1) {
      const Path &conn_path = param.get_connections()[0];
      if (conn_path.prim_part().find("/NodeGraphs") != std::string::npos) {
        // This is a MaterialX NodeGraph connection, traverse to find texture
        const Shader *image_shader{nullptr};
        Path texPath;
        std::string st_varname;
        const AssetInfo *assetInfo{nullptr};

        auto mtlx_result = GetConnectedMtlxTexture(
            env.stage, param, &texPath, &image_shader, &st_varname, &assetInfo,
            env.mesh_config.default_texcoords_primvar_name);

        if (mtlx_result) {
          // Found a MaterialX texture node
          DCOUT("Found MaterialX texture node: " << texPath);

          // Extract the file path from the image shader
          value::AssetPath texAssetPath;
          bool found_file = false;

          // Helper lambda to find file input in a property map
          auto find_file_input = [&](const std::map<std::string, Property>& props_map) -> bool {
            for (const auto& prop : props_map) {
              if (prop.first == "inputs:file" && prop.second.is_attribute()) {
                const Attribute &attr = prop.second.get_attribute();
                if (attr.has_value()) {
                  auto asset_val = attr.get_value<value::AssetPath>();
                  if (asset_val) {
                    texAssetPath = *asset_val;
                    return true;
                  }
                }
              }
            }
            return false;
          };

          // Check both ShaderNode props and Shader props
          const ShaderNode *shader_node = image_shader->value.as<ShaderNode>();
          if (shader_node && !shader_node->props.empty()) {
            found_file = find_file_input(shader_node->props);
          }
          if (!found_file) {
            found_file = find_file_input(image_shader->props);
          }

          if (!found_file) {
            PUSH_ERROR_AND_RETURN(fmt::format(
                "MaterialX image node {} has no file input",
                texPath.prim_part()));
          }

          // Create a synthetic UsdUVTexture to pass to ConvertUVTexture
          UsdUVTexture synth_tex;
          synth_tex.file.set_value(texAssetPath);

          // Helper lambda to extract wrap mode from properties
          auto extract_wrap_modes = [&](const std::map<std::string, Property>& props_map) {
            for (const auto& prop : props_map) {
              if (prop.first == "inputs:uaddressmode" && prop.second.is_attribute()) {
                const Attribute &attr = prop.second.get_attribute();
                if (attr.has_value()) {
                  auto val = attr.get_value<std::string>();
                  if (val) {
                    if (*val == "periodic") {
                      synth_tex.wrapS.set_value(UsdUVTexture::Wrap::Repeat);
                    } else if (*val == "clamp") {
                      synth_tex.wrapS.set_value(UsdUVTexture::Wrap::Clamp);
                    } else if (*val == "mirror") {
                      synth_tex.wrapS.set_value(UsdUVTexture::Wrap::Mirror);
                    } else if (*val == "constant") {
                      synth_tex.wrapS.set_value(UsdUVTexture::Wrap::Black);
                    }
                  }
                }
              }
              if (prop.first == "inputs:vaddressmode" && prop.second.is_attribute()) {
                const Attribute &attr = prop.second.get_attribute();
                if (attr.has_value()) {
                  auto val = attr.get_value<std::string>();
                  if (val) {
                    if (*val == "periodic") {
                      synth_tex.wrapT.set_value(UsdUVTexture::Wrap::Repeat);
                    } else if (*val == "clamp") {
                      synth_tex.wrapT.set_value(UsdUVTexture::Wrap::Clamp);
                    } else if (*val == "mirror") {
                      synth_tex.wrapT.set_value(UsdUVTexture::Wrap::Mirror);
                    } else if (*val == "constant") {
                      synth_tex.wrapT.set_value(UsdUVTexture::Wrap::Black);
                    }
                  }
                }
              }
            }
          };

          // Map MaterialX wrap modes to USD - check both ShaderNode and Shader props
          if (shader_node && !shader_node->props.empty()) {
            extract_wrap_modes(shader_node->props);
          }
          extract_wrap_modes(image_shader->props);

          // Use ConvertUVTexture to properly handle the texture
          UVTexture rtex;
          AssetInfo mtlx_assetInfo; // Use the assetInfo if available
          if (assetInfo) {
            mtlx_assetInfo = *assetInfo;
          }

          // Set sourceColorSpace based on parameter semantics.
          // Color parameters (diffuseColor, emissiveColor, etc.) use sRGB,
          // non-color parameters (roughness, metallic, normal, etc.) use Raw
          // to prevent double-linearization.
          // This matches hdSt's MaterialX texture handling where colorspace
          // is inferred from the MaterialX nodedef's type.
          {
            static const std::set<std::string> srgb_params = {
              "diffuseColor", "emissiveColor", "specularColor",
              "base_color", "emission_color", "specular_color",
              "coat_color", "sheen_color", "subsurface_color",
              "transmission_color", "fuzz_color",
            };
            Animatable<UsdUVTexture::SourceColorSpace> cs;
            if (srgb_params.count(param_name)) {
              cs.set_default(UsdUVTexture::SourceColorSpace::SRGB);
            } else {
              cs.set_default(UsdUVTexture::SourceColorSpace::Raw);
            }
            synth_tex.sourceColorSpace.set_value(cs);
          }

          if (!ConvertUVTexture(env, texPath, mtlx_assetInfo, synth_tex, &rtex)) {
            PUSH_ERROR_AND_RETURN(fmt::format(
                "Failed to convert MaterialX texture for {}", param_name));
          }

          // Set the connected output channel and UV primvar name
          rtex.connectedOutputChannel = tydra::UVTexture::Channel::RGB;
          rtex.varname_uv = st_varname;

          uint64_t texId = textures.size();
          textures.push_back(rtex);

          textureMap.add(texId, shader_abs_path.prim_part() + "." + param_name);

          DCOUT(fmt::format("MaterialX TexId {}.{} = {}",
                            shader_abs_path.prim_part(), param_name, texId));

          dst_param.texture_id = int32_t(texId);

          return true;
        } else {
          // No texture found — try evaluating the node graph as a constant
          // value (e.g., ND_add_float or ND_multiply_color3 with constant inputs).
          auto const_result =
              EvaluateMtlxNodeGraphAsConstant(env.stage, conn_path);
          if (const_result) {
            DCOUT(fmt::format("MaterialX constant evaluation {}.{} components={}",
                              shader_abs_path.prim_part(), param_name,
                              const_result->n));
            if (std::is_same<T, float>::value && const_result->is_float()) {
              float v = const_result->as_float();
              memcpy(&dst_param.value, &v, sizeof(float));
              return true;
            }
            if (std::is_same<T, value::color3f>::value && const_result->is_color3()) {
              value::color3f c;
              c[0] = const_result->v[0];
              c[1] = const_result->v[1];
              c[2] = const_result->v[2];
              memcpy(&dst_param.value, &c, sizeof(value::color3f));
              return true;
            }
            // Float result can also be used for color3 (broadcast)
            if (std::is_same<T, value::color3f>::value && const_result->is_float()) {
              float f = const_result->as_float();
              value::color3f c;
              c[0] = f; c[1] = f; c[2] = f;
              memcpy(&dst_param.value, &c, sizeof(value::color3f));
              return true;
            }
            // Color3 result used for float (take first component)
            if (std::is_same<T, float>::value && const_result->is_color3()) {
              float v = const_result->v[0];
              memcpy(&dst_param.value, &v, sizeof(float));
              return true;
            }
          }
          PUSH_ERROR_AND_RETURN(fmt::format(
              "Failed to find MaterialX texture for {}: {}",
              param_name, mtlx_result.error()));
        }
      }
    }

    // Fall back to standard UsdUVTexture handling
    const UsdUVTexture *ptex{nullptr};
    const Shader *pshader{nullptr};
    Path texPath;
    auto result =
        GetConnectedUVTexture(env.stage, param, &texPath, &ptex, &pshader);

    if (!result) {
      PUSH_ERROR_AND_RETURN(result.error());
    }

    if (!ptex) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "[InternalError] ptex is nullptr for parameter '{}' in shader '{}'.",
          param_name, shader_abs_path.full_path_name()));
    }
    DCOUT("ptex = " << ptex->name);

    if (!pshader) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "[InternalError] pshader is nullptr for parameter '{}' in shader '{}'.",
          param_name, shader_abs_path.full_path_name()));
    }

    DCOUT("Get connected UsdUVTexture Prim: " << texPath);

    UVTexture rtex;
    const AssetInfo assetInfo = pshader->metas().get_assetInfo_struct();
    if (!ConvertUVTexture(env, texPath, assetInfo, *ptex, &rtex)) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "Failed to convert UVTexture connected to {}", param_name));
    }

    // Extract connected outputChannel from prop part.
    std::string prop_part = texPath.prop_part();

    // TODO: Attribute type check.
    if (prop_part == "outputs:r") {
      rtex.connectedOutputChannel = tydra::UVTexture::Channel::R;
    } else if (prop_part == "outputs:g") {
      rtex.connectedOutputChannel = tydra::UVTexture::Channel::G;
    } else if (prop_part == "outputs:b") {
      rtex.connectedOutputChannel = tydra::UVTexture::Channel::B;
    } else if (prop_part == "outputs:a") {
      rtex.connectedOutputChannel = tydra::UVTexture::Channel::A;
    } else if (prop_part == "outputs:rgb") {
      rtex.connectedOutputChannel = tydra::UVTexture::Channel::RGB;
    } else {
      PUSH_ERROR_AND_RETURN(fmt::format("Unknown or invalid connection to a property of output channel: {}(Abs path {})", prop_part, texPath.full_path_name()));
    }


    uint64_t texId = textures.size();
    textures.push_back(rtex);

    textureMap.add(texId, shader_abs_path.prim_part() + "." + param_name);

    DCOUT(fmt::format("TexId {}.{} = {}",
                      shader_abs_path.prim_part(), param_name, texId));

    dst_param.texture_id = int32_t(texId);

    return true;
  } else {
    T val;
    if (!param.get_value().get(env.timecode, &val)) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Failed to get {} at `default` timecode.", param_name));
    }

    dst_param.set_value(val);

    return true;
  }
}

bool RenderSceneConverter::ConvertPreviewSurfaceShader(
    const RenderSceneConverterEnv &env, const Path &shader_abs_path,
    const UsdPreviewSurface &shader, PreviewSurfaceShader *rshader_out) {
  if (!rshader_out) {
    PUSH_ERROR_AND_RETURN("rshader_out arg is nullptr.");
  }

  PreviewSurfaceShader rshader;

  if (shader.useSpecularWorkflow.authored()) {
    if (shader.useSpecularWorkflow.is_blocked()) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("useSpecularWorkflow attribute is blocked."));
    } else {
      int val;
      std::string eval_err;
      if (!ResolveTypedAnimatableValue(
              env.stage, shader.useSpecularWorkflow,
              "inputs:useSpecularWorkflow", env.timecode, env.tinterp, &val,
              &eval_err)) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("Failed to resolve useSpecularWorkflow at time `{}`: {}",
                        env.timecode, eval_err));
      }

      rshader.useSpecularWorkflow = val ? true : false;
    }
  }

  // Macro to reduce repetitive ConvertPreviewSurfaceShaderParam calls.
#define CONVERT_PREVIEW_PARAM(field, name) \
  if (!ConvertPreviewSurfaceShaderParam( \
          env, shader_abs_path, shader.field, name, rshader.field)) { \
    PushWarn(fmt::format("Failed to convert " name " parameter for shader: {}", \
                         shader_abs_path.prim_part())); \
    return false; \
  }

  CONVERT_PREVIEW_PARAM(diffuseColor, "diffuseColor")
  CONVERT_PREVIEW_PARAM(emissiveColor, "emissiveColor")
  CONVERT_PREVIEW_PARAM(specularColor, "specularColor")
  CONVERT_PREVIEW_PARAM(normal, "normal")
  CONVERT_PREVIEW_PARAM(roughness, "roughness")
  CONVERT_PREVIEW_PARAM(metallic, "metallic")
  CONVERT_PREVIEW_PARAM(clearcoat, "clearcoat")
  CONVERT_PREVIEW_PARAM(clearcoatRoughness, "clearcoatRoughness")
  CONVERT_PREVIEW_PARAM(opacity, "opacity")
  CONVERT_PREVIEW_PARAM(opacityThreshold, "opacityThreshold")
  CONVERT_PREVIEW_PARAM(ior, "ior")
  CONVERT_PREVIEW_PARAM(occlusion, "occlusion")
  CONVERT_PREVIEW_PARAM(displacement, "displacement")

#undef CONVERT_PREVIEW_PARAM

  (*rshader_out) = rshader;
  return true;
}

bool RenderSceneConverter::ConvertOpenPBRSurfaceShader(
    const RenderSceneConverterEnv &env, const Path &shader_abs_path,
    const OpenPBRSurface &shader, OpenPBRSurfaceShader *rshader_out) {
  if (!rshader_out) {
    PUSH_ERROR_AND_RETURN("rshader_out argument is nullptr.");
  }

  OpenPBRSurfaceShader rshader;

  // Macros to reduce repetitive ConvertPreviewSurfaceShaderParam calls.
#define CONVERT_OPENPBR_PARAM(field, name) \
  if (!ConvertPreviewSurfaceShaderParam( \
          env, shader_abs_path, shader.field, name, rshader.field)) { \
    PushWarn(fmt::format("Failed to convert " name " parameter for shader: {}", shader_abs_path.prim_part())); \
    return false; \
  }
#define CONVERT_OPENPBR_PARAM_MTLX(field, name) \
  if (!ConvertPreviewSurfaceShaderParam( \
          env, shader_abs_path, shader.field, name, rshader.field, true)) { \
    PushWarn(fmt::format("Failed to convert " name " parameter for shader: {}", shader_abs_path.prim_part())); \
    return false; \
  }

  // Base layer
  CONVERT_OPENPBR_PARAM_MTLX(base_weight, "base_weight")
  CONVERT_OPENPBR_PARAM_MTLX(base_color, "base_color")
  CONVERT_OPENPBR_PARAM_MTLX(base_roughness, "base_roughness")
  CONVERT_OPENPBR_PARAM_MTLX(base_metalness, "base_metalness")
  CONVERT_OPENPBR_PARAM_MTLX(base_diffuse_roughness, "base_diffuse_roughness")

  // Specular layer
  CONVERT_OPENPBR_PARAM_MTLX(specular_weight, "specular_weight")
  CONVERT_OPENPBR_PARAM_MTLX(specular_color, "specular_color")
  CONVERT_OPENPBR_PARAM_MTLX(specular_roughness, "specular_roughness")
  CONVERT_OPENPBR_PARAM_MTLX(specular_ior, "specular_ior")
  CONVERT_OPENPBR_PARAM(specular_ior_level, "specular_ior_level")
  CONVERT_OPENPBR_PARAM(specular_anisotropy, "specular_anisotropy")
  CONVERT_OPENPBR_PARAM(specular_rotation, "specular_rotation")
  CONVERT_OPENPBR_PARAM(specular_roughness_anisotropy, "specular_roughness_anisotropy")

  // Transmission
  CONVERT_OPENPBR_PARAM_MTLX(transmission_weight, "transmission_weight")
  CONVERT_OPENPBR_PARAM_MTLX(transmission_color, "transmission_color")
  CONVERT_OPENPBR_PARAM(transmission_depth, "transmission_depth")
  CONVERT_OPENPBR_PARAM(transmission_scatter, "transmission_scatter")
  CONVERT_OPENPBR_PARAM(transmission_scatter_anisotropy, "transmission_scatter_anisotropy")
  CONVERT_OPENPBR_PARAM(transmission_dispersion, "transmission_dispersion")
  CONVERT_OPENPBR_PARAM(transmission_dispersion_abbe_number, "transmission_dispersion_abbe_number")
  CONVERT_OPENPBR_PARAM(transmission_dispersion_scale, "transmission_dispersion_scale")

  // Subsurface
  CONVERT_OPENPBR_PARAM_MTLX(subsurface_weight, "subsurface_weight")
  CONVERT_OPENPBR_PARAM_MTLX(subsurface_color, "subsurface_color")
  CONVERT_OPENPBR_PARAM_MTLX(subsurface_radius, "subsurface_radius")
  CONVERT_OPENPBR_PARAM_MTLX(subsurface_radius_scale, "subsurface_radius_scale")
  CONVERT_OPENPBR_PARAM_MTLX(subsurface_scale, "subsurface_scale")
  CONVERT_OPENPBR_PARAM_MTLX(subsurface_anisotropy, "subsurface_anisotropy")
  CONVERT_OPENPBR_PARAM(subsurface_scatter_anisotropy, "subsurface_scatter_anisotropy")

  // Sheen
  CONVERT_OPENPBR_PARAM_MTLX(sheen_weight, "sheen_weight")
  CONVERT_OPENPBR_PARAM_MTLX(sheen_color, "sheen_color")
  CONVERT_OPENPBR_PARAM_MTLX(sheen_roughness, "sheen_roughness")

  // Fuzz
  CONVERT_OPENPBR_PARAM_MTLX(fuzz_weight, "fuzz_weight")
  CONVERT_OPENPBR_PARAM_MTLX(fuzz_color, "fuzz_color")
  CONVERT_OPENPBR_PARAM_MTLX(fuzz_roughness, "fuzz_roughness")

  // Thin film
  CONVERT_OPENPBR_PARAM_MTLX(thin_film_weight, "thin_film_weight")
  CONVERT_OPENPBR_PARAM_MTLX(thin_film_thickness, "thin_film_thickness")
  CONVERT_OPENPBR_PARAM_MTLX(thin_film_ior, "thin_film_ior")

  // Coat layer
  CONVERT_OPENPBR_PARAM_MTLX(coat_weight, "coat_weight")
  CONVERT_OPENPBR_PARAM_MTLX(coat_color, "coat_color")
  CONVERT_OPENPBR_PARAM_MTLX(coat_roughness, "coat_roughness")
  CONVERT_OPENPBR_PARAM_MTLX(coat_anisotropy, "coat_anisotropy")
  CONVERT_OPENPBR_PARAM_MTLX(coat_rotation, "coat_rotation")
  CONVERT_OPENPBR_PARAM_MTLX(coat_ior, "coat_ior")
  CONVERT_OPENPBR_PARAM_MTLX(coat_affect_color, "coat_affect_color")
  CONVERT_OPENPBR_PARAM_MTLX(coat_affect_roughness, "coat_affect_roughness")
  CONVERT_OPENPBR_PARAM(coat_roughness_anisotropy, "coat_roughness_anisotropy")
  CONVERT_OPENPBR_PARAM(coat_darkening, "coat_darkening")

  // Emission
  CONVERT_OPENPBR_PARAM_MTLX(emission_luminance, "emission_luminance")
  CONVERT_OPENPBR_PARAM_MTLX(emission_color, "emission_color")

  // Geometry
  CONVERT_OPENPBR_PARAM_MTLX(opacity, "opacity")
  CONVERT_OPENPBR_PARAM_MTLX(normal, "normal")
  CONVERT_OPENPBR_PARAM_MTLX(tangent, "tangent")

#undef CONVERT_OPENPBR_PARAM

  // Convert MaterialX NodeGraph connections to JSON if present
  // This allows reconstruction of node-based shading in JavaScript/WASM
  {
    const Prim *shader_prim_ptr = nullptr;
    std::string lookup_err;
    if (env.stage.find_prim_at_path(shader_abs_path, shader_prim_ptr, &lookup_err) && shader_prim_ptr) {
      std::string nodegraph_json;
      std::string conv_err;
      if (ConvertShaderWithNodeGraphToJson(*shader_prim_ptr, shader_abs_path, env.stage, &nodegraph_json, &conv_err)) {
        if (!nodegraph_json.empty()) {
          rshader.nodeGraphJson = nodegraph_json;
          DCOUT("Successfully converted MaterialX NodeGraph to JSON for shader: " << shader_abs_path.prim_part());
        }
      } else {
        // Not an error - shader may not have node graph connections
        DCOUT("No MaterialX NodeGraph found for shader: " << shader_abs_path.prim_part() << " (" << conv_err << ")");
      }
    }
  }

  (*rshader_out) = rshader;
  return true;
}

// Convert MtlxAutodeskStandardSurface → OpenPBRSurface.
// Maps StandardSurface parameters to their OpenPBR equivalents.
// Key differences: naming (base vs base_weight), opacity type (color3f vs float),
// no fuzz layer in StandardSurface.
static OpenPBRSurface ConvertMtlxStandardSurfaceToOpenPBRSurface(
    const MtlxAutodeskStandardSurface &src) {
  OpenPBRSurface dst;

  // Base layer
  dst.base_weight = src.base;
  dst.base_color = src.base_color;
  dst.base_diffuse_roughness = src.diffuse_roughness;
  dst.base_metalness = src.metalness;

  // Specular layer
  dst.specular_weight = src.specular;
  dst.specular_color = src.specular_color;
  dst.specular_roughness = src.specular_roughness;
  dst.specular_ior = src.specular_IOR;
  dst.specular_anisotropy = src.specular_anisotropy;
  dst.specular_rotation = src.specular_rotation;

  // Transmission
  dst.transmission_weight = src.transmission;
  dst.transmission_color = src.transmission_color;
  dst.transmission_depth = src.transmission_depth;
  dst.transmission_scatter = src.transmission_scatter;
  dst.transmission_scatter_anisotropy = src.transmission_scatter_anisotropy;
  dst.transmission_dispersion = src.transmission_dispersion;
  // Note: StandardSurface.transmission_extra_roughness has no OpenPBR equivalent

  // Subsurface
  dst.subsurface_weight = src.subsurface;
  dst.subsurface_color = src.subsurface_color;
  dst.subsurface_scale = src.subsurface_scale;
  dst.subsurface_anisotropy = src.subsurface_anisotropy;

  // Sheen
  dst.sheen_weight = src.sheen;
  dst.sheen_color = src.sheen_color;
  dst.sheen_roughness = src.sheen_roughness;

  // Coat
  dst.coat_weight = src.coat;
  dst.coat_color = src.coat_color;
  dst.coat_roughness = src.coat_roughness;
  dst.coat_anisotropy = src.coat_anisotropy;
  dst.coat_rotation = src.coat_rotation;
  dst.coat_ior = src.coat_IOR;
  dst.coat_affect_roughness = src.coat_affect_roughness;
  dst.coat_affect_color = src.coat_affect_color;

  // Thin film
  dst.thin_film_thickness = src.thin_film_thickness;
  dst.thin_film_ior = src.thin_film_IOR;

  // Emission
  dst.emission_luminance = src.emission;
  dst.emission_color = src.emission_color;

  // Opacity: StandardSurface is color3f, OpenPBR is float — take luminance
  // Using Rec.709 luminance: 0.2126*R + 0.7152*G + 0.0722*B

  // Geometry (normal, tangent)
  // StandardSurface uses TypedAttribute (optional, no fallback),
  // OpenPBR uses TypedAttributeWithFallback. Extract value if authored.
  if (src.normal.authored()) {
    auto nval = src.normal.get_value();  // nonstd::optional<Animatable<normal3f>>
    if (nval) {
      dst.normal.set_value(*nval);
    }
  }
  if (src.tangent.authored()) {
    auto tval = src.tangent.get_value();  // nonstd::optional<Animatable<vector3f>>
    if (tval) {
      dst.tangent.set_value(*tval);
    }
  }

  return dst;
}

static OpenPBRSurface ConvertMtlxOpenPBRSurfaceToOpenPBRSurface(
    const MtlxOpenPBRSurface &src) {
  OpenPBRSurface dst;

  // Copy base layer properties
  dst.base_weight = src.base_weight;
  dst.base_color = src.base_color;
  dst.base_roughness = src.base_diffuse_roughness;
  dst.base_metalness = src.base_metalness;
  dst.base_diffuse_roughness = src.base_diffuse_roughness;

  // Copy specular properties
  dst.specular_weight = src.specular_weight;
  dst.specular_color = src.specular_color;
  dst.specular_roughness = src.specular_roughness;
  dst.specular_ior = src.specular_ior;
  dst.specular_anisotropy = src.specular_anisotropy;
  dst.specular_rotation = src.specular_rotation;
  dst.specular_roughness_anisotropy = src.specular_roughness_anisotropy;

  // Copy transmission properties
  dst.transmission_weight = src.transmission_weight;
  dst.transmission_color = src.transmission_color;
  dst.transmission_depth = src.transmission_depth;
  dst.transmission_scatter = src.transmission_scatter;
  dst.transmission_scatter_anisotropy = src.transmission_scatter_anisotropy;
  dst.transmission_dispersion = src.transmission_dispersion;
  dst.transmission_dispersion_abbe_number = src.transmission_dispersion_abbe_number;
  dst.transmission_dispersion_scale = src.transmission_dispersion_scale;

  // Copy subsurface properties
  dst.subsurface_weight = src.subsurface_weight;
  dst.subsurface_color = src.subsurface_color;
  dst.subsurface_scale = src.subsurface_scale;
  dst.subsurface_anisotropy = src.subsurface_anisotropy;
  dst.subsurface_scatter_anisotropy = src.subsurface_scatter_anisotropy;

  // Copy coat properties
  dst.coat_weight = src.coat_weight;
  dst.coat_color = src.coat_color;
  dst.coat_roughness = src.coat_roughness;
  dst.coat_anisotropy = src.coat_anisotropy;
  dst.coat_rotation = src.coat_rotation;
  dst.coat_ior = src.coat_ior;
  dst.coat_affect_color = src.coat_affect_color;
  dst.coat_affect_roughness = src.coat_affect_roughness;
  dst.coat_roughness_anisotropy = src.coat_roughness_anisotropy;
  dst.coat_darkening = src.coat_darkening;

  // Copy fuzz properties (velvet/fabric-like appearance)
  dst.fuzz_weight = src.fuzz_weight;
  dst.fuzz_color = src.fuzz_color;
  dst.fuzz_roughness = src.fuzz_roughness;

  // Copy thin film properties (iridescence)
  dst.thin_film_weight = src.thin_film_weight;
  dst.thin_film_thickness = src.thin_film_thickness;
  dst.thin_film_ior = src.thin_film_ior;

  // Copy emission properties
  dst.emission_luminance = src.emission_luminance;
  dst.emission_color = src.emission_color;

  // Copy geometry properties
  dst.opacity = src.geometry_opacity;
  if (src.geometry_normal.has_value()) {
    auto normal_val = src.geometry_normal.get_value();
    if (normal_val) {
      dst.normal = normal_val.value();
    }
  }
  if (src.geometry_tangent.has_value()) {
    auto tangent_val = src.geometry_tangent.get_value();
    if (tangent_val) {
      dst.tangent = tangent_val.value();
    }
  }

  return dst;
}

static int32_t ApplyMtlxNormalMapInfoToOpenPBRShader(
    const MtlxNodeGraphInfo &normal_info, const std::string &default_uv_name,
    std::vector<TextureImage> *images, std::vector<UVTexture> *textures,
    OpenPBRSurfaceShader *openpbr_shader) {
  if (!images || !textures || !openpbr_shader) {
    return -1;
  }

  if (!normal_info.has_normal_map) {
    return -1;
  }

  openpbr_shader->normal_map_scale = normal_info.normal_map_scale;

  if (normal_info.normal_map_texture.empty()) {
    return -1;
  }

  TextureImage tex_image;
  tex_image.asset_identifier = normal_info.normal_map_texture;
  tex_image.colorSpace = ColorSpace::Raw;  // Normal maps are always raw/linear
  tex_image.usdColorSpace = ColorSpace::Raw;

  int64_t image_id = -1;
  for (size_t i = 0; i < images->size(); ++i) {
    if ((*images)[i].asset_identifier == normal_info.normal_map_texture) {
      image_id = static_cast<int64_t>(i);
      break;
    }
  }

  if (image_id < 0) {
    image_id = static_cast<int64_t>(images->size());
    images->push_back(tex_image);
  }

  UVTexture uvtex;
  uvtex.texture_image_id = static_cast<int32_t>(image_id);
  uvtex.varname_uv = default_uv_name;
  uvtex.connectedOutputChannel = UVTexture::Channel::RGB;
  uvtex.wrapS = UVTexture::WrapMode::REPEAT;
  uvtex.wrapT = UVTexture::WrapMode::REPEAT;

  int32_t tex_id = static_cast<int32_t>(textures->size());
  textures->push_back(uvtex);
  openpbr_shader->normal.texture_id = tex_id;
  return tex_id;
}

static bool ApplyMtlxTangentInfoToOpenPBRShader(
    const MtlxNodeGraphInfo &tangent_info,
    OpenPBRSurfaceShader *openpbr_shader) {
  if (!openpbr_shader) {
    return false;
  }

  if (!tangent_info.has_tangent_rotation) {
    return false;
  }

  openpbr_shader->tangent_rotation = tangent_info.tangent_rotation;
  return true;
}

static void ApplyMtlxGeometryNodeGraphInfoToOpenPBRShader(
    const Stage &stage, const Prim *material_prim,
    const MtlxOpenPBRSurface &mtlx_openpbr, const std::string &default_uv_name,
    std::vector<TextureImage> *images, std::vector<UVTexture> *textures,
    OpenPBRSurfaceShader *openpbr_shader, std::string *err,
    bool emit_extract_debug_trace) {
  if (!material_prim || !images || !textures || !openpbr_shader) {
    return;
  }

  // Check if geometry_normal has connections (links to NodeGraph with ND_normalmap node)
  const auto &normal_conns = mtlx_openpbr.geometry_normal.get_connections();
  DCOUT("DEBUG: geometry_normal has " << normal_conns.size()
        << " connections, has_value="
        << mtlx_openpbr.geometry_normal.has_value());

  if (!normal_conns.empty()) {
    if (emit_extract_debug_trace) {
      DCOUT("DEBUG: First connection path: " << normal_conns[0].full_path_name());
    }

    std::string extract_debug;
    std::string *extract_err = emit_extract_debug_trace ? &extract_debug : err;
    auto normal_info_result = ExtractMtlxNodeGraphInfo(
        stage, material_prim, normal_conns, extract_err);

    if (emit_extract_debug_trace && !extract_debug.empty()) {
      DCOUT("ExtractMtlxNodeGraphInfo debug:\n" << extract_debug);
    }

    if (normal_info_result) {
      const auto &normal_info = normal_info_result.value();
      DCOUT("DEBUG: ExtractMtlxNodeGraphInfo returned: has_normal_map="
            << normal_info.has_normal_map
            << ", normal_map_scale=" << normal_info.normal_map_scale
            << ", normal_map_texture='" << normal_info.normal_map_texture << "'");

      int32_t tex_id = ApplyMtlxNormalMapInfoToOpenPBRShader(
          normal_info, default_uv_name, images, textures, openpbr_shader);
      if (normal_info.has_normal_map) {
        DCOUT("DEBUG: Extracted normal_map_scale: "
              << normal_info.normal_map_scale);
      }
      if (tex_id >= 0) {
        DCOUT("DEBUG: Created normal map UVTexture with tex_id: " << tex_id);
      }
    } else {
      std::string error_message;
      if (emit_extract_debug_trace) {
        error_message = extract_debug;
      } else if (err) {
        error_message = *err;
      }
      DCOUT("DEBUG: ExtractMtlxNodeGraphInfo failed: " << error_message);
    }
  }

  // Check if geometry_tangent has connections (links to NodeGraph with ND_rotate3d_vector3 node)
  const auto &tangent_conns = mtlx_openpbr.geometry_tangent.get_connections();
  if (!tangent_conns.empty()) {
    auto tangent_info_result = ExtractMtlxNodeGraphInfo(
        stage, material_prim, tangent_conns, err);
    if (tangent_info_result) {
      const auto &tangent_info = tangent_info_result.value();
      if (ApplyMtlxTangentInfoToOpenPBRShader(tangent_info, openpbr_shader)) {
        DCOUT("DEBUG: Extracted tangent_rotation: "
              << tangent_info.tangent_rotation);
      }
    }
  }

  // Check if geometry_coat_normal has connections
  const auto &coat_normal_conns = mtlx_openpbr.geometry_coat_normal.get_connections();
  if (!coat_normal_conns.empty()) {
    auto coat_normal_info_result = ExtractMtlxNodeGraphInfo(
        stage, material_prim, coat_normal_conns, err);
    if (coat_normal_info_result) {
      const auto &coat_normal_info = coat_normal_info_result.value();
      if (coat_normal_info.has_normal_map) {
        openpbr_shader->coat_normal_map_scale = coat_normal_info.normal_map_scale;
        // Create coat normal map texture (same logic as base normal map)
        if (!coat_normal_info.normal_map_texture.empty()) {
          TextureImage coat_nmap_img;
          coat_nmap_img.asset_identifier = coat_normal_info.normal_map_texture;
          coat_nmap_img.colorSpace = ColorSpace::Raw;
          coat_nmap_img.usdColorSpace = ColorSpace::Raw;
          images->push_back(coat_nmap_img);

          UVTexture coat_nmap_tex;
          coat_nmap_tex.texture_image_id = static_cast<int32_t>(images->size() - 1);
          coat_nmap_tex.connectedOutputChannel = UVTexture::Channel::RGB;
          coat_nmap_tex.varname_uv = default_uv_name;
          textures->push_back(coat_nmap_tex);

          openpbr_shader->coat_normal.texture_id = static_cast<int32_t>(textures->size() - 1);
        }
      }
    }
  }

  // Check if geometry_coat_tangent has connections
  const auto &coat_tangent_conns = mtlx_openpbr.geometry_coat_tangent.get_connections();
  if (!coat_tangent_conns.empty()) {
    auto coat_tangent_info_result = ExtractMtlxNodeGraphInfo(
        stage, material_prim, coat_tangent_conns, err);
    if (coat_tangent_info_result) {
      const auto &coat_tangent_info = coat_tangent_info_result.value();
      if (coat_tangent_info.has_tangent_rotation) {
        openpbr_shader->coat_tangent_rotation = coat_tangent_info.tangent_rotation;
      }
    }
  }
}

bool RenderSceneConverter::ConvertMaterial(const RenderSceneConverterEnv &env,
                                           const Path &mat_abs_path,
                                           const tinyusdz::Material &material,
                                           RenderMaterial *rmat_out) {
  if (!rmat_out) {
    PUSH_ERROR_AND_RETURN("rmat_out argument is nullptr.");
  }

  RenderMaterial rmat;
  rmat.abs_path = mat_abs_path.prim_part();
  rmat.name = mat_abs_path.element_name();
  DCOUT("rmat.abs_path = " << rmat.abs_path);
  DCOUT("rmat.name = " << rmat.name);
  std::string err;
  Path surfacePath;

  //
  // surface shader
  // First try outputs:surface (standard USD), then outputs:mtlx:surface (MaterialX)
  {
    if (material.surface.authored()) {
      auto paths = material.surface.get_connections();
      DCOUT("paths = " << paths);
      // must have single targetPath.
      if (paths.size() != 1) {
        PUSH_ERROR_AND_RETURN(
            fmt::format("{}'s outputs:surface must be connection with single "
                        "target Path.\n",
                        mat_abs_path.full_path_name()));
      }
      surfacePath = paths[0];
    } else {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "{}'s outputs:surface isn't authored.",
          mat_abs_path.full_path_name()));
    }

    const Prim *shaderPrim{nullptr};
    if (!env.stage.find_prim_at_path(
            Path(surfacePath.prim_part(), /* prop part */ ""), shaderPrim,
            &err)) {
      PUSH_ERROR_AND_RETURN(fmt::format(
          "{}'s outputs:surface isn't connected to exising Prim path.\n",
          mat_abs_path.full_path_name()));
    }

    if (!shaderPrim) {
      // this should not happen though.
      PUSH_ERROR_AND_RETURN("[InternalError] invalid Shader Prim.\n");
    }

    const Shader *shader = shaderPrim->as<Shader>();

    if (!shader) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("{}'s outputs:surface must be connected to Shader Prim, "
                      "but connected to `{}` Prim.\n",
                      shaderPrim->prim_type_name()));
    }

    // Check for UsdPreviewSurface, OpenPBRSurface, MtlxOpenPBRSurface, or MtlxAutodeskStandardSurface
    const UsdPreviewSurface *psurface = shader->value.as<UsdPreviewSurface>();
    const OpenPBRSurface *openpbr = shader->value.as<OpenPBRSurface>();
    const MtlxOpenPBRSurface *mtlx_openpbr = shader->value.as<MtlxOpenPBRSurface>();
    const MtlxAutodeskStandardSurface *mtlx_standard = shader->value.as<MtlxAutodeskStandardSurface>();

    // prop part must be `outputs:surface` for now.
    if (surfacePath.prop_part() != "outputs:surface") {
      PUSH_ERROR_AND_RETURN(
          fmt::format("{}'s outputs:surface connection must point to property "
                      "`outputs:surface`, but got `{}`",
                      mat_abs_path.full_path_name(), surfacePath.prop_part()));
    }

    if (psurface) {
      // Convert UsdPreviewSurface
      PreviewSurfaceShader pss;
      if (!ConvertPreviewSurfaceShader(env, surfacePath, *psurface, &pss)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Failed to convert UsdPreviewSurface : {}", surfacePath.prim_part()));
      }
      rmat.surfaceShader = pss;
    }

    if (openpbr) {
      // Convert OpenPBRSurface
      OpenPBRSurfaceShader openpbr_shader;
      if (!ConvertOpenPBRSurfaceShader(env, surfacePath, *openpbr, &openpbr_shader)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Failed to convert OpenPBRSurface : {}", surfacePath.prim_part()));
      }
      rmat.openPBRShader = openpbr_shader;
    }

    if (mtlx_openpbr) {
      // Convert MtlxOpenPBRSurface (Blender v4.5+ MaterialX export with ND_open_pbr_surface_surfaceshader)
      OpenPBRSurface converted_openpbr =
          ConvertMtlxOpenPBRSurfaceToOpenPBRSurface(*mtlx_openpbr);

      // Convert to OpenPBRSurfaceShader
      OpenPBRSurfaceShader openpbr_shader;
      if (!ConvertOpenPBRSurfaceShader(env, surfacePath, converted_openpbr, &openpbr_shader)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Failed to convert MtlxOpenPBRSurface : {}", surfacePath.prim_part()));
      }

      // Extract tangent rotation, normal map scale, and normal map texture from NodeGraph connections
      const Prim *material_prim{nullptr};
      bool found_prim = env.stage.find_prim_at_path(
              Path(mat_abs_path.prim_part(), /* prop part */ ""), material_prim,
              &err);
      if (found_prim && material_prim) {
        ApplyMtlxGeometryNodeGraphInfoToOpenPBRShader(
            env.stage, material_prim, *mtlx_openpbr,
            env.mesh_config.default_texcoords_primvar_name, &images, &textures,
            &openpbr_shader, &err,
            /*emit_extract_debug_trace*/ false);
      }

      rmat.openPBRShader = openpbr_shader;
    }

    if (mtlx_standard) {
      // Convert MtlxAutodeskStandardSurface (MaterialX StandardSurface via
      // ND_standard_surface_surfaceshader or MtlxAutodeskStandardSurface info:id)
      OpenPBRSurface converted_openpbr =
          ConvertMtlxStandardSurfaceToOpenPBRSurface(*mtlx_standard);

      OpenPBRSurfaceShader openpbr_shader;
      if (!ConvertOpenPBRSurfaceShader(env, surfacePath, converted_openpbr, &openpbr_shader)) {
        PUSH_ERROR_AND_RETURN(fmt::format(
            "Failed to convert MtlxAutodeskStandardSurface : {}", surfacePath.prim_part()));
      }

      // Extract normal map and tangent info from NodeGraph connections
      // StandardSurface uses `normal` and `tangent` fields (not geometry_normal/geometry_tangent)
      const Prim *material_prim{nullptr};
      bool found_prim = env.stage.find_prim_at_path(
              Path(mat_abs_path.prim_part(), ""), material_prim, &err);
      if (found_prim && material_prim) {
        // Normal map extraction
        const auto &normal_conns = mtlx_standard->normal.get_connections();
        if (!normal_conns.empty()) {
          auto normal_info_result = ExtractMtlxNodeGraphInfo(
              env.stage, material_prim, normal_conns, &err);
          if (normal_info_result) {
            ApplyMtlxNormalMapInfoToOpenPBRShader(
                normal_info_result.value(),
                env.mesh_config.default_texcoords_primvar_name,
                &images, &textures, &openpbr_shader);
          }
        }
        // Tangent rotation extraction
        const auto &tangent_conns = mtlx_standard->tangent.get_connections();
        if (!tangent_conns.empty()) {
          auto tangent_info_result = ExtractMtlxNodeGraphInfo(
              env.stage, material_prim, tangent_conns, &err);
          if (tangent_info_result) {
            ApplyMtlxTangentInfoToOpenPBRShader(
                tangent_info_result.value(), &openpbr_shader);
          }
        }
      }

      rmat.openPBRShader = openpbr_shader;
    }

    if (!psurface && !openpbr && !mtlx_openpbr && !mtlx_standard) {
      PUSH_ERROR_AND_RETURN(
          fmt::format("Shader's info:id must be UsdPreviewSurface, OpenPBRSurface, "
                      "ND_open_pbr_surface_surfaceshader, or ND_standard_surface_surfaceshader, but got {}",
                      shader->info_id));
    }
  }

  //
  // Process MaterialX-specific surface shader when MaterialXConfigAPI is present
  // When MaterialXConfigAPI is authored, we look for MaterialX shaders
  {
    // Check if MaterialXConfigAPI is applied (via materialXConfig field)
    // For now, we only check the materialXConfig field as apiSchemas checking would need
    // proper MaterialXConfigAPI enum support in APISchemas::APIName
    bool has_materialx_api = material.materialXConfig.has_value();

    if (has_materialx_api) {
      DCOUT("Material has MaterialXConfigAPI, looking for MaterialX shaders");

      // First try to parse outputs:mtlx:surface connection
      Path mtlxSurfacePath;
      bool has_mtlx_surface = false;

      // Try to find the connection in various forms
      for (const auto& prop_name : {"outputs:mtlx:surface.connect", "outputs:mtlx:surface"}) {
        auto it = material.props.find(prop_name);
        if (it != material.props.end()) {
          if (it->second.is_relationship()) {
            auto targets = it->second.get_relationTargets();
            if (!targets.empty()) {
              mtlxSurfacePath = targets[0];
              has_mtlx_surface = true;
              DCOUT("Found MaterialX surface connection via relationship: " << mtlxSurfacePath);
              break;
            }
          } else if (it->second.is_attribute()) {
            // Try to extract path from attribute
            auto attr = it->second.get_attribute();
            if (auto token_val = attr.get_value<value::token>()) {
              std::string path_str = token_val.value().str();
              if (!path_str.empty()) {
                // Remove brackets if present
                if (path_str.front() == '<' && path_str.back() == '>') {
                  path_str = path_str.substr(1, path_str.size() - 2);
                }
                // Parse the path
                size_t pos = path_str.find(".outputs:");
                if (pos != std::string::npos) {
                  std::string prim_path = path_str.substr(0, pos);
                  mtlxSurfacePath = Path(prim_path, "");
                  has_mtlx_surface = true;
                  DCOUT("Found MaterialX surface connection via token: " << mtlxSurfacePath);
                  break;
                }
              }
            }
          }
        }
      }

      // If direct connection parsing failed, look for child Shader prims with OpenPBR info:id
      if (!has_mtlx_surface) {
        DCOUT("Direct connection not found, searching for child shaders with OpenPBR info:id");

        // Get the material prim from the stage to access its children
        const Prim* mat_prim = nullptr;
        if (env.stage.find_prim_at_path(mat_abs_path, mat_prim, &err)) {
          if (mat_prim) {
            // Iterate through children to find OpenPBR shader
            for (const auto& child : mat_prim->children()) {
              const Shader* shader = child.as<Shader>();
              if (shader) {
                // Check if this is an OpenPBR shader by its info:id
                if (shader->info_id == kNdOpenPbrSurfaceSurfaceshader ||
                    shader->info_id == "ND_open_pbr_surface_surfaceshader") {
                  Path child_path = mat_abs_path;
                  child_path = child_path.append_element(child.element_name());
                  mtlxSurfacePath = child_path;
                  has_mtlx_surface = true;
                  DCOUT("Found OpenPBR shader child: " << child_path);
                  break;
                }
              }
            }
          }
        }
      }

      // Process the found MaterialX shader
      if (has_mtlx_surface) {
        const Prim *mtlxShaderPrim{nullptr};
        if (!env.stage.find_prim_at_path(
                Path(mtlxSurfacePath.prim_part(), /* prop part */ ""), mtlxShaderPrim,
                &err)) {
          PUSH_ERROR_AND_RETURN(fmt::format(
              "MaterialX shader path {} not found in stage",
              mtlxSurfacePath.full_path_name()));
        } else if (!mtlxShaderPrim) {
          PUSH_ERROR_AND_RETURN(fmt::format(
              "[InternalError] MaterialX shader path {} resolved to nullptr",
              mtlxSurfacePath.full_path_name()));
        } else {
          const Shader *mtlxShader = mtlxShaderPrim->as<Shader>();

          if (!mtlxShader) {
            PUSH_ERROR_AND_RETURN(fmt::format(
                "MaterialX surface path {} must point to a Shader Prim",
                mtlxSurfacePath.full_path_name()));
          }

          // Check if it's an OpenPBR shader
          const MtlxOpenPBRSurface *mtlx_openpbr =
              mtlxShader->value.as<MtlxOpenPBRSurface>();

          if (mtlx_openpbr) {
            DCOUT("Converting MtlxOpenPBRSurface to RenderMaterial");

            OpenPBRSurface converted_openpbr =
                ConvertMtlxOpenPBRSurfaceToOpenPBRSurface(*mtlx_openpbr);

            // Convert to OpenPBRSurfaceShader
            OpenPBRSurfaceShader openpbr_shader;
            if (!ConvertOpenPBRSurfaceShader(env, mtlxSurfacePath,
                                             converted_openpbr,
                                             &openpbr_shader)) {
              PUSH_ERROR_AND_RETURN(fmt::format(
                  "Failed to convert MtlxOpenPBRSurface : {}",
                  mtlxSurfacePath.prim_part()));
            } else {
              // Extract normal map texture from NodeGraph connections
              const Prim *material_prim_for_ng = nullptr;
              if (!env.stage.find_prim_at_path(mat_abs_path, material_prim_for_ng,
                                               &err)) {
                DCOUT("Could not find material prim at "
                      << mat_abs_path.full_path_name());
                material_prim_for_ng = nullptr;
              }

              ApplyMtlxGeometryNodeGraphInfoToOpenPBRShader(
                  env.stage, material_prim_for_ng, *mtlx_openpbr,
                  env.mesh_config.default_texcoords_primvar_name, &images,
                  &textures, &openpbr_shader, &err,
                  /*emit_extract_debug_trace*/ false);

              rmat.openPBRShader = openpbr_shader;
              DCOUT("Successfully attached MaterialX OpenPBR shader to "
                    "RenderMaterial: "
                    << mtlxSurfacePath.full_path_name());
            }
          } else {
            PUSH_ERROR_AND_RETURN(fmt::format(
                "Found shader {} but it's not "
                "ND_open_pbr_surface_surfaceshader (got {})",
                mtlxSurfacePath.prim_part(), mtlxShader->info_id));
          }
        }
      } else {
        DCOUT("No MaterialX OpenPBR shader found for material with MaterialXConfigAPI");
      }
    }
  }

  //
  // displacement output (outputs:displacement)
  //
  if (material.displacement.authored()) {
    auto disp_paths = material.displacement.get_connections();
    if (disp_paths.size() == 1) {
      rmat.has_displacement = true;
      rmat.displacement_shader_path = disp_paths[0].full_path_name();
      DCOUT("Material has displacement shader: " << rmat.displacement_shader_path);
    }
  }

  //
  // volume output (outputs:volume)
  //
  if (material.volume.authored()) {
    auto vol_paths = material.volume.get_connections();
    if (vol_paths.size() == 1) {
      rmat.has_volume = true;
      rmat.volume_shader_path = vol_paths[0].full_path_name();
      DCOUT("Material has volume shader: " << rmat.volume_shader_path);
    }
  }

  DCOUT("Converted Material: " << mat_abs_path);

  (*rmat_out) = rmat;
  return true;
}

bool MeshVisitor(const tinyusdz::Path &abs_path, const tinyusdz::Prim &prim,
                 const int32_t level, void *userdata, std::string *err) {
  if (!userdata) {
    if (err) {
      (*err) += "userdata pointer must be filled.";
    }
    return false;
  }

  MeshVisitorEnv *visitorEnv = reinterpret_cast<MeshVisitorEnv *>(userdata);

  if (size_t(level) > kMaxDefaultTraversalLimit) {
    if (err) {
      (*err) += "Scene graph is too deep.\n";
    }
    // Too deep
    return false;
  }

  // Lambda to convert and cache bound materials - shared by all geometry types
  auto ConvertBoundMaterial = [&](const Path &bound_material_path,
                                  const tinyusdz::Material *bound_material,
                                  int64_t &rmaterial_id) -> bool {
    std::vector<RenderMaterial> &rmaterials =
        visitorEnv->converter->materials;

    const auto matIt = visitorEnv->converter->materialMap.find(
        bound_material_path.full_path_name());

    if (matIt != visitorEnv->converter->materialMap.s_end()) {
      // Got material in the cache.
      uint64_t mat_id = matIt->second;
      if (mat_id >= visitorEnv->converter->materials
                        .size()) {  // this should not happen though
        if (err) {
          (*err) += "Material index out-of-range.\n";
        }
        return false;
      }

      if (mat_id >= size_t((std::numeric_limits<int32_t>::max)())) {
        if (err) {
          (*err) += "Material index too large.\n";
        }
        return false;
      }

      rmaterial_id = int64_t(mat_id);

    } else {
      RenderMaterial rmat;
      if (!visitorEnv->converter->ConvertMaterial(*visitorEnv->env,
                                                  bound_material_path,
                                                  *bound_material, &rmat)) {
        if (err) {
          (*err) += fmt::format("Material conversion failed: {}",
                                bound_material_path);
        }
        return false;
      }

      // Assign new material ID
      uint64_t mat_id = rmaterials.size();

      if (mat_id >= uint64_t((std::numeric_limits<int32_t>::max)())) {
        if (err) {
          (*err) += "Material index too large.\n";
        }
        return false;
      }
      rmaterial_id = int64_t(mat_id);

      visitorEnv->converter->materialMap.add(
          bound_material_path.full_path_name(), uint64_t(rmaterial_id));
      // Compute material tag for render pass sorting (opaque/translucent/masked)
      rmat.computeMaterialTag();

      DCOUT("Added renderMaterial: " << mat_id << " " << rmat.abs_path
                                     << " ( " << rmat.name << " ) ");

      rmaterials.push_back(rmat);
    }

    return true;
  };

  auto ResolveBoundMaterial = [&](const Path &query_path,
                                  const std::string &purpose,
                                  Path *bound_material_path,
                                  const Material **bound_material,
                                  bool *found) -> bool {
    if (!found) {
      return false;
    }

    std::string local_err;
    bool local_found = visitorEnv->converter->GetBoundMaterialCached(
        visitorEnv->env->stage, query_path, purpose, bound_material_path,
        bound_material, &local_err);

    if (!local_err.empty()) {
      if (err) {
        (*err) += local_err;
      }
      return false;
    }

    (*found) = local_found;
    return true;
  };

  if (const tinyusdz::GeomMesh *pmesh = prim.as<tinyusdz::GeomMesh>()) {
    // Collect GeomSubsets
    // std::vector<const tinyusdz::GeomSubset *> subsets = GetGeomSubsets(;

    DCOUT("Mesh: " << abs_path);

    if (!pmesh->points.authored()) {
      // Maybe Collider mesh? Ignore for now.
      DCOUT(fmt::format("Mesh {} does not author `points` attribute(Maybe Collider mesh?). Ignore it for now", abs_path));
      return true;
    }

    //
    // First convert Material assigned to GeomMesh.
    //
    // - If prim has GeomSubset with materialBind, convert it to per-face
    // material.
    // - If prim has materialBind, convert it to RenderMesh's material.
    //

    // Convert bound materials in GeomSubsets
    //
    // key: subset Prim name
    std::map<std::string, MaterialPath> subset_material_path_map;
    std::vector<const GeomSubset *> material_subsets;
    {
      material_subsets = GetMaterialBindGeomSubsets(prim);

      for (const auto &psubset : material_subsets) {
        MaterialPath mpath;
        mpath.default_texcoords_primvar_name =
            visitorEnv->env->mesh_config.default_texcoords_primvar_name;

        Path subset_abs_path = abs_path.AppendElement(psubset->name);

        // front and back
        {
          tinyusdz::Path bound_material_path;
          const tinyusdz::Material *bound_material{nullptr};
          bool ret{false};
          if (!ResolveBoundMaterial(
                  /* GeomSubset prim path */ subset_abs_path,
                  /* purpose */ "", &bound_material_path, &bound_material,
                  &ret)) {
            return false;
          }

          if (ret && bound_material) {
            int64_t rmaterial_id = -1;  // not used.

            if (!ConvertBoundMaterial(bound_material_path, bound_material,
                                      rmaterial_id)) {
              if (err) {
                (*err) += "Convert boundMaterial failed: " + bound_material_path.full_path_name();
              }
              return false;
            }

            mpath.material_path = bound_material_path.full_path_name();
            DCOUT("GeomSubset " << subset_abs_path << " : Bound material path: "
                                << mpath.backface_material_path);
          }
        }

        std::string backface_purpose =
            visitorEnv->env->material_config
                .default_backface_material_purpose_name;

        if (!backface_purpose.empty() &&
            psubset->has_materialBinding(value::token(backface_purpose))) {
          DCOUT("backface_material_purpose "
                << visitorEnv->env->material_config
                       .default_backface_material_purpose_name);
          tinyusdz::Path bound_material_path;
          const tinyusdz::Material *bound_material{nullptr};
          bool ret{false};
          if (!ResolveBoundMaterial(
                  /* GeomSubset prim path */ subset_abs_path,
                  /* purpose */
                  visitorEnv->env->material_config
                      .default_backface_material_purpose_name,
                  &bound_material_path, &bound_material, &ret)) {
            return false;
          }

          if (ret && bound_material) {
            int64_t rmaterial_id = -1;  // not used

            if (!ConvertBoundMaterial(bound_material_path, bound_material,
                                      rmaterial_id)) {
              if (err) {
                (*err) += "Convert boundMaterial failed: " + bound_material_path.full_path_name();
              }
              return false;
            }

            mpath.backface_material_path = bound_material_path.full_path_name();
            DCOUT("GeomSubset " << subset_abs_path
                                << " : Bound backface material path: "
                                << mpath.backface_material_path);
          }
        }

        subset_material_path_map[psubset->name] = mpath;
      }
    }

    MaterialPath material_path;
    material_path.default_texcoords_primvar_name =
        visitorEnv->env->mesh_config.default_texcoords_primvar_name;
    // TODO: Implement feature to assign default material
    // id(MaterialPath::default_material_id) when no bound material found.

    {
      const std::string mesh_path_str = abs_path.full_path_name();

      // Front and back material.
      {
        tinyusdz::Path bound_material_path;
        const tinyusdz::Material *bound_material{nullptr};
        bool ret{false};
        if (!ResolveBoundMaterial(
                /* GeomMesh prim path */ abs_path,
                /* purpose */ "", &bound_material_path, &bound_material,
                &ret)) {
          return false;
        }

        DCOUT("Bound material found: " << ret);
        if (ret && bound_material) {
          int64_t rmaterial_id = -1;  // not used

          if (!ConvertBoundMaterial(bound_material_path, bound_material,
                                    rmaterial_id)) {
            if (err) {
              (*err) += "Convert boundMaterial failed: " + bound_material_path.full_path_name();
            }
            return false;
          }

          material_path.material_path = bound_material_path.full_path_name();
          DCOUT("Bound material path: " << material_path.material_path);
        }
      }

      std::string backface_purpose =
          visitorEnv->env->material_config
              .default_backface_material_purpose_name;

      if (!backface_purpose.empty() &&
          pmesh->has_materialBinding(value::token(backface_purpose))) {
        tinyusdz::Path bound_material_path;
        const tinyusdz::Material *bound_material{nullptr};
        bool ret{false};
        if (!ResolveBoundMaterial(
                /* GeomMesh prim path */ abs_path,
                /* purpose */
                visitorEnv->env->material_config
                    .default_backface_material_purpose_name,
                &bound_material_path, &bound_material, &ret)) {
          return false;
        }

        if (ret && bound_material) {
          int64_t rmaterial_id = -1;  // not used

          if (!ConvertBoundMaterial(bound_material_path, bound_material,
                                    rmaterial_id)) {
            if (err) {
              (*err) += "Convert boundMaterial failed: " + bound_material_path.full_path_name();
            }
            return false;
          }

          material_path.backface_material_path =
              bound_material_path.full_path_name();
          DCOUT("Bound backface material path: "
                << material_path.backface_material_path);
        }
      }

      // BlendShapes
      std::vector<std::pair<std::string, const BlendShape *>> blendshapes;
      {
        std::string local_err;
        blendshapes = GetBlendShapes(visitorEnv->env->stage, prim, &local_err);
        if (local_err.size()) {
          if (err) {
            (*err) += fmt::format("Failed to get BlendShapes prims. err = {}", local_err);
          }
          return false;
        }
      }
      DCOUT("# of blendshapes : " << blendshapes.size());

      RenderMesh rmesh;

      if (!visitorEnv->converter->ConvertMesh(
              *visitorEnv->env, abs_path, *pmesh, material_path,
              subset_material_path_map, visitorEnv->converter->materialMap,
              material_subsets, blendshapes, &rmesh)) {
        if (err) {
          (*err) += fmt::format("Mesh conversion failed: {}",
                                abs_path.full_path_name());
          (*err) += "\n" + visitorEnv->converter->GetError() + "\n";

        }
        return false;
      }

      uint64_t mesh_id = uint64_t(visitorEnv->converter->meshes.size());
      if (mesh_id >= size_t((std::numeric_limits<int32_t>::max)())) {
        if (err) {
          (*err) += "Mesh index too large.\n";
        }
        return false;
      }
      visitorEnv->converter->meshMap.add(abs_path.full_path_name(), mesh_id);

      visitorEnv->converter->meshes.emplace_back(std::move(rmesh));

      // Report mesh progress
      visitorEnv->meshes_processed++;
      std::string msg = "Converting mesh " +
          std::to_string(visitorEnv->meshes_processed) + "/" +
          std::to_string(visitorEnv->meshes_total);
      if (!visitorEnv->converter->ReportMeshProgress(
              visitorEnv->meshes_processed, visitorEnv->meshes_total,
              abs_path.full_path_name(), msg)) {
        if (err) {
          (*err) += "Conversion cancelled by user.\n";
        }
        return false;
      }
      DCOUT("[Tydra] Mesh " << visitorEnv->meshes_processed << "/" << visitorEnv->meshes_total
            << ": " << abs_path.full_path_name());
    }
  }

  // Handle GeomCube primitives by converting to mesh
  if (const tinyusdz::GeomCube *pcube = prim.as<tinyusdz::GeomCube>()) {
    DCOUT("Cube: " << abs_path);

    // Get material binding (same logic as GeomMesh)
    MaterialPath material_path;
    std::map<std::string, MaterialPath> subset_material_path_map;

    {
      const Material *bound_material{nullptr};
      Path bound_material_path;

      bool ret{false};
      if (!ResolveBoundMaterial(abs_path,
                                /* purpose */ "",
                                &bound_material_path, &bound_material,
                                &ret)) {
        return false;
      }

      if (ret && bound_material) {
        int64_t rmaterial_id = -1;

        if (!ConvertBoundMaterial(
                bound_material_path, bound_material, rmaterial_id)) {
          if (err) {
            (*err) += "Convert boundMaterial failed: " +
                      bound_material_path.full_path_name();
          }
          return false;
        }

        material_path.material_path = bound_material_path.full_path_name();
        DCOUT("Bound material path: " << material_path.material_path);
      }
    }

    RenderMesh rmesh;
    std::vector<const tinyusdz::GeomSubset *> material_subsets;  // Cubes don't have subsets
    std::vector<std::pair<std::string, const tinyusdz::BlendShape *>> blendshapes;  // Cubes don't have blendshapes

    if (!visitorEnv->converter->ConvertCube(
            *visitorEnv->env, abs_path, *pcube, material_path,
            subset_material_path_map, visitorEnv->converter->materialMap,
            material_subsets, blendshapes, &rmesh)) {
      if (err) {
        (*err) += fmt::format("Cube conversion failed: {}",
                              abs_path.full_path_name());
        (*err) += "\n" + visitorEnv->converter->GetError() + "\n";
      }
      return false;
    }

    uint64_t mesh_id = uint64_t(visitorEnv->converter->meshes.size());
    if (mesh_id >= size_t((std::numeric_limits<int32_t>::max)())) {
      if (err) {
        (*err) += "Mesh index too large.\n";
      }
      return false;
    }
    visitorEnv->converter->meshMap.add(abs_path.full_path_name(), mesh_id);
    visitorEnv->converter->meshes.emplace_back(std::move(rmesh));

    // Report mesh progress (cube)
    visitorEnv->meshes_processed++;
    std::string msg = "Converting cube " +
        std::to_string(visitorEnv->meshes_processed) + "/" +
        std::to_string(visitorEnv->meshes_total);
    if (!visitorEnv->converter->ReportMeshProgress(
            visitorEnv->meshes_processed, visitorEnv->meshes_total,
            abs_path.full_path_name(), msg)) {
      if (err) {
        (*err) += "Conversion cancelled by user.\n";
      }
      return false;
    }
    DCOUT("[Tydra] Mesh " << visitorEnv->meshes_processed << "/" << visitorEnv->meshes_total
          << " (cube): " << abs_path.full_path_name());
  }

  // Handle GeomSphere primitives by converting to mesh
  if (const tinyusdz::GeomSphere *psphere = prim.as<tinyusdz::GeomSphere>()) {
    DCOUT("Sphere: " << abs_path);

    // Get material binding (same logic as GeomMesh)
    MaterialPath material_path;
    std::map<std::string, MaterialPath> subset_material_path_map;

    {
      const Material *bound_material{nullptr};
      Path bound_material_path;

      bool ret{false};
      if (!ResolveBoundMaterial(abs_path,
                                /* purpose */ "",
                                &bound_material_path, &bound_material,
                                &ret)) {
        return false;
      }

      if (ret && bound_material) {
        int64_t rmaterial_id = -1;

        if (!ConvertBoundMaterial(
                bound_material_path, bound_material, rmaterial_id)) {
          if (err) {
            (*err) += "Convert boundMaterial failed: " +
                      bound_material_path.full_path_name();
          }
          return false;
        }

        material_path.material_path = bound_material_path.full_path_name();
        DCOUT("Bound material path: " << material_path.material_path);
      }
    }

    RenderMesh rmesh;
    std::vector<const tinyusdz::GeomSubset *> material_subsets;  // Spheres don't have subsets
    std::vector<std::pair<std::string, const tinyusdz::BlendShape *>> blendshapes;  // Spheres don't have blendshapes

    if (!visitorEnv->converter->ConvertSphere(
            *visitorEnv->env, abs_path, *psphere, material_path,
            subset_material_path_map, visitorEnv->converter->materialMap,
            material_subsets, blendshapes, &rmesh)) {
      if (err) {
        (*err) += fmt::format("Sphere conversion failed: {}",
                              abs_path.full_path_name());
        (*err) += "\n" + visitorEnv->converter->GetError() + "\n";
      }
      return false;
    }

    uint64_t mesh_id = uint64_t(visitorEnv->converter->meshes.size());
    if (mesh_id >= size_t((std::numeric_limits<int32_t>::max)())) {
      if (err) {
        (*err) += "Mesh index too large.\n";
      }
      return false;
    }
    visitorEnv->converter->meshMap.add(abs_path.full_path_name(), mesh_id);
    visitorEnv->converter->meshes.emplace_back(std::move(rmesh));

    // Report mesh progress (sphere)
    visitorEnv->meshes_processed++;
    std::string msg = "Converting sphere " +
        std::to_string(visitorEnv->meshes_processed) + "/" +
        std::to_string(visitorEnv->meshes_total);
    if (!visitorEnv->converter->ReportMeshProgress(
            visitorEnv->meshes_processed, visitorEnv->meshes_total,
            abs_path.full_path_name(), msg)) {
      if (err) {
        (*err) += "Conversion cancelled by user.\n";
      }
      return false;
    }
    DCOUT("[Tydra] Mesh " << visitorEnv->meshes_processed << "/" << visitorEnv->meshes_total
          << " (sphere): " << abs_path.full_path_name());
  }

  return true;  // continue traversal
}

}  // namespace tydra
}  // namespace tinyusdz
