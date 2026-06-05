// SPDX-License-Identifier: Apache 2.0
// Copyright present, Light Transport Entertainment, Inc.
//
// value::Value pretty-print dispatch — split out of value-pprint.cc.
// Holds pprint_value(), the big switch-on-type_id that renders a value::Value:
// base value types via operator<<, 1D arrays via print_1d_array, and ~60 schema
// prim types (GeomMesh/Material/Shader/...) via to_string(). The schema arms are
// the only thing in value-pprint.cc that needed usdGeom.hh/usdLux.hh/core/prim.hh;
// isolating them lets value-pprint.cc (operator<< + to_string for value types)
// drop those heavy schema headers. operator<< / to_string are declared in
// value-pprint.hh / pprinter.hh and resolved cross-TU.
#include "value-pprint.hh"

#include <sstream>

#include "pprinter.hh"
#include "core/prim.hh"
#include "str-util.hh"
#include "value-types.hh"

#include "common-macros.inc"

namespace tinyusdz {
namespace value {

// Simple brute-force way..
// TODO: Use std::function or some template technique?
// NOTE: Use dedicated path for `float` and `double`

#define CASE_EXPR_LIST(__FUNC) \
  __FUNC(bool)                 \
  __FUNC(half)                 \
  __FUNC(half2)                \
  __FUNC(half3)                \
  __FUNC(half4)                \
  __FUNC(int32_t)              \
  __FUNC(uint32_t)             \
  __FUNC(int2)                 \
  __FUNC(int3)                 \
  __FUNC(int4)                 \
  __FUNC(uint2)                \
  __FUNC(uint3)                \
  __FUNC(uint4)                \
  __FUNC(int64_t)              \
  __FUNC(uint64_t)             \
  __FUNC(float2)               \
  __FUNC(float3)               \
  __FUNC(float4)               \
  __FUNC(double2)              \
  __FUNC(double3)              \
  __FUNC(double4)              \
  __FUNC(matrix2f)             \
  __FUNC(matrix3f)             \
  __FUNC(matrix4f)             \
  __FUNC(matrix2d)             \
  __FUNC(matrix3d)             \
  __FUNC(matrix4d)             \
  __FUNC(quath)                \
  __FUNC(quatf)                \
  __FUNC(quatd)                \
  __FUNC(normal3h)             \
  __FUNC(normal3f)             \
  __FUNC(normal3d)             \
  __FUNC(vector3h)             \
  __FUNC(vector3f)             \
  __FUNC(vector3d)             \
  __FUNC(point3h)              \
  __FUNC(point3f)              \
  __FUNC(point3d)              \
  __FUNC(color3h)              \
  __FUNC(color3f)              \
  __FUNC(color3d)              \
  __FUNC(color4h)              \
  __FUNC(color4f)              \
  __FUNC(color4d)              \
  __FUNC(texcoord2h)           \
  __FUNC(texcoord2f)           \
  __FUNC(texcoord2d)           \
  __FUNC(texcoord3h)           \
  __FUNC(texcoord3f)           \
  __FUNC(texcoord3d)           \
  __FUNC(frame4d)              \
  __FUNC(timecode)



std::string pprint_value(const value::Value &v, const uint32_t indent,
                         bool closing_brace) {
  // Schema prim types (Model/Xform/GeomMesh/.../SpatialAudio) are rendered
  // out-of-line by pprint_prim_value() in value-pprint-prim.cc — same
  // [MODEL_BEGIN, MODEL_END) detection Prim uses — so this TU need not
  // instantiate v.as<PrimType>()/to_string() for ~60 schema types.
  {
    const uint32_t prim_tid = v.type_id();
    if (prim_tid >= static_cast<uint32_t>(value::TypeId::TYPE_ID_MODEL_BEGIN) &&
        prim_tid < static_cast<uint32_t>(value::TypeId::TYPE_ID_MODEL_END)) {
      return pprint_prim_value(v, indent, closing_brace);
    }
  }
#define BASETYPE_CASE_EXPR(__ty)                           \
  case TypeTraits<__ty>::type_id(): {                      \
    auto p = v.as<__ty>();                                 \
    if (p) {                                               \
      os << (*p);                                          \
    } else {                                               \
      os << "[InternalError: Base type TypeId mismatch.]"; \
    }                                                      \
    break;                                                 \
  }


#define ARRAY1DTYPE_CASE_EXPR(__ty)                      \
  case TypeTraits<std::vector<__ty>>::type_id(): {       \
    if (auto p = v.as<std::vector<__ty>>()) {            \
      os << (*p);                                        \
    } else if (auto tp = v.as<TypedArray<__ty>>()) {            \
      os << (*tp);                                        \
    } else if (auto ctp = v.as<ChunkedTypedArray<__ty>>()) {            \
      os << (*ctp);                                        \
    } else {                                             \
      os << "[InternalError: 1D type TypeId mismatch.]"; \
    }                                                    \
    break;                                               \
  }

  std::stringstream os;

  switch (v.type_id()) {
    // base type
    CASE_EXPR_LIST(BASETYPE_CASE_EXPR)

    case TypeTraits<float>::type_id(): {
      auto p = v.as<float>();
      if (p) {
        os << dtos(*p);
      } else {
        os << "[InternalError: TypeId mismatch(`float` expected).]";
      }
      break;
    }

    case TypeTraits<double>::type_id(): {
      auto p = v.as<double>();
      if (p) {
        os << dtos(*p);
      } else {
        os << "[InternalError: TypeId mismatch(`double` expected).]";
      }
      break;
    }

      // 1D array
      CASE_EXPR_LIST(ARRAY1DTYPE_CASE_EXPR)

    case TypeTraits<std::vector<float>>::type_id(): {
      auto p = v.as<std::vector<float>>();
      if (p) {
        os << (*p);
      } else {
        os << "[InternalError: TypeId mismatch(`float[]` expected).]";
      }
      break;
    }

    case TypeTraits<std::vector<double>>::type_id(): {
      auto p = v.as<std::vector<double>>();
      if (p) {
        os << (*p);
      } else {
        os << "[InternalError: TypeId mismatch(`double[]` expected).]";
      }
      break;
    }

      // 2D array
      // CASE_EXPR_LIST(ARRAY2DTYPE_CASE_EXPR)


    // dict and customData
    case TypeTraits<CustomDataType>::type_id(): {
      auto p = v.as<CustomDataType>();
      if (p) {
        os << print_customData(*p, "", indent);
      } else {
        os << "[InternalError: Dict type TypeId mismatch.]";
      }
      break;
    }
    case TypeTraits<value::AssetPath>::type_id(): {
      auto p = v.as<value::AssetPath>();
      if (p) {
        os << (*p);
      } else {
        os << "[InternalError: asset type TypeId mismatch.]";
      }
      break;
    }
    case TypeTraits<std::vector<value::AssetPath>>::type_id(): {
      auto p = v.as<std::vector<value::AssetPath>>();
      if (p) {
        os << (*p);
      } else {
        os << "[InternalError: asset[] type TypeId mismatch.]";
      }
      break;
    }

    case TypeTraits<value::token>::type_id(): {
      auto p = v.as<value::token>();
      if (p) {
        os << buildEscapedAndQuotedStringForUSDA(p->str());
      } else {
        os << "[InternalError: Token type TypeId mismatch.]";
      }
      break;
    }
    case TypeTraits<std::vector<value::token>>::type_id(): {
      auto p = v.get_value<std::vector<value::token>>();
      if (p) {
        std::vector<std::string> vs;
        std::transform(p->begin(), p->end(), std::back_inserter(vs),
                       [](const value::token &tok) {
                         return buildEscapedAndQuotedStringForUSDA(tok.str());
                       });

        os << vs;
      } else {
        os << "[InternalError: `token[]` type TypeId mismatch.]";
      }
      break;
    }
    case TypeTraits<std::string>::type_id(): {
      auto p = v.as<std::string>();
      if (p) {
        os << buildEscapedAndQuotedStringForUSDA(*p);
      } else {
        os << "[InternalError: `string` type TypeId mismatch.]";
      }
      break;
    }
    case TypeTraits<value::StringData>::type_id(): {
      auto p = v.as<value::StringData>();
      if (p) {
        os << (*p);  // FIXME: Call buildEscapedAndQuotedStringForUSDA() here?
      } else {
        os << "[InternalError: `string` type TypeId mismatch.]";
      }
      break;
    }
    case TypeTraits<std::vector<std::string>>::type_id(): {
      auto p = v.as<std::vector<std::string>>();
      if (p) {
        std::vector<std::string> ss;
        for (const auto &item : *p) {
          ss.push_back(buildEscapedAndQuotedStringForUSDA(item));
        }
        os << ss;  // Use operator<<(std::vector<std::string>)
      } else {
        os << "[InternalError: `string[]` type TypeId mismatch.]";
      }
      break;
    }
    case TypeTraits<std::vector<value::StringData>>::type_id(): {
      auto p = v.as<std::vector<value::StringData>>();
      if (p) {
        os << (*p);
      } else {
        os << "[InternalError: `string[]` type TypeId mismatch.]";
      }
      break;
    }
    // uchar (uint8_t) needs special handling: os << uint8_t prints as char
    case TypeTraits<uint8_t>::type_id(): {
      auto p = v.as<uint8_t>();
      if (p) {
        os << static_cast<int>(*p);
      } else {
        os << "[InternalError: uchar TypeId mismatch.]";
      }
      break;
    }
    // uchar[] (std::vector<uint8_t>): operator<< prints elements as integers.
    case TypeTraits<std::vector<uint8_t>>::type_id(): {
      if (auto p = v.as<std::vector<uint8_t>>()) {
        os << (*p);
      } else {
        os << "[InternalError: uchar[] TypeId mismatch.]";
      }
      break;
    }
    case TypeTraits<value::ValueBlock>::type_id(): {
      if (v.as<value::ValueBlock>()) {
        os << "None";
      } else {
        os << "[InternalError: ValueBlock type TypeId mismatch.]";
      }
      break;
    }
    // TODO: List-up all case and remove `default` clause.
    default: {
      os << "VALUE_PPRINT: TODO: (type: " << v.type_name() << ") ";
    }
  }

#undef BASETYPE_CASE_EXPR
#undef ARRAY1DTYPE_CASE_EXPR
#undef ARRAY2DTYPE_CASE_EXPR

  return os.str();
}

#undef CASE_EXPR_LIST


}  // namespace value
}  // namespace tinyusdz
