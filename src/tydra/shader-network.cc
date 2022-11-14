#include "shader-network.hh"

#include "usdShade.hh"
#include "pprinter.hh"
#include "prim-pprint.hh"
#include "value-pprint.hh"

namespace tinyusdz {
namespace tydra {

namespace {

bool EvaluateUsdPreviewSurfaceAttribute(
  const Stage &stage,
  const UsdPreviewSurface &shader,
  const std::string &attr_name,
  const uint32_t req_type_id,
  value::Value &out_val,
  std::string *err,
  const value::TimeCode timeCode) {

  (void)stage;

  if ((attr_name == "diffuseColor") && (req_type_id == value::TypeTraits<value::color3f>::type_id())) {
    if (shader.diffuseColor.authored()) {

    } else {
      value::color3f col;
      if (shader.diffuseColor.get_value().get_scalar(&col)) {
        out_val = col;
        return true;
      }
    }
  }

  (void)err;
  (void)timeCode;

  return false;
}


} // namespace local


template<typename T>
bool EvaluateShaderAttribute(
  const Stage &stage,
  const Shader &shader, const std::string &attr_name,
  T * out_val,
  std::string *err,
  const value::TimeCode timeCode) {

  if (!out_val) {
    return false;
  }

  uint32_t tyid = value::TypeTraits<T>::type_id();
  value::Value outval;

  bool result = false;

  if (const auto *psurf = shader.value.as<UsdPreviewSurface>()) {
    result = EvaluateUsdPreviewSurfaceAttribute(stage, *psurf, attr_name, tyid, outval, err, timeCode);
    if (const auto pt = outval.as<T>()) {
      (*out_val) = (*pt);
    } else {
      if (err) {
        (*err) += "[InternalError] Type mismatch.\n";
      }
      return false;
    }
  } else {
    if (err) {
      (*err) += "Unsupported shader type: " + shader.value.type_name() + "\n";
    }
    return false;
  }

  return result;
}

#define INSTANCIATE_EVAL_SHADER(__ty) \
template bool EvaluateShaderAttribute( const Stage &stage, const Shader &shader, const std::string &attr_name, __ty * out_val, std::string *err, const value::TimeCode timeCode)

INSTANCIATE_EVAL_SHADER(value::token);
INSTANCIATE_EVAL_SHADER(std::string);
INSTANCIATE_EVAL_SHADER(value::half);
INSTANCIATE_EVAL_SHADER(value::half2);
INSTANCIATE_EVAL_SHADER(value::half3);
INSTANCIATE_EVAL_SHADER(value::half4);
INSTANCIATE_EVAL_SHADER(int32_t);
INSTANCIATE_EVAL_SHADER(value::int2);
INSTANCIATE_EVAL_SHADER(value::int3);
INSTANCIATE_EVAL_SHADER(value::int4);
INSTANCIATE_EVAL_SHADER(uint32_t);
INSTANCIATE_EVAL_SHADER(value::uint2);
INSTANCIATE_EVAL_SHADER(value::uint3);
INSTANCIATE_EVAL_SHADER(value::uint4);
INSTANCIATE_EVAL_SHADER(float);
INSTANCIATE_EVAL_SHADER(value::float2);
INSTANCIATE_EVAL_SHADER(value::float3);
INSTANCIATE_EVAL_SHADER(value::float4);
INSTANCIATE_EVAL_SHADER(double);
INSTANCIATE_EVAL_SHADER(value::double2);
INSTANCIATE_EVAL_SHADER(value::double3);
INSTANCIATE_EVAL_SHADER(value::double4);
INSTANCIATE_EVAL_SHADER(value::quath);
INSTANCIATE_EVAL_SHADER(value::quatf);
INSTANCIATE_EVAL_SHADER(value::quatd);
INSTANCIATE_EVAL_SHADER(value::color3h);
INSTANCIATE_EVAL_SHADER(value::color3f);
INSTANCIATE_EVAL_SHADER(value::color3d);
INSTANCIATE_EVAL_SHADER(value::color4h);
INSTANCIATE_EVAL_SHADER(value::color4f);
INSTANCIATE_EVAL_SHADER(value::color4d);
INSTANCIATE_EVAL_SHADER(value::vector3h);
INSTANCIATE_EVAL_SHADER(value::vector3f);
INSTANCIATE_EVAL_SHADER(value::vector3d);
INSTANCIATE_EVAL_SHADER(value::point3h);
INSTANCIATE_EVAL_SHADER(value::point3f);
INSTANCIATE_EVAL_SHADER(value::point3d);
INSTANCIATE_EVAL_SHADER(value::normal3h);
INSTANCIATE_EVAL_SHADER(value::normal3f);
INSTANCIATE_EVAL_SHADER(value::normal3d);
INSTANCIATE_EVAL_SHADER(value::matrix2d);
INSTANCIATE_EVAL_SHADER(value::matrix3d);
INSTANCIATE_EVAL_SHADER(value::matrix4d);

// instanciations

} // namespace tydra
} // namespace tinyusdz
