#include "c-tinyusd.h"

#include "tinyusdz.hh"

uint32_t c_tinyusd_type_components(CTinyUSDValueType value_type)
{
  // drop array bit.
  uint32_t basety = value_type & (~C_TINYUSD_VALUE_1D_BIT);

  switch (static_cast<CTinyUSDValueType>(basety)) {
    case C_TINYUSD_VALUE_BOOL: { return 1; }
    case C_TINYUSD_VALUE_TOKEN: { return 0; } // invalid
    case C_TINYUSD_VALUE_STRING: { return 0; } // invalid
    case C_TINYUSD_VALUE_HALF: { return 1; }
    case C_TINYUSD_VALUE_HALF2: { return 2; }
    case C_TINYUSD_VALUE_HALF3: { return 3; }
    case C_TINYUSD_VALUE_HALF4: { return 4; }
    case C_TINYUSD_VALUE_INT: { return 1; }
    case C_TINYUSD_VALUE_INT2: { return 2; }
    case C_TINYUSD_VALUE_INT3: { return 3; }
    case C_TINYUSD_VALUE_INT4: { return 4; }
    case C_TINYUSD_VALUE_UINT: { return  1; }
    case C_TINYUSD_VALUE_UINT2: { return 2; }
    case C_TINYUSD_VALUE_UINT3: { return 3; }
    case C_TINYUSD_VALUE_UINT4: { return 4; }
    case C_TINYUSD_VALUE_INT64: { return 1; }
    case C_TINYUSD_VALUE_UINT64: { return 1; }
    case C_TINYUSD_VALUE_FLOAT: { return 1; }
    case C_TINYUSD_VALUE_FLOAT2: { return 2; }
    case C_TINYUSD_VALUE_FLOAT3: { return 3; }
    case C_TINYUSD_VALUE_FLOAT4: { return 4; }
    case C_TINYUSD_VALUE_DOUBLE: { return 1; }
    case C_TINYUSD_VALUE_DOUBLE2: { return 2; }
    case C_TINYUSD_VALUE_DOUBLE3: { return 3; }
    case C_TINYUSD_VALUE_DOUBLE4: { return 4; }
    case C_TINYUSD_VALUE_QUATH: { return 4; }
    case C_TINYUSD_VALUE_QUATF: { return 4; }
    case C_TINYUSD_VALUE_QUATD: { return 4; }
    case C_TINYUSD_VALUE_NORMAL3H: { return 3; }
    case C_TINYUSD_VALUE_NORMAL3F: { return 3; }
    case C_TINYUSD_VALUE_NORMAL3D: { return 3; }
    case C_TINYUSD_VALUE_VECTOR3H: { return 3; }
    case C_TINYUSD_VALUE_VECTOR3F: { return 3; }
    case C_TINYUSD_VALUE_VECTOR3D: { return 3; }
    case C_TINYUSD_VALUE_POINT3H: { return  3; }
    case C_TINYUSD_VALUE_POINT3F: { return  3; }
    case C_TINYUSD_VALUE_POINT3D: { return  3; }
    case C_TINYUSD_VALUE_TEXCOORD2H: { return 2; }
    case C_TINYUSD_VALUE_TEXCOORD2F: { return 2; }
    case C_TINYUSD_VALUE_TEXCOORD2D: { return 2; }
    case C_TINYUSD_VALUE_TEXCOORD3H: { return 3; }
    case C_TINYUSD_VALUE_TEXCOORD3F: { return 3; }
    case C_TINYUSD_VALUE_TEXCOORD3D: { return 3; }
    case C_TINYUSD_VALUE_COLOR3H: { return 3; }
    case C_TINYUSD_VALUE_COLOR3F: { return 3; }
    case C_TINYUSD_VALUE_COLOR3D: { return 3; }
    case C_TINYUSD_VALUE_COLOR4H: { return 4; }
    case C_TINYUSD_VALUE_COLOR4F: { return 4; }
    case C_TINYUSD_VALUE_COLOR4D: { return 4; }
    case C_TINYUSD_VALUE_MATRIX2D: { return 2*2; }
    case C_TINYUSD_VALUE_MATRIX3D: { return 3*3; }
    case C_TINYUSD_VALUE_MATRIX4D: { return 4*4; }
    case C_TINYUSD_VALUE_FRAME4D: { return 4*4; }
    case C_TINYUSD_VALUE_1D_BIT: { return 0; } // invalid
    //default: { return 0; }
  }

  return 0;
}

