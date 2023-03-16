#include "c-tinyusd.h"

#include "tinyusdz.hh"
#include "tydra/scene-access.hh"

const char *c_tinyusd_value_type_name(CTinyUSDValueType value_type)
{
  // 32 should be enough length to support all C_TINYUSD_VALUE_* type name + '[]'
  static thread_local char buf[32];

  bool is_array = value_type & C_TINYUSD_VALUE_1D_BIT;

  // drop array bit.
  uint32_t basety = value_type & (~C_TINYUSD_VALUE_1D_BIT);

  const char *tyname = "[invalid]";

  switch (static_cast<CTinyUSDValueType>(basety)) {
    case C_TINYUSD_VALUE_BOOL: { tyname = "bool"; break; }
    case C_TINYUSD_VALUE_TOKEN: { tyname = "token";break; }
    case C_TINYUSD_VALUE_STRING: { tyname = "string";break; }
    case C_TINYUSD_VALUE_HALF: {  tyname = "half";break; }
    case C_TINYUSD_VALUE_HALF2: { tyname = "half2"; break;}
    case C_TINYUSD_VALUE_HALF3: { tyname = "half3"; break;}
    case C_TINYUSD_VALUE_HALF4: { tyname = "half4"; break;}
    case C_TINYUSD_VALUE_INT: {   tyname = "int"; break;}
    case C_TINYUSD_VALUE_INT2: {  tyname = "int2"; break;}
    case C_TINYUSD_VALUE_INT3: {  tyname = "int3"; break;}
    case C_TINYUSD_VALUE_INT4: {  tyname = "int4"; break;}
    case C_TINYUSD_VALUE_UINT: {  tyname = "uint"; break;}
    case C_TINYUSD_VALUE_UINT2: { tyname = "uint2"; break;}
    case C_TINYUSD_VALUE_UINT3: { tyname = "uint3"; break;}
    case C_TINYUSD_VALUE_UINT4: { tyname = "uint4"; break;}
    case C_TINYUSD_VALUE_INT64: { tyname = "int64"; break;}
    case C_TINYUSD_VALUE_UINT64: {tyname = "uint64"; break;}
    case C_TINYUSD_VALUE_FLOAT: { tyname = "float"; break;}
    case C_TINYUSD_VALUE_FLOAT2: { tyname = "float2"; break;}
    case C_TINYUSD_VALUE_FLOAT3: { tyname = "float3"; break;}
    case C_TINYUSD_VALUE_FLOAT4: { tyname = "float4"; break;}
    case C_TINYUSD_VALUE_DOUBLE: { tyname = "double"; break;}
    case C_TINYUSD_VALUE_DOUBLE2: {  tyname = "double2"; break;}
    case C_TINYUSD_VALUE_DOUBLE3: {  tyname = "double3"; break;}
    case C_TINYUSD_VALUE_DOUBLE4: {  tyname = "double4"; break;}
    case C_TINYUSD_VALUE_QUATH: {    tyname = "quath"; break;}
    case C_TINYUSD_VALUE_QUATF: {    tyname = "quatf"; break;}
    case C_TINYUSD_VALUE_QUATD: {    tyname = "quatd"; break;}
    case C_TINYUSD_VALUE_NORMAL3H: { tyname = "normal3h"; break;}
    case C_TINYUSD_VALUE_NORMAL3F: { tyname = "normal3f"; break;}
    case C_TINYUSD_VALUE_NORMAL3D: { tyname = "normal3d"; break;}
    case C_TINYUSD_VALUE_VECTOR3H: { tyname = "vector3h"; break;}
    case C_TINYUSD_VALUE_VECTOR3F: { tyname = "vector3f"; break;}
    case C_TINYUSD_VALUE_VECTOR3D: { tyname = "vector3d"; break;}
    case C_TINYUSD_VALUE_POINT3H: {  tyname = "point3h"; break;}
    case C_TINYUSD_VALUE_POINT3F: {  tyname = "point3f"; break;}
    case C_TINYUSD_VALUE_POINT3D: {  tyname = "point3d"; break;}
    case C_TINYUSD_VALUE_TEXCOORD2H: { tyname = "texCoord2h"; break;}
    case C_TINYUSD_VALUE_TEXCOORD2F: { tyname = "texCoord2f"; break;}
    case C_TINYUSD_VALUE_TEXCOORD2D: { tyname = "texCoord2d"; break;}
    case C_TINYUSD_VALUE_TEXCOORD3H: { tyname = "texCoord3h"; break;}
    case C_TINYUSD_VALUE_TEXCOORD3F: { tyname = "texCoord3f"; break;}
    case C_TINYUSD_VALUE_TEXCOORD3D: { tyname = "texCoord3d"; break;}
    case C_TINYUSD_VALUE_COLOR3H: { tyname = "color3h"; break;}
    case C_TINYUSD_VALUE_COLOR3F: { tyname = "color3f"; break;}
    case C_TINYUSD_VALUE_COLOR3D: { tyname = "color3d"; break;}
    case C_TINYUSD_VALUE_COLOR4H: { tyname = "color4h"; break;}
    case C_TINYUSD_VALUE_COLOR4F: { tyname = "color4f"; break;}
    case C_TINYUSD_VALUE_COLOR4D: { tyname = "color4d"; break;}
    case C_TINYUSD_VALUE_MATRIX2D: { tyname = "matrix2d"; break;}
    case C_TINYUSD_VALUE_MATRIX3D: { tyname = "matrix2d"; break;}
    case C_TINYUSD_VALUE_MATRIX4D: { tyname = "matrix2d"; break;}
    case C_TINYUSD_VALUE_FRAME4D: { tyname = "frame4d"; break;}
    case C_TINYUSD_VALUE_END: { tyname = "[invalid]"; break;} // invalid
    //default: { return 0; }
  }

  uint32_t sz = static_cast<uint32_t>(strlen(tyname));

  if (sz > 31) {
    // Just in case: this should not happen though.
    sz = 31;
  }

  strncpy(buf, tyname, sz);

  if (is_array) {
    if (sz > 29) {
      // Just in case: this should not happen though.
      sz = 29;
    }

    buf[sz] = '[';
    buf[sz+1] = ']';
    buf[sz+2] = '\0';
  } else {
    buf[sz] = '\0';
  }

  return buf;
}

