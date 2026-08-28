// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace vchar {

// Extension boundary for external facial solvers. Implementations author an
// overlay or produce named control values; they never mutate source geometry.
class IDeformerAdapter {
 public:
  virtual ~IDeformerAdapter() = default;
  virtual const char* name() const = 0;
  virtual bool loadOverlay(const std::string& path, std::string* error) = 0;
  virtual bool evaluate(double timeCode,
                        const std::unordered_map<std::string, float>& controls,
                        std::unordered_map<std::string, float>* blendWeights,
                        std::string* error) = 0;
};

struct PhysicsDebugRecord {
  std::string primPath;
  std::string schema;
  std::unordered_map<std::string, std::string> properties;
};

// V1 adapters inspect and visualize authored metadata. `step` is reserved for
// a future simulation adapter and must report unsupported until then.
class IPhysicsAdapter {
 public:
  virtual ~IPhysicsAdapter() = default;
  virtual const char* name() const = 0;
  virtual std::vector<PhysicsDebugRecord> inspect() const = 0;
  virtual bool step(double seconds, std::string* error) = 0;
};

}  // namespace vchar
