// SPDX-License-Identifier: MIT
// C binding for TinyUSD(Z) 
#ifndef C_TINYUSD_H
#define C_TINYUSD_H

#ifdef __cplusplus
extern "C" {
#endif

enum CTinyUSDFormat {
  C_TINYUSD_FORMAT_UNKNOWN,
  C_TINYUSD_FORMAT_USDA,
  C_TINYUSD_FORMAT_USDC,
  C_TINYUSD_FORMAT_USDZ
};

///
/// Detect file format of input file.
///
///
CTinyUSDFormat c_tinyusd_detect_format(const char *filename);

#ifdef __cplusplus
}
#endif

#endif // C_TINYUSD_H
