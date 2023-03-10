#include "c-tinyusd.h"

#include "tinyusdz.hh"

CTinyUSDFormat c_tinyusd_detect_format(const char *filename)
{
  if (tinyusdz::IsUSDA(filename)) {
    return C_TINYUSD_FORMAT_USDA;
  }
  // TODO: Implement
  return C_TINYUSD_FORMAT_UNKNOWN;
}
