// SPDX-License-Identifier: Apache-2.0
#include "lighting_ies.hh"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <limits>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace tusdview {
namespace {

constexpr size_t kMaxIesValues = 512u * 256u;
constexpr size_t kMaxIesCacheEntries = 32u;

struct CachedIesProfile {
  float maxCandela{0.0f};
  std::vector<float> verticalAngles;
  std::vector<float> horizontalAngles;
  std::vector<float> candela;
};

std::mutex& IesCacheMutex() {
  static std::mutex mutex;
  return mutex;
}

std::unordered_map<std::string, CachedIesProfile>& IesCache() {
  static std::unordered_map<std::string, CachedIesProfile> cache;
  return cache;
}

bool ParseFloatList(const std::string& text, std::vector<float>* out) {
  if (!out) return false;
  std::string normalized = text;
  std::replace(normalized.begin(), normalized.end(), ',', ' ');
  std::istringstream stream(normalized);
  float value = 0.0f;
  while (stream >> value) {
    if (!std::isfinite(value)) return false;
    out->push_back(value);
    if (out->size() > kMaxIesValues) return false;
  }
  return true;
}

bool IsStrictlyIncreasing(const std::vector<float>& values) {
  for (size_t i = 1; i < values.size(); ++i) {
    if (!(values[i] > values[i - 1])) return false;
  }
  return true;
}

template <typename SampleFn>
float InterpolateSamples(const std::vector<float>& x, float value,
                         SampleFn sample) {
  if (x.empty()) return 0.0f;
  if (x.size() == 1) return sample(0);
  if (value <= x.front()) return sample(0);
  if (value >= x.back()) return sample(x.size() - 1);
  const auto it = std::upper_bound(x.begin(), x.end(), value);
  const size_t hi = static_cast<size_t>(it - x.begin());
  const size_t lo = hi - 1;
  const float span = x[hi] - x[lo];
  const float t = span > 1.0e-8f ? (value - x[lo]) / span : 0.0f;
  const float loValue = sample(lo);
  return loValue + (sample(hi) - loValue) * t;
}

}  // namespace

