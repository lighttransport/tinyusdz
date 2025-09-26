// SPDX-License-Identifier: Apache 2.0
// Copyright 2025 - Present, Light Transport Entertainment Inc.
//

#include "tydra/render-data-pprint.hh"
#include "tydra/render-data.hh"

namespace tinyusdz {

namespace tydra {

std::string to_string(const UVTexture::Channel channel) {
  if (channel == UVTexture::Channel::RGB) {
    return "rgb";
  } else if (channel == UVTexture::Channel::R) {
    return "r";
  } else if (channel == UVTexture::Channel::G) {
    return "g";
  } else if (channel == UVTexture::Channel::B) {
    return "b";
  } else if (channel == UVTexture::Channel::A) {
    return "a";
  }

  return "[[InternalError. Invalid UVTexture::Channel]]";
}

std::string to_string(ColorSpace cty) {
  std::string s;
  switch (cty) {
    case ColorSpace::sRGB: {
      s = "srgb";
      break;
    }
    case ColorSpace::Lin_sRGB: {
      s = "lin_srgb";
      break;
    }
    case ColorSpace::Raw: {
      s = "raw";
      break;
    }
    case ColorSpace::Rec709: {
      s = "rec709";
      break;
    }
    case ColorSpace::Lin_Rec709: {
      s = "lin_rec709";
      break;
    }
    case ColorSpace::g22_Rec709: {
      s = "g22_rec709";
      break;
    }
    case ColorSpace::g18_Rec709: {
      s = "g18_rec709";
      break;
    }
    case ColorSpace::sRGB_Texture: {
      s = "srgb_texture";
      break;
    }
    case ColorSpace::OCIO: {
      s = "ocio";
      break;
    }
    case ColorSpace::Lin_ACEScg: {
      s = "lin_acescg";
      break;
    }
    case ColorSpace::ACES2065_1: {
      s = "aces2065-1";
      break;
    }
    case ColorSpace::Lin_Rec2020: {
      s = "lin_rec2020";
      break;
    }
    case ColorSpace::Lin_DisplayP3: {
      s = "lin_displayp3";
      break;
    }
    case ColorSpace::sRGB_DisplayP3: {
      s = "srgb_displayp3";
      break;
    }
    case ColorSpace::Custom: {
      s = "custom";
      break;
    }
    case ColorSpace::Unknown: {
      s = "unknown";
      break;
    }
  }

  return s;
}

std::string to_string(NodeType ntype) {
  if (ntype == NodeType::Xform) {
    return "xform";
  } else if (ntype == NodeType::Mesh) {
    return "mesh";
  } else if (ntype == NodeType::Camera) {
    return "camera";
  } else if (ntype == NodeType::PointLight) {
    return "pointLight";
  } else if (ntype == NodeType::DirectionalLight) {
    return "directionalLight";
  } else if (ntype == NodeType::Skeleton) {
    return "skeleton";
  }
  return "???";
}

std::string to_string(ComponentType cty) {
  std::string s;
  switch (cty) {
    case ComponentType::UInt8: {
      s = "uint8";
      break;
    }
    case ComponentType::Int8: {
      s = "int8";
      break;
    }
    case ComponentType::UInt16: {
      s = "uint16";
      break;
    }
    case ComponentType::Int16: {
      s = "int16";
      break;
    }
    case ComponentType::UInt32: {
      s = "uint32";
      break;
    }
    case ComponentType::Int32: {
      s = "int32";
      break;
    }
    case ComponentType::Half: {
      s = "half";
      break;
    }
    case ComponentType::Float: {
      s = "float";
      break;
    }
    case ComponentType::Double: {
      s = "double";
      break;
    }
  }

  return s;
}

std::string to_string(UVTexture::WrapMode mode) {
  std::string s;
  switch (mode) {
    case UVTexture::WrapMode::REPEAT: {
      s = "repeat";
      break;
    }
    case UVTexture::WrapMode::CLAMP_TO_BORDER: {
      s = "clamp_to_border";
      break;
    }
    case UVTexture::WrapMode::CLAMP_TO_EDGE: {
      s = "clamp_to_edge";
      break;
    }
    case UVTexture::WrapMode::MIRROR: {
      s = "mirror";
      break;
    }
  }

  return s;
}

std::string to_string(VertexVariability v) {
  std::string s;

  switch (v) {
    case VertexVariability::Constant: {
      s = "constant";
      break;
    }
    case VertexVariability::Uniform: {
      s = "uniform";
      break;
    }
    case VertexVariability::Varying: {
      s = "varying";
      break;
    }
    case VertexVariability::Vertex: {
      s = "vertex";
      break;
    }
    case VertexVariability::FaceVarying: {
      s = "facevarying";
      break;
    }
    case VertexVariability::Indexed: {
      s = "indexed";
      break;
    }
  }

  return s;
}

std::string to_string(VertexAttributeFormat f) {
  std::string s;

  switch (f) {
    case VertexAttributeFormat::Bool: {
      s = "bool";
      break;
    }
    case VertexAttributeFormat::Char: {
      s = "int8";
      break;
    }
    case VertexAttributeFormat::Char2: {
      s = "int8x2";
      break;
    }
    case VertexAttributeFormat::Char3: {
      s = "int8x3";
      break;
    }
    case VertexAttributeFormat::Char4: {
      s = "int8x4";
      break;
    }
    case VertexAttributeFormat::Byte: {
      s = "uint8";
      break;
    }
    case VertexAttributeFormat::Byte2: {
      s = "uint8x2";
      break;
    }
    case VertexAttributeFormat::Byte3: {
      s = "uint8x3";
      break;
    }
    case VertexAttributeFormat::Byte4: {
      s = "uint8x4";
      break;
    }
    case VertexAttributeFormat::Short: {
      s = "int16";
      break;
    }
    case VertexAttributeFormat::Short2: {
      s = "int16x2";
      break;
    }
    case VertexAttributeFormat::Short3: {
      s = "int16x2";
      break;
    }
    case VertexAttributeFormat::Short4: {
      s = "int16x2";
      break;
    }
    case VertexAttributeFormat::Ushort: {
      s = "uint16";
      break;
    }
    case VertexAttributeFormat::Ushort2: {
      s = "uint16x2";
      break;
    }
    case VertexAttributeFormat::Ushort3: {
      s = "uint16x2";
      break;
    }
    case VertexAttributeFormat::Ushort4: {
      s = "uint16x2";
      break;
    }
    case VertexAttributeFormat::Half: {
      s = "half";
      break;
    }
    case VertexAttributeFormat::Half2: {
      s = "half2";
      break;
    }
    case VertexAttributeFormat::Half3: {
      s = "half3";
      break;
    }
    case VertexAttributeFormat::Half4: {
      s = "half4";
      break;
    }
    case VertexAttributeFormat::Float: {
      s = "float";
      break;
    }
    case VertexAttributeFormat::Vec2: {
      s = "float2";
      break;
    }
    case VertexAttributeFormat::Vec3: {
      s = "float3";
      break;
    }
    case VertexAttributeFormat::Vec4: {
      s = "float4";
      break;
    }
    case VertexAttributeFormat::Int: {
      s = "int";
      break;
    }
    case VertexAttributeFormat::Ivec2: {
      s = "int2";
      break;
    }
    case VertexAttributeFormat::Ivec3: {
      s = "int3";
      break;
    }
    case VertexAttributeFormat::Ivec4: {
      s = "int4";
      break;
    }
    case VertexAttributeFormat::Uint: {
      s = "uint";
      break;
    }
    case VertexAttributeFormat::Uvec2: {
      s = "uint2";
      break;
    }
    case VertexAttributeFormat::Uvec3: {
      s = "uint3";
      break;
    }
    case VertexAttributeFormat::Uvec4: {
      s = "uint4";
      break;
    }
    case VertexAttributeFormat::Double: {
      s = "double";
      break;
    }
    case VertexAttributeFormat::Dvec2: {
      s = "double2";
      break;
    }
    case VertexAttributeFormat::Dvec3: {
      s = "double3";
      break;
    }
    case VertexAttributeFormat::Dvec4: {
      s = "double4";
      break;
    }
    case VertexAttributeFormat::Mat2: {
      s = "mat2";
      break;
    }
    case VertexAttributeFormat::Mat3: {
      s = "mat3";
      break;
    }
    case VertexAttributeFormat::Mat4: {
      s = "mat4";
      break;
    }
    case VertexAttributeFormat::Dmat2: {
      s = "dmat2";
      break;
    }
    case VertexAttributeFormat::Dmat3: {
      s = "dmat3";
      break;
    }
    case VertexAttributeFormat::Dmat4: {
      s = "dmat4";
      break;
    }
  }

  return s;
}

std::string to_string(UVReaderFloatComponentType ty) {
  std::string s;
  switch (ty) {
    case UVReaderFloatComponentType::COMPONENT_FLOAT: {
      s = "float";
      break;
    }
    case UVReaderFloatComponentType::COMPONENT_FLOAT2: {
      s = "float2";
      break;
    }
    case UVReaderFloatComponentType::COMPONENT_FLOAT3: {
      s = "float3";
      break;
    }
    case UVReaderFloatComponentType::COMPONENT_FLOAT4: {
      s = "float4";
      break;
    }
  }
  return s;
}

}  // namespace tydra
}  // namespace tinyusdz
