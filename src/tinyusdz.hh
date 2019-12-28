#ifndef TINYUSDZ_HH_
#define TINYUSDZ_HH_

#include <string>
#include <vector>
#include <array>

namespace tinyusdz {

// TODO(syoyo): Support Big Endian
// https://gist.github.com/rygorous/2156668
union FP32 {
  unsigned int u;
  float f;
  struct {
#if 1
    unsigned int Mantissa : 23;
    unsigned int Exponent : 8;
    unsigned int Sign : 1;
#else
    unsigned int Sign : 1;
    unsigned int Exponent : 8;
    unsigned int Mantissa : 23;
#endif
  } s;
};

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wpadded"
#endif

union float16 {
  unsigned short u;
  struct {
#if 1
    unsigned int Mantissa : 10;
    unsigned int Exponent : 5;
    unsigned int Sign : 1;
#else
    unsigned int Sign : 1;
    unsigned int Exponent : 5;
    unsigned int Mantissa : 10;
#endif
  } s;
};

float half_to_float(float16 h);
float16 float_to_half_full(float f);


template<typename T, size_t N>
struct Matrix
{
  T m[N][N];
  constexpr static uint32_t n = N;
};

using Matrix2f = Matrix<float, 2>;
using Matrix2d = Matrix<double, 2>;
using Matrix3f = Matrix<float, 3>;
using Matrix3d = Matrix<double, 3>;
using Matrix4f = Matrix<float, 4>;
using Matrix4d = Matrix<double, 4>;

using Vec4i = std::array<int32_t, 4>;
using Vec3i = std::array<int32_t, 3>;
using Vec2i = std::array<int32_t, 2>;

using Vec4h = std::array<float16, 4>;
using Vec3h = std::array<float16, 3>;
using Vec2h = std::array<float16, 2>;

using Vec4f = std::array<float, 4>;
using Vec3f = std::array<float, 3>;
using Vec2f = std::array<float, 2>;

using Vec4d = std::array<double, 4>;
using Vec3d = std::array<double, 3>;
using Vec2d = std::array<double, 2>;

using Quath = std::array<float16, 4>;
using Quatf = std::array<float, 4>;
using Quatd = std::array<double, 4>;
using Quaternion = std::array<double, 4>; // Storage layout is same with Quadd, so we can delete this

// TODO(syoyo): Range, Interval, Rect2i, Frustum, MultiInterval

/*
#define VT_GFRANGE_VALUE_TYPES                 \
((      GfRange3f,           Range3f        )) \
((      GfRange3d,           Range3d        )) \
((      GfRange2f,           Range2f        )) \
((      GfRange2d,           Range2d        )) \
((      GfRange1f,           Range1f        )) \
((      GfRange1d,           Range1d        ))

#define VT_RANGE_VALUE_TYPES                   \
    VT_GFRANGE_VALUE_TYPES                     \
((      GfInterval,          Interval       )) \
((      GfRect2i,            Rect2i         ))

#define VT_STRING_VALUE_TYPES            \
((      std::string,           String )) \
((      TfToken,               Token  ))

#define VT_QUATERNION_VALUE_TYPES           \
((      GfQuath,             Quath ))       \
((      GfQuatf,             Quatf ))       \
((      GfQuatd,             Quatd ))       \
((      GfQuaternion,        Quaternion ))

#define VT_NONARRAY_VALUE_TYPES                 \
((      GfFrustum,           Frustum))          \
((      GfMultiInterval,     MultiInterval))

*/

struct USDZLoadOptions
{


};

struct USDCLoadOptions
{


};

struct USDCWriteOptions
{


};

///
/// Represent value.
/// multi-dimensional type is not supported(e.g. float[][])
///
class Value {
 public:
  std::vector<uint8_t> data; // value as opaque binary data.
  bool is_array{false};
  uint64_t array_length{0};
  std::string type;
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

///
/// Write scene as USDC to a file.
///
/// @param[in] filename USDC filename
/// @param[out] err Error message(filled when the function returns false)
/// @param[in] options Write options(optional)
///
/// @return true upon success
///
bool WriteAsUSDCToFile(const std::string &filename, std::string *err, const USDCWriteOptions &options = USDCWriteOptions());


} // namespace tinyusdz

#endif // TINYUSDZ_HH_