bool LoadIesProfile(const std::string& path, DrawLightCPU* light,
                    std::string* err) {
  if (err) err->clear();
  if (!light) {
    if (err) *err = "empty IES profile path";
    return false;
  }

  // Loading is transactional from the caller's perspective. A light may be
  // reloaded after an asset edit; never leave a previous valid profile active
  // when the replacement is malformed or cannot be opened.
  light->iesValid = false;
  light->iesVerticalAngles.clear();
  light->iesHorizontalAngles.clear();
  light->iesCandela.clear();
  light->iesMaxCandela = 0.0f;

  if (path.empty()) {
    if (err) *err = "empty IES profile path";
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(IesCacheMutex());
    const auto it = IesCache().find(path);
    if (it != IesCache().end()) {
      light->iesMaxCandela = it->second.maxCandela;
      light->iesVerticalAngles = it->second.verticalAngles;
      light->iesHorizontalAngles = it->second.horizontalAngles;
      light->iesCandela = it->second.candela;
      light->iesValid = true;
      return true;
    }
  }

  std::ifstream file(path);
  if (!file) {
    if (err) *err = "cannot open IES profile: " + path;
    return false;
  }

  std::string line;
  bool sawTilt = false;
  bool includeTilt = false;
  bool tiltPayloadPending = false;
  std::string tiltIncludeFile;
  std::string numeric;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.size() >= 5 && line.compare(0, 5, "TILT=") == 0) {
      sawTilt = true;
      if (line == "TILT=INCLUDE") {
        includeTilt = true;
        tiltPayloadPending = true;
      } else if (line != "TILT=NONE") {
        if (err) *err = "IES TILT mode is unsupported (only NONE is accepted)";
        return false;
      }
      continue;
    }
    if (sawTilt) {
      if (tiltPayloadPending) {
        std::vector<float> probe;
        if (ParseFloatList(line, &probe) && probe.empty()) {
          size_t begin = 0;
          while (begin < line.size() &&
                 std::isspace(static_cast<unsigned char>(line[begin]))) ++begin;
          size_t end = line.size();
          while (end > begin &&
                 std::isspace(static_cast<unsigned char>(line[end - 1]))) --end;
          tiltIncludeFile = line.substr(begin, end - begin);
          if (tiltIncludeFile.size() >= 2 &&
              ((tiltIncludeFile.front() == '"' && tiltIncludeFile.back() == '"') ||
               (tiltIncludeFile.front() == '\'' && tiltIncludeFile.back() == '\''))) {
            tiltIncludeFile = tiltIncludeFile.substr(1, tiltIncludeFile.size() - 2);
          }
          tiltPayloadPending = false;
          continue;
        }
        tiltPayloadPending = false;
      }
      numeric.append(line);
      numeric.push_back(' ');
    }
  }
  if (!sawTilt) {
    if (err) *err = "IES profile has no TILT section";
    return false;
  }

  if (!tiltIncludeFile.empty()) {
    std::string::size_type slash = path.find_last_of("/\\");
    const std::string tiltPath =
        (slash == std::string::npos ? std::string() : path.substr(0, slash + 1)) +
        tiltIncludeFile;
    std::ifstream tiltFile(tiltPath);
    if (!tiltFile) {
      if (err) *err = "cannot open IES TILT include: " + tiltPath;
      return false;
    }
    std::string tiltNumeric;
    std::string tiltLine;
    while (std::getline(tiltFile, tiltLine)) {
      if (!tiltLine.empty() && tiltLine.back() == '\r') tiltLine.pop_back();
      tiltNumeric.append(tiltLine);
      tiltNumeric.push_back(' ');
    }
    numeric = tiltNumeric + numeric;
  }

  std::vector<float> values;
  if (!ParseFloatList(numeric, &values) || values.size() < 13) {
    if (err) *err = "IES profile has an incomplete numeric section";
    return false;
  }
  size_t photometricOffset = 0;
  std::vector<float> tiltAngles;
  std::vector<float> tiltMultipliers;
  if (includeTilt) {
    if (values.empty() || std::floor(values[0]) != values[0] ||
        values[0] < 1.0f || values[0] > 512.0f) {
      if (err) *err = "IES TILT section has an invalid angle count";
      return false;
    }
    const size_t tiltCount = static_cast<size_t>(values[0]);
    const size_t tiltValueCount = 1u + tiltCount * 2u;
    if (values.size() < tiltValueCount + 13u) {
      if (err) *err = "IES TILT section is incomplete";
      return false;
    }
    tiltAngles.assign(values.begin() + 1,
                      values.begin() + 1 + tiltCount);
    tiltMultipliers.assign(values.begin() + 1 + tiltCount,
                           values.begin() + tiltValueCount);
    if (!IsStrictlyIncreasing(tiltAngles)) {
      if (err) *err = "IES TILT angles must be strictly increasing";
      return false;
    }
    photometricOffset = tiltValueCount;
  }
  if (values.size() < photometricOffset + 13u) {
    if (err) *err = "IES profile has an incomplete photometric header";
    return false;
  }
  const float verticalCountValue = values[photometricOffset + 3u];
  const float horizontalCountValue = values[photometricOffset + 4u];
  if (verticalCountValue < 1.0f || verticalCountValue > 512.0f ||
      horizontalCountValue < 1.0f || horizontalCountValue > 256.0f ||
      std::floor(verticalCountValue) != verticalCountValue ||
      std::floor(horizontalCountValue) != horizontalCountValue) {
    if (err) *err = "IES profile angle dimensions are outside supported bounds";
    return false;
  }
  const int verticalCount = static_cast<int>(verticalCountValue);
  const int horizontalCount = static_cast<int>(horizontalCountValue);
  constexpr size_t kPhotometricHeaderValues = 13u;
  const size_t angleCount = static_cast<size_t>(verticalCount + horizontalCount);
  const size_t candelaCount = static_cast<size_t>(verticalCount) *
                              static_cast<size_t>(horizontalCount);
  if (values.size() < photometricOffset + kPhotometricHeaderValues +
                         angleCount + candelaCount) {
    if (err) *err = "IES profile has fewer candela values than declared";
    return false;
  }

  const size_t dataOffset = photometricOffset + kPhotometricHeaderValues;
  light->iesVerticalAngles.assign(values.begin() + dataOffset,
                                  values.begin() + dataOffset +
                                      verticalCount);
  light->iesHorizontalAngles.assign(
      values.begin() + dataOffset + verticalCount,
      values.begin() + dataOffset + verticalCount +
          horizontalCount);
  if (!IsStrictlyIncreasing(light->iesVerticalAngles) ||
      !IsStrictlyIncreasing(light->iesHorizontalAngles)) {
    if (err) *err = "IES angle tables must be strictly increasing";
    light->iesVerticalAngles.clear();
    light->iesHorizontalAngles.clear();
    return false;
  }
  light->iesCandela.assign(
      values.begin() + dataOffset + angleCount,
      values.begin() + dataOffset + angleCount + candelaCount);
  const float multiplier = values[photometricOffset + 2u];
  const int photometricType = static_cast<int>(values[photometricOffset + 5u]);
  const int unitsType = static_cast<int>(values[photometricOffset + 6u]);
  if (photometricType < 1 || photometricType > 3 ||
      unitsType < 1 || unitsType > 2) {
    if (err) *err = "IES photometric or units type is outside the LM-63 range";
    light->iesVerticalAngles.clear();
    light->iesHorizontalAngles.clear();
    light->iesCandela.clear();
    return false;
  }
  light->iesMaxCandela = 0.0f;
  for (size_t j = 0; j < static_cast<size_t>(horizontalCount); ++j) {
    float tilt = 1.0f;
    if (includeTilt) {
      tilt = InterpolateSamples(tiltAngles, light->iesVerticalAngles.front(),
                                [&](size_t i) { return tiltMultipliers[i]; });
    }
    for (size_t i = 0; i < static_cast<size_t>(verticalCount); ++i) {
      if (includeTilt) {
        tilt = InterpolateSamples(
            tiltAngles, light->iesVerticalAngles[i],
            [&](size_t k) { return tiltMultipliers[k]; });
      }
      float& value = light->iesCandela[j * static_cast<size_t>(verticalCount) + i];
      value = std::max(0.0f, value * multiplier * tilt);
    }
  }
  for (const float value : light->iesCandela) {
    light->iesMaxCandela = std::max(light->iesMaxCandela, value);
  }
  if (light->iesMaxCandela <= 0.0f) {
    if (err) *err = "IES profile contains no positive candela values";
    light->iesVerticalAngles.clear();
    light->iesHorizontalAngles.clear();
    light->iesCandela.clear();
    return false;
  }
  light->iesValid = true;
  {
    std::lock_guard<std::mutex> lock(IesCacheMutex());
    auto& cache = IesCache();
    if (cache.size() >= kMaxIesCacheEntries) cache.erase(cache.begin());
    cache[path] = CachedIesProfile{light->iesMaxCandela,
                                   light->iesVerticalAngles,
                                   light->iesHorizontalAngles,
                                   light->iesCandela};
  }
  return true;
}

