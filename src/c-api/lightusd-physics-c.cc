// SPDX-License-Identifier: Apache-2.0
#include "lightusd-physics-c.h"
#include "c-internal.hh"
#include "next/schema/physics-query.hh"
#include <cmath>
#include <new>
namespace n = lightusd::next;
using lightusd_internal::Fail;
struct lightusd_physics_snapshot { n::PhysicsSnapshot data; };
static_assert(static_cast<uint8_t>(n::PhysicsGroup::Count) == LIGHTUSD_PHYSICS_GROUP_COUNT,
              "C/C++ physics group inventories must agree");
namespace {
lightusd_sv View(const std::string& text) { return {text.data(), text.size()}; }
const n::PhysicsRecord* Record(const lightusd_physics_snapshot* snapshot, size_t i) {
  return snapshot && i < snapshot->data.records.size() ? &snapshot->data.records[i] : nullptr;
}
lightusd_status Missing() { return Fail(LIGHTUSD_ERR_NOT_FOUND, "Physics snapshot index out of range"); }
}
extern "C" {
void lightusd_physics_query_options_init(lightusd_physics_query_options* options) {
  if (!options) return;
  *options = {};
  options->struct_size = sizeof(*options);
  options->max_memory_bytes = 512ull * 1024 * 1024;
  options->max_records = 1000000;
}
lightusd_status lightusd_stage_query_physics(const lightusd_stage* stage,
    const lightusd_physics_query_options* options, lightusd_physics_snapshot** out) {
  if (out) *out = nullptr;
  if (!stage || !out) return Fail(LIGHTUSD_ERR_INVALID_ARG, "Physics stage/output is null");
  n::PhysicsQueryOptions query;
  if (options) {
    if (options->struct_size < sizeof(*options) || (options->flags & ~LIGHTUSD_PHYSICS_DEFAULT_TIME) ||
        options->max_memory_bytes > SIZE_MAX || options->max_records > SIZE_MAX ||
        !options->max_memory_bytes || !options->max_records ||
        (!(options->flags & LIGHTUSD_PHYSICS_DEFAULT_TIME) && !std::isfinite(options->time_code)))
      return Fail(LIGHTUSD_ERR_INVALID_ARG, "Invalid physics query options");
    query.max_memory = static_cast<size_t>(options->max_memory_bytes);
    query.max_records = static_cast<size_t>(options->max_records);
    query.time = options->flags & LIGHTUSD_PHYSICS_DEFAULT_TIME ? n::TimeQuery::Default() : n::TimeQuery::Numeric(options->time_code);
  }
  auto result = n::QueryPhysics(stage->ReadStage(), query);
  if (!result) return Fail(result.error().find("budget") != std::string::npos ||
      result.error().find("limit") != std::string::npos ? LIGHTUSD_ERR_RESOURCE_LIMIT : LIGHTUSD_ERR_PARSE, result.error());
  auto* snapshot = new (std::nothrow) lightusd_physics_snapshot;
  if (!snapshot) return Fail(LIGHTUSD_ERR_OUT_OF_MEMORY, "Cannot allocate physics snapshot");
  snapshot->data = std::move(*result);
  *out = snapshot;
  return LIGHTUSD_OK;
}
void lightusd_physics_snapshot_destroy(lightusd_physics_snapshot* snapshot) { delete snapshot; }
size_t lightusd_physics_snapshot_size(const lightusd_physics_snapshot* snapshot) { return snapshot ? snapshot->data.records.size() : 0; }
const char* lightusd_physics_group_name(uint32_t group) {
  return group < LIGHTUSD_PHYSICS_GROUP_COUNT ? n::PhysicsGroupName(static_cast<n::PhysicsGroup>(group)) : "";
}
lightusd_status lightusd_physics_snapshot_record(const lightusd_physics_snapshot* snapshot,
    size_t i, lightusd_physics_record_info* out) {
  if (!out) return Fail(LIGHTUSD_ERR_INVALID_ARG, "Physics record output is null");
  *out = {};
  const auto* record = Record(snapshot, i);
  if (!record) return Missing();
  *out = {View(record->prim_path), View(record->type_name), record->groups,
          record->api_schemas.size(), record->properties.size(), record->relationships.size()};
  return LIGHTUSD_OK;
}
lightusd_status lightusd_physics_snapshot_schema(const lightusd_physics_snapshot* snapshot,
    size_t i, size_t schema, lightusd_sv* out) {
  if (!out) return Fail(LIGHTUSD_ERR_INVALID_ARG, "Physics schema output is null");
  *out = {};
  const auto* record = Record(snapshot, i);
  if (!record || schema >= record->api_schemas.size()) return Missing();
  *out = View(record->api_schemas[schema]);
  return LIGHTUSD_OK;
}
lightusd_status lightusd_physics_snapshot_property(const lightusd_physics_snapshot* snapshot,
    size_t i, size_t property, lightusd_physics_property_info* info, lightusd_value** out) {
  if (info) *info = {};
  if (out) *out = nullptr;
  if (!info || !out) return Fail(LIGHTUSD_ERR_INVALID_ARG, "Physics property output is null");
  const auto* record = Record(snapshot, i);
  if (!record || property >= record->properties.size()) return Missing();
  auto* value = new (std::nothrow) lightusd_value;
  if (!value) return Fail(LIGHTUSD_ERR_OUT_OF_MEMORY, "Cannot allocate physics value");
  const auto& item = record->properties[property];
  value->v = item.value;
  *info = {View(item.name), View(item.type_name), uint8_t(item.uniform), uint8_t(item.blocked)};
  *out = value;
  return LIGHTUSD_OK;
}
lightusd_status lightusd_physics_snapshot_relationship(const lightusd_physics_snapshot* snapshot,
    size_t i, size_t relationship, lightusd_sv* name, lightusd_strlist** out) {
  if (name) *name = {};
  if (out) *out = nullptr;
  if (!name || !out) return Fail(LIGHTUSD_ERR_INVALID_ARG, "Physics relationship output is null");
  const auto* record = Record(snapshot, i);
  if (!record || relationship >= record->relationships.size()) return Missing();
  auto* targets = new (std::nothrow) lightusd_strlist;
  if (!targets) return Fail(LIGHTUSD_ERR_OUT_OF_MEMORY, "Cannot allocate physics targets");
  const auto& item = record->relationships[relationship];
  targets->items = item.targets;
  *name = View(item.name);
  *out = targets;
  return LIGHTUSD_OK;
}
}  // extern C
