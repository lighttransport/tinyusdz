#ifndef TINYUSDZ_HH_
#define TINYUSDZ_HH_

#include <string>

namespace tinyusdz {

struct USDZLoadOptions
{


};

struct USDCLoadOptions
{


};

///
/// Load USDZ file from a file.
///
/// @param[in] filename USDZ filename
/// @param[out] err Error message(filled when the function returns false)
/// @param[in] options Load options(optional)
///
/// @return true upon success
///
bool LoadUSDZFromFile(const std::string &filename, std::string *err, const USDZLoadOptions &options = USDZLoadOptions());

///
/// Load USDC(binary) file from a file.
///
/// @param[in] filename USDC filename
/// @param[out] err Error message(filled when the function returns false)
/// @param[in] options Load options(optional)
///
/// @return true upon success
///
bool LoadUSDCFromFile(const std::string &filename, std::string *err, const USDCLoadOptions &options = USDCLoadOptions());

} // namespace tinyusdz

#endif // TINYUSDZ_HH_
