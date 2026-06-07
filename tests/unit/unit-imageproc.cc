#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "imageproc/simd.hh"

#include <cmath>
#include <cstdint>
#include <vector>

using namespace tinyusdz;

namespace {

std::vector<float> MakeRGB(size_t n) {
  std::vector<float> v(n * 3);
  for (size_t i = 0; i < n * 3; ++i) {
    v[i] = float((i * 2654435761u) & 0xFFFF) / 65535.0f;  // deterministic [0,1]
  }
  return v;
}

}  // namespace

void imageproc_mat3_identity_test(void) {
  const float I[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  std::vector<float> in = MakeRGB(1000);
  std::vector<float> out(in.size(), -1.0f);
  imageproc::Mat3MulRGBf(in.data(), out.data(), 1000, I);
  bool ok = true;
  for (size_t i = 0; i < in.size(); ++i)
    if (out[i] != in[i]) { ok = false; break; }  // exact: multiply by 1/0 only
  TEST_CHECK(ok);
}

void imageproc_mat3_swap_test(void) {
  // Swap R<->B (and scale G by 2). All exactly representable -> bit-exact.
  const float M[9] = {0, 0, 1, 0, 2, 0, 1, 0, 0};
  const size_t n = 257;
  std::vector<float> in = MakeRGB(n);
  std::vector<float> out(in.size(), -1.0f);
  imageproc::Mat3MulRGBf(in.data(), out.data(), n, M);
  bool ok = true;
  for (size_t i = 0; i < n; ++i) {
    if (out[3 * i + 0] != in[3 * i + 2]) { ok = false; break; }
    if (out[3 * i + 1] != in[3 * i + 1] * 2.0f) { ok = false; break; }
    if (out[3 * i + 2] != in[3 * i + 0]) { ok = false; break; }
  }
  TEST_CHECK(ok);
}

void imageproc_mat3_parity_test(void) {
  // A representative linear DisplayP3->sRGB-ish matrix vs a scalar reference.
  const float M[9] = {1.2249f,  -0.2247f, 0.0000f, -0.0420f, 1.0419f,
                      0.0000f,  -0.0197f, -0.0786f, 1.0979f};
  const size_t n = 4096;
  std::vector<float> in = MakeRGB(n);
  std::vector<float> out(in.size());
  imageproc::Mat3MulRGBf(in.data(), out.data(), n, M);
  bool ok = true;
  for (size_t i = 0; i < n; ++i) {
    float r = in[3 * i + 0], g = in[3 * i + 1], b = in[3 * i + 2];
    float e0 = M[0] * r + M[1] * g + M[2] * b;
    float e1 = M[3] * r + M[4] * g + M[5] * b;
    float e2 = M[6] * r + M[7] * g + M[8] * b;
    if (std::fabs(out[3 * i + 0] - e0) > 1e-5f ||
        std::fabs(out[3 * i + 1] - e1) > 1e-5f ||
        std::fabs(out[3 * i + 2] - e2) > 1e-5f) {
      ok = false;
      break;
    }
  }
  TEST_CHECK(ok);
}

void imageproc_simd_level_test(void) {
  imageproc::SimdLevel lvl = imageproc::ActiveSimdLevel();
  const char *name = imageproc::ToString(lvl);
  TEST_CHECK(name != nullptr);
  // In-place aliasing must also work (in == out buffer).
  const float I[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
  std::vector<float> buf = MakeRGB(64);
  std::vector<float> ref = buf;
  imageproc::Mat3MulRGBf(buf.data(), buf.data(), 64, I);
  bool ok = true;
  for (size_t i = 0; i < buf.size(); ++i)
    if (buf[i] != ref[i]) { ok = false; break; }
  TEST_CHECK(ok);
}
