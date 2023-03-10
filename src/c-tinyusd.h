// SPDX-License-Identifier: Apache 2.0
// 
// C binding(C11) for TinyUSDZ 
//
#ifndef C_TINYUSD_H
#define C_TINYUSD_H

#ifdef __cplusplus
extern "C" {
#endif

enum CTinyUSDFormat {
  C_TINYUSD_FORMAT_UNKNOWN,
  C_TINYUSD_FORMAT_AUTO, // auto detect based on file extension.
  C_TINYUSD_FORMAT_USDA,
  C_TINYUSD_FORMAT_USDC,
  C_TINYUSD_FORMAT_USDZ
};

enum CTinyUSDAxis {
  C_TINYUSD_AXIS_UNKNOWN,
  C_TINYUSD_AXIS_X,
  C_TINYUSD_AXIS_Y,
  C_TINYUSD_AXIS_Z,
};

enum CTinyUSDValueType {
  C_TINYUSD_VALUE_FLOAT,
  C_TINYUSD_VALUE_FLOAT2,
  C_TINYUSD_VALUE_FLOAT3,
  C_TINYUSD_VALUE_FLOAT4,
};

struct CTinyUSDRelationship {
  char *targetPath;
};

CTinyUSDRelationship *ctinyusd_relationsip_new(const char *targetPath);
void ctinyusd_relationsip_delete(CTinyUSDRelationship *rel);

struct CTinyUSDAttributeValue {
  CTinyUSDValueType type;
  void *data; // opaque pointer.
};

void ctinyusd_attribute_value_new(CTinyUSDAttributeValue *val, const CTinyUSDValueType type, const void *data);


struct CStage {

};

///
/// Detect file format of input file.
///
///
CTinyUSDFormat c_tinyusd_detect_format(const char *filename);

int c_tinyusdz_is_usd(const char *filename);
int c_tinyusdz_is_usda(const char *filename);
int c_tinyusdz_is_usdc(const char *filename);
int c_tinyusdz_is_usdz(const char *filename);

int c_tinuyusdz_load_usd_from_file(const char *filename);
int c_tinuyusdz_load_usda_from_file(const char *filename);
int c_tinuyusdz_load_usdc_from_file(const char *filename);
int c_tinuyusdz_load_usdz_from_file(const char *filename);

#ifdef __cplusplus
}
#endif

#endif // C_TINYUSD_H
