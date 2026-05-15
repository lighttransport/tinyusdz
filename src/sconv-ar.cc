// SPDX-License-Identifier: Apache 2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// USDC writer: AR/Interactive prim property extraction (Apple Preliminary_*)
//
#include "sconv-detail.hh"
#include "usdAR.hh"

namespace tinyusdz {
namespace experimental {

#define EXTRACT_TYPED(attr, name) do { \
  auto _opt = (attr).get_value(); \
  if (_opt.has_value()) { \
    crate::CrateValue _cv; \
    value::Value _v(_opt.value()); \
    std::string _cerr; \
    if (ConvertValue(_v, _cv, &_cerr)) { \
      fields.push_back({(name), _cv}); \
    } \
  } \
} while(0)

#define EXTRACT_FALLBACK(attr, name) do { \
  crate::CrateValue _cv; \
  value::Value _v((attr).get_value()); \
  std::string _cerr; \
  if (ConvertValue(_v, _cv, &_cerr)) { \
    fields.push_back({(name), _cv}); \
  } \
} while(0)

#define EXTRACT_TOKEN(attr, name) do { \
  auto _opt = (attr).get_value(); \
  if (_opt.has_value()) { \
    crate::CrateValue _cv; \
    _cv.Set(_opt.value()); \
    fields.push_back({(name), _cv}); \
  } \
} while(0)

#define EXTRACT_TOKEN_FALLBACK(attr, name) do { \
  crate::CrateValue _cv; \
  _cv.Set((attr).get_value()); \
  fields.push_back({(name), _cv}); \
} while(0)

#define EXTRACT_REL(rp, name) do { \
  if ((rp).authored()) { \
    ConvertRelationshipToFields((name), (rp).relationship(), prim_path, err); \
  } \
} while(0)

// ============================================================================
// Preliminary_PhysicsGravitationalForce
// ============================================================================
bool CrateWriter::ExtractPreliminaryGravitationalForceProperties(
    const Prim &prim, const Path &prim_path,
    crate::FieldValuePairVector &fields, std::string *err) {
  const auto *p = prim.data().as<Preliminary_PhysicsGravitationalForce>();
  if (!p) { if (err) *err = "Failed to cast to Preliminary_PhysicsGravitationalForce"; return false; }
  EXTRACT_FALLBACK(p->acceleration, "physics:gravitationalForce:acceleration");
  (void)prim_path;
  return true;
}

// ============================================================================
// Preliminary_InfiniteColliderPlane
// ============================================================================
bool CrateWriter::ExtractPreliminaryInfiniteColliderPlaneProperties(
    const Prim &prim, const Path &prim_path,
    crate::FieldValuePairVector &fields, std::string *err) {
  const auto *p = prim.data().as<Preliminary_InfiniteColliderPlane>();
  if (!p) { if (err) *err = "Failed to cast to Preliminary_InfiniteColliderPlane"; return false; }
  EXTRACT_FALLBACK(p->position, "position");
  EXTRACT_FALLBACK(p->normal, "normal");
  (void)prim_path;
  return true;
}

// ============================================================================
// Preliminary_ReferenceImage
// ============================================================================
bool CrateWriter::ExtractPreliminaryReferenceImageProperties(
    const Prim &prim, const Path &prim_path,
    crate::FieldValuePairVector &fields, std::string *err) {
  const auto *p = prim.data().as<Preliminary_ReferenceImage>();
  if (!p) { if (err) *err = "Failed to cast to Preliminary_ReferenceImage"; return false; }
  EXTRACT_TYPED(p->image, "image");
  EXTRACT_FALLBACK(p->physicalWidth, "physicalWidth");
  (void)prim_path;
  return true;
}

// ============================================================================
// Preliminary_Behavior
// ============================================================================
bool CrateWriter::ExtractPreliminaryBehaviorProperties(
    const Prim &prim, const Path &prim_path,
    crate::FieldValuePairVector &fields, std::string *err) {
  const auto *p = prim.data().as<Preliminary_Behavior>();
  if (!p) { if (err) *err = "Failed to cast to Preliminary_Behavior"; return false; }
  EXTRACT_REL(p->triggers, "triggers");
  EXTRACT_REL(p->actions, "actions");
  EXTRACT_FALLBACK(p->exclusive, "exclusive");
  return true;
}

// ============================================================================
// Preliminary_Trigger
// ============================================================================
bool CrateWriter::ExtractPreliminaryTriggerProperties(
    const Prim &prim, const Path &prim_path,
    crate::FieldValuePairVector &fields, std::string *err) {
  const auto *p = prim.data().as<Preliminary_Trigger>();
  if (!p) { if (err) *err = "Failed to cast to Preliminary_Trigger"; return false; }
  EXTRACT_TOKEN(p->info_id, "info:id");
  (void)prim_path;
  return true;
}

// ============================================================================
// Preliminary_Action
// ============================================================================
bool CrateWriter::ExtractPreliminaryActionProperties(
    const Prim &prim, const Path &prim_path,
    crate::FieldValuePairVector &fields, std::string *err) {
  const auto *p = prim.data().as<Preliminary_Action>();
  if (!p) { if (err) *err = "Failed to cast to Preliminary_Action"; return false; }
  EXTRACT_TOKEN(p->info_id, "info:id");
  EXTRACT_TOKEN_FALLBACK(p->multiplePerformOperation, "multiplePerformOperation");
  (void)prim_path;
  return true;
}

// ============================================================================
// Preliminary_Text
// ============================================================================
bool CrateWriter::ExtractPreliminaryTextProperties(
    const Prim &prim, const Path &prim_path,
    crate::FieldValuePairVector &fields, std::string *err) {
  const auto *p = prim.data().as<Preliminary_Text>();
  if (!p) { if (err) *err = "Failed to cast to Preliminary_Text"; return false; }
  // String types need direct CrateValue::Set() — not EXTRACT_FALLBACK
  // which goes through value::Value and loses type info.
  if (p->content.authored()) {
    crate::CrateValue cv;
    cv.Set(p->content.get_value());
    fields.push_back({"content", cv});
  }
  if (p->font.authored()) {
    auto fv = p->font.get_value();
    if (fv.has_value()) {
      crate::CrateValue cv;
      cv.Set(fv.value());
      fields.push_back({"font", cv});
    }
  }
  EXTRACT_FALLBACK(p->pointSize, "pointSize");
  EXTRACT_TYPED(p->width, "width");
  EXTRACT_TYPED(p->height, "height");
  EXTRACT_FALLBACK(p->depth, "depth");
  EXTRACT_TOKEN_FALLBACK(p->wrapMode, "wrapMode");
  EXTRACT_TOKEN_FALLBACK(p->horizontalAlignment, "horizontalAlignment");
  EXTRACT_TOKEN_FALLBACK(p->verticalAlignment, "verticalAlignment");
  (void)prim_path;
  return true;
}

}  // namespace experimental
}  // namespace tinyusdz