uint32_t c_tinyusd_value_type_components(CTinyUSDValueType value_type)
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
    case C_TINYUSD_VALUE_END: { return 0; } // invalid
    //default: { return 0; }
  }

  return 0;
}

uint32_t c_tinyusd_value_type_sizeof(CTinyUSDValueType value_type)
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
    case C_TINYUSD_VALUE_END: { return 0; } // invalid
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

int c_tinyusd_token_new(c_tinyusd_token *tok, const char *str) {
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

size_t c_tinyusd_token_size(c_tinyusd_token *tok) {

  if (!tok) {
    return 0;
  }

  if (!tok->data) {
    return 0;
  }

  auto *p = reinterpret_cast<tinyusdz::value::token *>(tok->data);

  return p->str().size();
}

int c_tinyusd_string_new_empty(c_tinyusd_string *s) {
  if (!s) {
    return 0;
  }

  auto *value = new std::string();
  s->data = reinterpret_cast<void *>(value);

  return 1; // ok
}

int c_tinyusd_string_new(c_tinyusd_string *s, const char *str) {
  if (!s) {
    return 0;
  }

  if (str) {
    auto *value = new std::string(str);
    s->data = reinterpret_cast<void *>(value);
  } else {
    auto *value = new std::string();
    s->data = reinterpret_cast<void *>(value);
  }

  return 1; // ok
}

size_t c_tinyusd_string_size(c_tinyusd_string *s) {

  if (!s) {
    return 0;
  }

  if (!s->data) {
    return 0;
  }

  auto *p = reinterpret_cast<std::string *>(s->data);

  return p->size();
}

int c_tinyusd_string_replace(c_tinyusd_string *s, const char *str) {
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

int c_tinyusd_buffer_new(CTinyUSDBuffer *buf, CTinyUSDValueType value_type,
                         int ndim, uint64_t shape[C_TINYUSD_MAX_DIM]) {

  if (!buf) {
    return 0;
  }

  uint32_t sz = c_tinyusd_value_type_sizeof(value_type);
  if (sz == 0) {
    return 0;
  }

  if (ndim >= C_TINYUSD_MAX_DIM) {
    return 0;
  }

  uint64_t n = 1;
  for (int i = 0; i < ndim; i++) {
    n *= shape[i];
  }

  if (n == 0) {
    return 0;
  }

  buf->value_type = value_type;
  buf->ndim = ndim;
  for (int i = 0; i < ndim; i++) {
    buf->shape[i] = shape[i];
  }

  uint8_t *m = new uint8_t[n];
  buf->data = reinterpret_cast<void *>(m);

  return 1; // ok
}

int c_tinyusd_buffer_free(CTinyUSDBuffer *buf) {
  if (!buf) {
    return 0;
  }

  if (!buf->data) {
    return 0;
  }

  uint8_t *p = reinterpret_cast<uint8_t*>(buf->data);
  delete [] p;

  buf->data = nullptr;

  return 1;
}

int c_tinyusd_is_usda_file(const char *filename) {
  if (tinyusdz::IsUSDA(filename)) {
    return 1;
  }
  return 0;
}

int c_tinyusd_is_usdc_file(const char *filename) {
  if (tinyusdz::IsUSDC(filename)) {
    return 1;
  }
  return 0;
}

int c_tinyusd_is_usdz_file(const char *filename) {
  if (tinyusdz::IsUSDZ(filename)) {
    return 1;
  }
  return 0;
}

int c_tinyusd_is_usd_file(const char *filename) {
  if (tinyusdz::IsUSD(filename)) {
    return 1;
  }
  return 0;
}

int c_tinyusd_load_usd_from_file(const char *filename, CTinyUSDStage *stage, c_tinyusd_string *warn, c_tinyusd_string *err) {

  //tinyusdz::Stage *p = new tinyusdz::Stage();

  if (!stage) {
    if (err) {
      c_tinyusd_string_replace(err, "`stage` argument is null.\n");
    }
    return 0;
  }

  if (!stage->data) {
    if (err) {
      c_tinyusd_string_replace(err, "`stage` object is not initialized or new'ed.\n");
    }
    return 0;
  }

  std::string _warn;
  std::string _err;

  bool ret = tinyusdz::LoadUSDFromFile(filename, reinterpret_cast<tinyusdz::Stage *>(stage->data), &_warn, &_err);

  if (_warn.size() && warn) {
    c_tinyusd_string_replace(warn, _warn.c_str());
  }

  if (!ret) {

    if (err) {
      c_tinyusd_string_replace(err, _err.c_str());
    }

    return 0;
  }

  return 1;
}

namespace {

using namespace tinyusdz;

bool CVisitPrimFunction(const Path &abs_path, const Prim &prim,
                        const int32_t tree_depth, void *userdata,
                        std::string *err) {

  (void)tree_depth;

  if (!userdata) {
    if (err) {
      (*err) += "`userdata` is nullptr.\n";
    }
    return false;
  }

  CTinyUSDPrim cprim;
  cprim.data = reinterpret_cast<void *>(const_cast<Prim *>(&prim));

  CTinyUSDPath cpath;
  cpath.data = reinterpret_cast<void *>(const_cast<Path *>(&abs_path));

  CTinyUSDTraversalFunction callback_fun = reinterpret_cast<CTinyUSDTraversalFunction>(userdata);

  int ret = callback_fun(&cprim, &cpath);

  if (ret) {
    return true;
  }

  return false;
}

} // namespace local

int c_tinyusd_stage_new(CTinyUSDStage *stage) {
  if (!stage) {
    return 0;
  }

  auto *buf = new tinyusdz::Stage();
  stage->data = reinterpret_cast<void *>(buf);

  return 1;
}

int c_tinyusd_stage_free(CTinyUSDStage *stage) {
  if (!stage) {
    return 0;
  }

  tinyusdz::Stage *ptr = reinterpret_cast<tinyusdz::Stage *>(stage->data);
  delete ptr;

  return 1;
}

int c_tinyusd_stage_traverse(const CTinyUSDStage *_stage, CTinyUSDTraversalFunction callback_fun, c_tinyusd_string *_err) {
  if (!_stage) {
    if (_err) {
      c_tinyusd_string_replace(_err, "`stage` argument is null.\n");
    }
    return 0;
  }

  if (!_stage->data) {
    if (_err) {
      c_tinyusd_string_replace(_err, "`stage.data` is null.\n");
    }
    return 0;
  }

  const tinyusdz::Stage *pstage = reinterpret_cast<const tinyusdz::Stage *>(_stage);

  std::string err;
  if (!tinyusdz::tydra::VisitPrims(*pstage, CVisitPrimFunction, reinterpret_cast<void *>(callback_fun), &err)) {
    if (_err) {
      c_tinyusd_string_replace(_err, err.c_str());
    }
  }

  return 1;
}
