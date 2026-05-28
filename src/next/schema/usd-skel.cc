// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - UsdSkel Schema Implementation

#include "usd-skel.hh"
#include <cstring>
#include <algorithm>

namespace tinyusdz {
namespace next {

// ============================================================
// Prim type checking
// ============================================================

bool IsSkelRoot(const UsdPrim& prim) {
  return prim.IsValid() && prim.GetTypeName() == "SkelRoot";
}

bool IsSkeleton(const UsdPrim& prim) {
  return prim.IsValid() && prim.GetTypeName() == "Skeleton";
}

bool IsSkelAnimation(const UsdPrim& prim) {
  return prim.IsValid() && prim.GetTypeName() == "SkelAnimation";
}

bool IsBlendShape(const UsdPrim& prim) {
  return prim.IsValid() && prim.GetTypeName() == "BlendShape";
}

// ============================================================
// Skeleton data
// ============================================================

bool GetSkeletonData(const Stage& stage, const UsdPrim& prim,
                     SkeletonData* out) {
  if (!IsSkeleton(prim) || !out) return false;

  (void)stage;

  // joints (uniform token[])
  {
    const Value* val = prim.GetPropertyValue("primvars:skel:joints");
    if (!val) {
      val = prim.GetPropertyValue("joints");
    }
    // skip - token array not yet supported in Value
    (void)val;
  }

  // bindTransforms (uniform matrix4d[]) - stored as float array, convert to double
  {
    const Value* val = prim.GetPropertyValue("primvars:skel:bindTransforms");
    if (val && val->is_array()) {
      const std::vector<float>* farray = val->as_float_array();
      if (farray) {
        out->bindTransforms.resize(farray->size());
        for (size_t i = 0; i < farray->size(); ++i) {
          out->bindTransforms[i] = static_cast<double>((*farray)[i]);
        }
      }
    }
  }

  // jointNames (uniform token[])
  {
    const Value* val = prim.GetPropertyValue("primvars:skel:jointNames");
    if (val) {
      const std::string* s = val->as_string();
      if (s) {
        out->jointNames.push_back(*s);
      }
      const std::string* tok = val->as_token();
      if (tok) {
        out->jointNames.push_back(*tok);
      }
    }
  }

  // restTransforms (uniform matrix4d[]) - stored as float array, convert to double
  {
    const Value* val = prim.GetPropertyValue("primvars:skel:restTransforms");
    if (val && val->is_array()) {
      const std::vector<float>* farray = val->as_float_array();
      if (farray) {
        out->restTransforms.resize(farray->size());
        for (size_t i = 0; i < farray->size(); ++i) {
          out->restTransforms[i] = static_cast<double>((*farray)[i]);
        }
      }
    }
  }

  // animationSource (rel skel:animationSource)
  {
    const std::vector<Path>* targets =
        prim.GetRelationship("skel:animationSource");
    if (targets && !targets->empty()) {
      out->animationSource = (*targets)[0].str();
      out->hasAnimationSource = true;
    }
  }

  return true;
}

// ============================================================
// SkelAnimation data helpers
// ============================================================

namespace {

bool ReadFloat3Array(const Value* val, std::vector<float>* out) {
  if (!val || !out) return false;
  const std::vector<float>* arr = val->as_float_array();
  if (arr) {
    *out = *arr;
    return true;
  }
  return false;
}

bool ReadQuatArray(const Value* val, std::vector<float>* out) {
  if (!val || !out) return false;
  // quatf[] stored as float4[] in Value (xyzw)
  const std::vector<float>* arr = val->as_float_array();
  if (arr) {
    *out = *arr;
    return true;
  }
  return false;
}

} // namespace

bool GetSkelAnimationData(const Stage& stage, const UsdPrim& prim,
                          SkelAnimationData* out, double time) {
  if (!IsSkelAnimation(prim) || !out) return false;

  AttributeEval eval(&stage);
  eval.SetTime(time);

  // blendShapes (uniform token[])
  {
    const Value* val = prim.GetPropertyValue("blendShapes");
    if (val) {
      const std::string* s = val->as_string();
      if (s) {
        out->blendShapes.push_back(*s);
      }
      const std::string* tok = val->as_token();
      if (tok) {
        out->blendShapes.push_back(*tok);
      }
    }
  }

  // blendShapeWeights (float[])
  {
    EvalResult result = eval.Eval(prim, "blendShapeWeights");
    if (result.success && result.value.is_array()) {
      const std::vector<float>* arr = result.value.as_float_array();
      if (arr) {
        out->blendShapeWeights = *arr;
        out->hasBlendShapes = true;
      }
    }
  }

  // joints (uniform token[])
  {
    const Value* val = prim.GetPropertyValue("joints");
    if (val) {
      const std::string* s = val->as_string();
      if (s) {
        out->joints.push_back(*s);
      }
      const std::string* tok = val->as_token();
      if (tok) {
        out->joints.push_back(*tok);
      }
    }
  }

  // rotations (quatf[])
  {
    EvalResult result = eval.Eval(prim, "rotations");
    if (result.success) {
      if (ReadQuatArray(&result.value, &out->rotations)) {
        out->hasRotations = true;
      }
    }
  }

  // scales (half3[] stored as float3[])
  {
    EvalResult result = eval.Eval(prim, "scales");
    if (result.success) {
      if (ReadFloat3Array(&result.value, &out->scales)) {
        out->hasScales = true;
      }
    }
  }

  // translations (float3[])
  {
    EvalResult result = eval.Eval(prim, "translations");
    if (result.success) {
      if (ReadFloat3Array(&result.value, &out->translations)) {
        out->hasTranslations = true;
      }
    }
  }

  return true;
}

bool GetSkelAnimationDataAtTime(const Stage& stage, const UsdPrim& prim,
                                SkelAnimationData* out, double time) {
  return GetSkelAnimationData(stage, prim, out, time);
}

// ============================================================
// BlendShape data
// ============================================================

bool GetBlendShapeData(const Stage& stage, const UsdPrim& prim,
                       BlendShapeData* out) {
  if (!IsBlendShape(prim) || !out) return false;

  (void)stage;

  // offsets (uniform vector3f[])
  {
    const Value* val = prim.GetPropertyValue("offsets");
    if (val && val->is_array()) {
      const std::vector<float>* arr = val->as_float_array();
      if (arr) {
        out->offsets = *arr;
      }
    }
  }

  // normalOffsets (uniform vector3f[])
  {
    const Value* val = prim.GetPropertyValue("normalOffsets");
    if (val && val->is_array()) {
      const std::vector<float>* arr = val->as_float_array();
      if (arr) {
        out->normalOffsets = *arr;
        out->hasNormalOffsets = true;
      }
    }
  }

  // pointIndices (uniform int[])
  {
    const Value* val = prim.GetPropertyValue("pointIndices");
    if (val && val->is_array()) {
      const std::vector<int32_t>* arr = val->as_int_array();
      if (arr) {
        out->pointIndices = *arr;
        out->hasPointIndices = true;
      }
    }
  }

  return true;
}

// ============================================================
// SkelRoot data
// ============================================================

bool GetSkelRootData(const Stage& stage, const UsdPrim& prim,
                     SkelRootData* out) {
  if (!IsSkelRoot(prim) || !out) return false;

  (void)stage;

  // skel:skeleton
  {
    const std::vector<Path>* targets =
        prim.GetRelationship("skel:skeleton");
    if (targets && !targets->empty()) {
      out->skeleton = (*targets)[0].str();
      out->hasSkeleton = true;
    }
  }

  // skel:animationSource
  {
    const std::vector<Path>* targets =
        prim.GetRelationship("skel:animationSource");
    if (targets && !targets->empty()) {
      out->animationSource = (*targets)[0].str();
      out->hasAnimationSource = true;
    }
  }

  return true;
}

// ============================================================
// Utility functions
// ============================================================

bool BuildSkelTopology(const std::vector<std::string>& joints,
                       std::vector<int>& dst, std::string* err) {
  dst.clear();

  if (joints.empty()) {
    if (err) *err = "Empty joints array";
    return false;
  }

  dst.resize(joints.size(), -1);

  // Build parent index from joint paths
  for (size_t i = 0; i < joints.size(); ++i) {
    const std::string& path = joints[i];

    // Find last '/' to get parent path
    size_t lastSlash = path.rfind('/');
    if (lastSlash == std::string::npos || lastSlash == 0) {
      // Root joint (no parent)
      dst[i] = -1;
      continue;
    }

    std::string parentPath = path.substr(0, lastSlash);

    // Find parent index
    bool found = false;
    for (size_t j = 0; j < joints.size(); ++j) {
      if (joints[j] == parentPath) {
        dst[i] = static_cast<int>(j);
        found = true;
        break;
      }
    }

    if (!found) {
      if (err) {
        *err = "Parent joint not found: " + parentPath +
               " for joint: " + path;
      }
      return false;
    }
  }

  return true;
}

bool SkelValidateTopology(const std::vector<int>& topology,
                          std::string* err) {
  if (topology.empty()) {
    if (err) *err = "Empty topology";
    return false;
  }

  // Check for single root
  int rootCount = 0;
  for (size_t i = 0; i < topology.size(); ++i) {
    if (topology[i] == -1) {
      ++rootCount;
    }
  }

  if (rootCount != 1) {
    if (err) {
      *err = "Expected 1 root joint, found " + std::to_string(rootCount);
    }
    return false;
  }

  // Check for valid parent indices
  for (size_t i = 0; i < topology.size(); ++i) {
    int parent = topology[i];
    if (parent >= static_cast<int>(topology.size())) {
      if (err) {
        *err = "Invalid parent index " + std::to_string(parent) +
               " at joint " + std::to_string(i);
      }
      return false;
    }
  }

  // Check for cycles using DFS
  std::vector<bool> visited(topology.size(), false);
  std::vector<bool> inStack(topology.size(), false);

  for (size_t i = 0; i < topology.size(); ++i) {
    if (topology[i] == -1) {
      // Start DFS from root
      std::vector<size_t> stack;
      stack.push_back(i);

      while (!stack.empty()) {
        size_t idx = stack.back();
        stack.pop_back();

        if (inStack[idx]) {
          if (err) {
            *err = "Cycle detected at joint " + std::to_string(idx);
          }
          return false;
        }

        if (visited[idx]) continue;
        visited[idx] = true;
        inStack[idx] = true;

        // Find children
        for (size_t j = 0; j < topology.size(); ++j) {
          if (topology[j] == static_cast<int>(idx)) {
            stack.push_back(j);
          }
        }

        inStack[idx] = false;
      }
    }
  }

  // Check all nodes visited (connected)
  for (size_t i = 0; i < topology.size(); ++i) {
    if (!visited[i]) {
      if (err) {
        *err = "Unreachable joint " + std::to_string(i);
      }
      return false;
    }
  }

  return true;
}

bool SkelNormalizeWeights(std::vector<float>& weights,
                          int numInfluencesPerComponent, float eps) {
  if (weights.empty() || numInfluencesPerComponent <= 0) return false;

  size_t numComponents = weights.size() /
    static_cast<size_t>(numInfluencesPerComponent);

  for (size_t i = 0; i < numComponents; ++i) {
    float sum = 0.0f;
    for (int j = 0; j < numInfluencesPerComponent; ++j) {
      sum += weights[i * numInfluencesPerComponent + j];
    }

    if (sum > eps) {
      float invSum = 1.0f / sum;
      for (int j = 0; j < numInfluencesPerComponent; ++j) {
        weights[i * numInfluencesPerComponent + j] *= invSum;
      }
    }
  }

  return true;
}

bool SkelSortInfluences(std::vector<int>& indices,
                        std::vector<float>& weights,
                        int numInfluencesPerComponent) {
  if (indices.empty() || weights.empty() ||
      numInfluencesPerComponent <= 0) {
    return false;
  }

  size_t numComponents = weights.size() /
    static_cast<size_t>(numInfluencesPerComponent);

  for (size_t i = 0; i < numComponents; ++i) {
    // Create index array for sorting
    std::vector<std::pair<float, int>> influences;
    influences.reserve(numInfluencesPerComponent);

    for (int j = 0; j < numInfluencesPerComponent; ++j) {
      size_t offset = i * numInfluencesPerComponent + j;
      influences.emplace_back(weights[offset], indices[offset]);
    }

    // Sort by weight descending
    std::sort(influences.begin(), influences.end(),
              [](const std::pair<float, int>& a,
                 const std::pair<float, int>& b) {
                return a.first > b.first;
              });

    // Write back sorted
    for (int j = 0; j < numInfluencesPerComponent; ++j) {
      size_t offset = i * numInfluencesPerComponent + j;
      weights[offset] = influences[j].first;
      indices[offset] = influences[j].second;
    }
  }

  return true;
}

} // namespace next
} // namespace tinyusdz