uint32_t c_tinyusd_type_sizeof(CTinyUSDValueType value_type)
{
  // drop array bit.
  uint32_t basety = value_type & (~C_TINYUSD_VALUE_1D_BIT);

  switch (static_cast<CTinyUSDValueType>(basety)) {
    case C_TINYUSD_VALUE_BOOL: { return 1; }
    case C_TINYUSD_VALUE_TOKEN: { return 0; } // invalid
    case C_TINYUSD_VALUE_STRING: { return 0; } // invalid
    case C_TINYUSD_VALUE_HALF: { return sizeof(uint16_t); }
    case C_TINYUSD_VALUE_HALF2: { return sizeof(uint16_t)*2; }
    case C_TINYUSD_VALUE_HALF3: { return sizeof(uint16_t)*3; }
    case C_TINYUSD_VALUE_HALF4: { return sizeof(uint16_t)*4; }
    case C_TINYUSD_VALUE_INT: { return sizeof(int); }
    case C_TINYUSD_VALUE_INT2: { return sizeof(int)*2; }
    case C_TINYUSD_VALUE_INT3: { return sizeof(int)*3; }
    case C_TINYUSD_VALUE_INT4: { return sizeof(int)*4; }
    case C_TINYUSD_VALUE_UINT: { return sizeof(uint32_t); }
    case C_TINYUSD_VALUE_UINT2: { return sizeof(uint32_t)*2; }
    case C_TINYUSD_VALUE_UINT3: { return sizeof(uint32_t)*3; }
    case C_TINYUSD_VALUE_UINT4: { return sizeof(uint32_t)*4; }
    case C_TINYUSD_VALUE_INT64: { return sizeof(int64_t); }
    case C_TINYUSD_VALUE_UINT64: { return sizeof(uint64_t); }
    case C_TINYUSD_VALUE_FLOAT: { return sizeof(float); }
    case C_TINYUSD_VALUE_FLOAT2: { return sizeof(float)*2; }
    case C_TINYUSD_VALUE_FLOAT3: { return sizeof(float)*3; }
    case C_TINYUSD_VALUE_FLOAT4: { return sizeof(float)*4; }
    case C_TINYUSD_VALUE_DOUBLE: { return sizeof(double); }
    case C_TINYUSD_VALUE_DOUBLE2: { return sizeof(double)*2; }
    case C_TINYUSD_VALUE_DOUBLE3: { return sizeof(double)*3; }
    case C_TINYUSD_VALUE_DOUBLE4: { return sizeof(double)*4; }
    case C_TINYUSD_VALUE_QUATH: { return sizeof(uint16_t)*4; }
    case C_TINYUSD_VALUE_QUATF: { return sizeof(float)*4; }
    case C_TINYUSD_VALUE_QUATD: { return sizeof(double)*4; }
    case C_TINYUSD_VALUE_NORMAL3H: { return sizeof(uint16_t)*3; }
    case C_TINYUSD_VALUE_NORMAL3F: { return sizeof(float)*3; }
    case C_TINYUSD_VALUE_NORMAL3D: { return sizeof(double)*3; }
    case C_TINYUSD_VALUE_VECTOR3H: { return sizeof(uint16_t)*3; }
    case C_TINYUSD_VALUE_VECTOR3F: { return sizeof(float)*3; }
    case C_TINYUSD_VALUE_VECTOR3D: { return sizeof(double)*3; }
    case C_TINYUSD_VALUE_POINT3H: { return sizeof(uint16_t)*3; }
    case C_TINYUSD_VALUE_POINT3F: { return sizeof(float)*3; }
    case C_TINYUSD_VALUE_POINT3D: { return sizeof(double)*3; }
    case C_TINYUSD_VALUE_TEXCOORD2H: { return sizeof(uint16_t)*2; }
    case C_TINYUSD_VALUE_TEXCOORD2F: { return sizeof(float)*2; }
    case C_TINYUSD_VALUE_TEXCOORD2D: { return sizeof(double)*2; }
    case C_TINYUSD_VALUE_TEXCOORD3H: { return sizeof(uint16_t)*3; }
    case C_TINYUSD_VALUE_TEXCOORD3F: { return sizeof(float)*3; }
    case C_TINYUSD_VALUE_TEXCOORD3D: { return sizeof(double)*3; }
    case C_TINYUSD_VALUE_COLOR3H: { return sizeof(uint16_t)*3; }
    case C_TINYUSD_VALUE_COLOR3F: { return sizeof(float)*3; }
    case C_TINYUSD_VALUE_COLOR3D: { return sizeof(double)*3; }
    case C_TINYUSD_VALUE_COLOR4H: { return sizeof(uint16_t)*4; }
    case C_TINYUSD_VALUE_COLOR4F: { return sizeof(float)*4; }
    case C_TINYUSD_VALUE_COLOR4D: { return sizeof(double)*4; }
    case C_TINYUSD_VALUE_MATRIX2D: { return sizeof(double)*2*2; }
    case C_TINYUSD_VALUE_MATRIX3D: { return sizeof(double)*3*3; }
    case C_TINYUSD_VALUE_MATRIX4D: { return sizeof(double)*4*4; }
    case C_TINYUSD_VALUE_FRAME4D: { return sizeof(double)*4*4; }
    case C_TINYUSD_VALUE_1D_BIT: { return 0; } // invalid
    //default: { return 0; }
  }

  return 0;
}