float EvaluateIesProfile(const DrawLightCPU& light, float verticalDeg,
                         float horizontalDeg) {
  if (!light.iesValid || light.iesVerticalAngles.empty() ||
      light.iesHorizontalAngles.empty()) return 1.0f;
  const size_t nv = light.iesVerticalAngles.size();
  const size_t nh = light.iesHorizontalAngles.size();
  if (light.iesCandela.size() != nv * nh || light.iesMaxCandela <= 0.0f)
    return 1.0f;

  const float v = std::max(light.iesVerticalAngles.front(),
                           std::min(light.iesVerticalAngles.back(),
                                    std::fabs(verticalDeg)));
  float h = horizontalDeg;
  const float hMin = light.iesHorizontalAngles.front();
  const float hMax = light.iesHorizontalAngles.back();
  const float span = hMax - hMin;
  if (span > 1.0e-6f) {
    while (h < 0.0f) h += 360.0f;
    while (h >= 360.0f) h -= 360.0f;
    // LM-63 profiles commonly author one half (0..180) or one quadrant
    // (0..90) and rely on rotational symmetry for the remainder.
    if (hMax <= 180.0f && h > 180.0f) h = 360.0f - h;
    if (h > hMax) h = hMax;
  }
  h = std::max(hMin, std::min(hMax, h));

  const auto verticalSample = [&](size_t horizontalIndex) {
    return InterpolateSamples(
        light.iesVerticalAngles, v, [&](size_t verticalIndex) {
          return light.iesCandela[horizontalIndex * nv + verticalIndex];
        });
  };
  const float candela = InterpolateSamples(
      light.iesHorizontalAngles, h,
      [&](size_t horizontalIndex) { return verticalSample(horizontalIndex); });
  const float normalized = light.shapingIesNormalize
      ? candela / light.iesMaxCandela : candela;
  return std::max(0.0f, normalized *
      (light.shapingIesAngleScale > 0.0f ? light.shapingIesAngleScale : 1.0f));
}

}  // namespace tusdview
