// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Lazy time-sample tests
//
// Verifies that gate-eligible scalar time samples read from USDC stay as
// undecoded lazy runs (LazyTimeSamplesRef) in TimeSampleStorage, materialize
// correctly on demand, fall back to eager storage for ineligible sample
// types, round-trip byte-identically, interpolate, and stay COW-safe when a
// storage is shared between prims.

#include "test-check.hh"
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "next/crate/crate-data-source.hh"
#include "next/crate/crate-format.hh"
#include "next/crate/lazy-array.hh"
#include "next/layer/layer.hh"
#include "next/layer/prim-spec.hh"
#include "next/reader/usdc-reader.hh"
#include "next/stage/stage.hh"
#include "next/types/value.hh"
#include "next/writer/usdc-writer.hh"

using namespace tinyusdz::next;

int main() {
  std::cout << "=== TinyUSDZ Next Lazy TimeSample Tests ===" << std::endl;

  // ---- Build a stage with scalar + ineligible time samples -----------------
  StageBuilder sb;
  sb.SetDefaultPrim("Anim");
  LayerBuilder& lb = sb.GetLayerBuilder();
  lb.begin_prim("Anim", "Xform");
  // Arbitrary (not float-exact) doubles: stored non-inlined in the crate.
  lb.add_time_sample("weight", 0.0, Value(0.123456789));
  lb.add_time_sample("weight", 10.0, Value(0.987654321));
  lb.add_time_sample("weight", 20.0, Value(0.5));  // float-exact -> inlined
  // Boxed scalar vec3d / matrix4d samples (outpost's dominant sample types).
  lb.add_time_sample("xformOp:translate", 0.0, Value::MakeDouble3(1.5, -2.25, 3.125));
  lb.add_time_sample("xformOp:translate", 10.0, Value::MakeDouble3(-0.1, 0.2, -0.3));
  double m0[16], m1[16];
  for (int i = 0; i < 16; ++i) { m0[i] = i * 0.375; m1[i] = 8.0 - i * 0.125; }
  lb.add_time_sample("xformOp:transform", 0.0, Value::MakeMatrix4d(m0));
  lb.add_time_sample("xformOp:transform", 10.0, Value::MakeMatrix4d(m1));
  // Token-valued samples are NOT lazy-eligible (index-table type) -> eager.
  lb.add_time_sample("visibility", 0.0, Value::MakeToken(std::string_view("inherited")));
  lb.add_time_sample("visibility", 10.0, Value::MakeToken(std::string_view("invisible")));
  lb.end_prim();
  lb.finalize();
  Stage stage = sb.Build();

  std::vector<uint8_t> buf;
  USDCWriteResult wr = WriteUSDCToMemory(buf, stage);
  NEXT_CHECK(wr.success);

  USDCLoadResult lr = LoadUSDCFromMemory(buf.data(), buf.size());
  NEXT_CHECK(lr.success);
  UsdPrim prim = lr.stage.GetPrimAtPath("/Anim");
  NEXT_CHECK(prim.IsValid());
  const PrimSpec* ps = prim.GetPrimSpec();
  NEXT_CHECK(ps);

  PropNameTable& names = GetPropNameTable();

  // ---- Eligible scalar samples come back as lazy offsets -------------------
  {
    const auto* samples = ps->time_samples(names.intern("weight"));
    NEXT_CHECK(samples && samples->size() == 3);
    for (const auto& kv : *samples) {
      NEXT_CHECK(TimeSampleStorage::is_lazy_offset(kv.second));
    }
    Value scratch;
    const Value* v0 = ps->time_sample_value((*samples)[0].second, &scratch);
    NEXT_CHECK(v0 && v0->as_double() && *v0->as_double() == 0.123456789);
    const Value* v1 = ps->time_sample_value((*samples)[1].second, &scratch);
    NEXT_CHECK(v1 && v1->as_double() && *v1->as_double() == 0.987654321);
    const Value* v2 = ps->time_sample_value((*samples)[2].second, &scratch);
    NEXT_CHECK(v2 && v2->as_double() && *v2->as_double() == 0.5);
    // No scratch supplied -> lazy sample cannot materialize.
    NEXT_CHECK(ps->time_sample_value((*samples)[0].second, nullptr) == nullptr);
  }
  {
    const auto* samples = ps->time_samples(names.intern("xformOp:translate"));
    NEXT_CHECK(samples && samples->size() == 2);
    NEXT_CHECK(TimeSampleStorage::is_lazy_offset((*samples)[0].second));
    Value scratch;
    const Value* v = ps->time_sample_value((*samples)[0].second, &scratch);
    NEXT_CHECK(v && v->as_double3());
    NEXT_CHECK(v->as_double3()[0] == 1.5 && v->as_double3()[1] == -2.25 &&
               v->as_double3()[2] == 3.125);
  }
  {
    const auto* samples = ps->time_samples(names.intern("xformOp:transform"));
    NEXT_CHECK(samples && samples->size() == 2);
    NEXT_CHECK(TimeSampleStorage::is_lazy_offset((*samples)[1].second));
    Value scratch;
    const Value* v = ps->time_sample_value((*samples)[1].second, &scratch);
    NEXT_CHECK(v && v->as_matrix4d());
    NEXT_CHECK(std::memcmp(v->as_matrix4d(), m1, sizeof(m1)) == 0);
  }

  // ---- Ineligible (token) samples stay eager --------------------------------
  {
    const auto* samples = ps->time_samples(names.intern("visibility"));
    NEXT_CHECK(samples && samples->size() == 2);
    for (const auto& kv : *samples) {
      NEXT_CHECK(!TimeSampleStorage::is_lazy_offset(kv.second));
    }
    Value scratch;
    const Value* v = ps->time_sample_value((*samples)[1].second, &scratch);
    NEXT_CHECK(v && v->as_token() && *v->as_token() == "invisible");
  }

  // ---- Interpolation over lazy scalars --------------------------------------
  {
    SampleResult sr = ps->interpolate_time_sample(names.intern("weight"), 5.0);
    NEXT_CHECK(sr.success);
    const double* d = sr.value.as_double();
    NEXT_CHECK(d);
    const double expect = 0.123456789 + (0.987654321 - 0.123456789) * 0.5;
    NEXT_CHECK(*d > expect - 1e-12 && *d < expect + 1e-12);
  }

  // ---- Held-value stage query through the scratch API ----------------------
  {
    Value scratch;
    const Value* v = prim.GetValueAtTime("weight", 12.0, &scratch);  // held: t=10
    NEXT_CHECK(v && v->as_double() && *v->as_double() == 0.987654321);
  }

  // ---- Round-trip byte identity ---------------------------------------------
  {
    std::vector<uint8_t> buf2;
    USDCWriteResult wr2 = WriteUSDCToMemory(buf2, lr.stage);
    NEXT_CHECK(wr2.success);
    NEXT_CHECK(buf2 == buf);
  }

  // ---- Direct storage-level lazy run + COW share ----------------------------
  {
    // Hand-build a values block of two INLINED Double reps (payload = float
    // bits) so no payload bytes are needed beyond the rep run itself.
    std::string bytes(8 + 2 * 8, '\0');
    uint64_t n = 2;
    std::memcpy(&bytes[0], &n, 8);
    float f0 = 1.5f, f1 = -4.25f;
    uint32_t b0, b1;
    std::memcpy(&b0, &f0, 4);
    std::memcpy(&b1, &f1, 4);
    uint64_t r0 = ValueRep::Make(CrateTypeId::Double, b0, false, true).raw();
    uint64_t r1 = ValueRep::Make(CrateTypeId::Double, b1, false, true).raw();
    std::memcpy(&bytes[8], &r0, 8);
    std::memcpy(&bytes[16], &r1, 8);
    auto source = CrateDataSource::Adopt(std::move(bytes), CrateVersion{});
    NEXT_CHECK(source);

    LazyTimeSamplesRef ref;
    ref.source = source;
    ref.vals_pos = 0;
    ref.count = 2;
    NEXT_CHECK(IsLazyEligibleTimeSampleRep(*source, ValueRep(r0)));

    PropNameId pid = names.intern("lazyprop");
    PrimSpec a("A");
    std::vector<double> times = {0.0, 1.0};
    NEXT_CHECK(a.add_lazy_time_samples(pid, times, ref));
    NEXT_CHECK(a.has_time_samples(pid));

    Value scratch;
    const auto* asamples = a.time_samples(pid);
    NEXT_CHECK(asamples && asamples->size() == 2);
    const Value* av = a.time_sample_value((*asamples)[1].second, &scratch);
    NEXT_CHECK(av && av->as_double() && *av->as_double() == double(-4.25f));

    // Share into B, then mutate B: copy-on-write must leave A untouched and
    // keep B's lazy samples intact alongside the new eager one.
    PrimSpec b("B");
    NEXT_CHECK(b.share_time_samples_from(a));
    b.add_time_sample(pid, 2.0, Value(9.75));
    const auto* bsamples = b.time_samples(pid);
    NEXT_CHECK(bsamples && bsamples->size() == 3);
    NEXT_CHECK(a.time_samples(pid)->size() == 2);  // A unchanged
    const Value* bv0 = b.time_sample_value((*bsamples)[0].second, &scratch);
    NEXT_CHECK(bv0 && bv0->as_double() && *bv0->as_double() == double(1.5f));
    Value scratch2;
    const Value* bv2 = b.time_sample_value((*bsamples)[2].second, &scratch2);
    NEXT_CHECK(bv2 && bv2->as_double() && *bv2->as_double() == 9.75);
  }

  std::cout << "All lazy time-sample tests passed!" << std::endl;
  return 0;
}