CTinyUSDFormat c_tinyusd_detect_format(const char *filename)
{
  if (tinyusdz::IsUSDA(filename)) {
    return C_TINYUSD_FORMAT_USDA;
  }

  if (tinyusdz::IsUSDC(filename)) {
    return C_TINYUSD_FORMAT_USDC;
  }

  if (tinyusdz::IsUSDZ(filename)) {
    return C_TINYUSD_FORMAT_USDZ;
  }

  return C_TINYUSD_FORMAT_UNKNOWN;
}

int c_tinyusd_token_init(c_tinyusd_token *tok, const char *str) {
  if (!tok) {
    return 0;
  }

  auto *value = new tinyusdz::value::token(str);

  tok->data = reinterpret_cast<void *>(value);

  return 1; // ok
}

int c_tinyusd_token_free(c_tinyusd_token *tok) {
  if (!tok) {
    return 0;
  }

  if (tok->data) {
    auto *p = reinterpret_cast<tinyusdz::value::token *>(tok->data);
    delete p;
    tok->data = nullptr;
  }

  return 1; // ok
}

const char *c_tinyusd_token_str(c_tinyusd_token *tok) {
  if (!tok) {
    return nullptr;
  }

  if (tok->data) {
    auto *p = reinterpret_cast<tinyusdz::value::token *>(tok->data);
    return p->str().c_str();
  }

  return nullptr;
}

int c_tinyusd_string_init(c_tinyusd_string *s) {
  if (!s) {
    return 0;
  }

  auto *value = new std::string();

  s->data = reinterpret_cast<void *>(value);

  return 1; // ok
}

int c_tinyusd_string_set(c_tinyusd_string *s, const char *str) {
  if (!s) {
    return 0;
  }

  if (!str) {
    return 0;
  }

  if (!s->data) {
    return 0;
  }

  std::string *p = reinterpret_cast<std::string *>(s->data);
  (*p) = std::string(str);

  return 1; // ok
}


int c_tinyusd_string_free(c_tinyusd_string *s) {
  if (!s) {
    return 0;
  }

  if (s->data) {
    auto *p = reinterpret_cast<std::string *>(s->data);
    delete p;
    s->data = nullptr;
  }

  return 1; // ok
}

const char *c_tinyusd_string_str(c_tinyusd_string *s) {
  if (!s) {
    return nullptr;
  }

  if (s->data) {
    auto *p = reinterpret_cast<std::string *>(s->data);
    return p->c_str();
  }

  return nullptr;
}
