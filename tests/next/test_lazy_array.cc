// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Lazy array reference tests
//
// Verifies that numeric POD arrays read from USDC come back as lazy references
// into the retained source buffer, materialize correctly on access, and share
// the backing buffer across copies (shared_ptr refcount).

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#include "next/composition/composition.hh"
#include "next/crate/crate-data-source.hh"
#include "next/crate/crate-writer.hh"
#include "next/crate/lazy-array.hh"
#include "next/layer/layer.hh"
#include "next/pipeline/flatten.hh"
#include "next/reader/usdc-reader.hh"
#include "next/stage/stage.hh"
#include "next/types/value.hh"
#include "next/writer/usdc-writer.hh"

using namespace tinyusdz::next;

int main() {
  std::cout << "=== TinyUSDZ Next Lazy Array Tests ===" << std::endl;

  // ---- Build a stage with numeric POD arrays --------------------------------
  std::vector<float> points;        // 128 vec3f
  for (int i = 0; i < 128 * 3; i++) points.push_back(static_cast<float>(i) * 0.5f);
  std::vector<int32_t> indices;     // 256 ints
  for (int i = 0; i < 256; i++) indices.push_back(i * 7 - 11);

  StageBuilder sb;
  sb.SetDefaultPrim("Mesh1");
  LayerBuilder& lb = sb.GetLayerBuilder();
  lb.begin_prim("Mesh1", "Mesh");
  lb.add_property("points", Value::MakeFloat3Array(points));
  lb.add_property("faceVertexIndices", Value::MakeIntArray(indices));
  lb.end_prim();
  lb.finalize();
  Stage stage = sb.Build();

  // ---- Write to USDC in memory ---------------------------------------------
  std::vector<uint8_t> buf;
  USDCWriteResult wr = WriteUSDCToMemory(buf, stage);
  assert(wr.success);
  assert(!buf.empty());

  // ---- Read it back ---------------------------------------------------------
  USDCLoadResult lr = LoadUSDCFromMemory(buf.data(), buf.size());
  if (!lr.success) {
    std::cerr << "Load failed";
    if (!lr.errors.empty()) std::cerr << ": " << lr.errors[0].message;
    std::cerr << std::endl;
    return 1;
  }

  UsdPrim mesh = lr.stage.GetPrimAtPath("/Mesh1");
  assert(mesh.IsValid());
  const PrimSpec* ps = mesh.GetPrimSpec();
  assert(ps);

  // ---- points: lazy Vec3f -> Float3 ----------------------------------------
  const Value* pv = ps->property_value("points");
  assert(pv);
  assert(pv->is_array());
  assert(pv->is_lazy());                          // lazy BEFORE any access
  assert(!pv->is_dirty());
  assert(pv->array_size() == points.size() / 3);  // count without materializing
  std::cout << "  points came back lazy (count=" << pv->array_size() << ")" << std::endl;

  // Copy the lazy value: must stay lazy and SHARE the source buffer.
  {
    Value copy = *pv;
    assert(copy.is_lazy());
    assert(copy.lazy_ref() && pv->lazy_ref());
    assert(copy.lazy_ref()->source.get() == pv->lazy_ref()->source.get());
    long uc_before = pv->lazy_ref()->source.use_count();
    assert(uc_before >= 2);  // pv + copy both reference it
    // Materializing the copy must not disturb the original.
    const std::vector<float>* carr = copy.as_float_array();
    assert(carr && carr->size() == points.size());
    assert(!copy.is_lazy());
    assert(pv->is_lazy());   // original untouched
    (void)uc_before;
  }
  // After the copy is destroyed, the original still resolves.

  // Materialize the original via accessor; verify contents byte-for-byte.
  const std::vector<float>* arr = pv->as_float_array();
  assert(arr);
  assert(arr->size() == points.size());
  for (size_t i = 0; i < points.size(); i++) assert((*arr)[i] == points[i]);
  assert(!pv->is_lazy());  // materialized after access
  std::cout << "  points materialized correctly (" << arr->size() << " floats)" << std::endl;

  // ---- faceVertexIndices: lazy Int -----------------------------------------
  const Value* iv = ps->property_value("faceVertexIndices");
  assert(iv);
  assert(iv->is_lazy());
  const std::vector<int32_t>* ia = iv->as_int_array();
  assert(ia);
  assert(*ia == indices);
  assert(!iv->is_lazy());
  std::cout << "  indices materialized correctly (" << ia->size() << " ints)" << std::endl;

  // ---- A2: composition (Clone + Compositor) preserves laziness -------------
  {
    USDCLoadResult lr2 = LoadUSDCFromMemory(buf.data(), buf.size());
    assert(lr2.success);
    const Layer* layer = lr2.stage.GetRootLayer();
    assert(layer);
    const PrimSpec* sps = layer->prim_at_path("/Mesh1");
    assert(sps);
    const Value* sv = sps->property_value("points");
    assert(sv && sv->is_lazy());
    const CrateDataSource* src = sv->lazy_ref()->source.get();
    long uc0 = sv->lazy_ref()->source.use_count();

    // PrimSpec::Clone() preserves laziness and shares the source buffer.
    PrimSpec clone = sps->Clone();
    const Value* cv = clone.property_value("points");
    assert(cv && cv->is_lazy());
    assert(cv->lazy_ref()->source.get() == src);
    assert(sv->lazy_ref()->source.use_count() > uc0);  // buffer shared, not copied
    std::cout << "  Clone() preserved lazy + shared source" << std::endl;

    // Compositor::Compose() preserves laziness and shares the source buffer.
    Compositor comp;
    std::unique_ptr<Layer> composed = comp.Compose(*layer);
    assert(composed);
    const PrimSpec* cps = composed->prim_at_path("/Mesh1");
    assert(cps);
    const Value* compv = cps->property_value("points");
    assert(compv && compv->is_lazy());
    assert(compv->lazy_ref()->source.get() == src);
    std::cout << "  Compose() preserved lazy + shared source" << std::endl;

    // Materializing the composed value must not disturb the source layer value.
    const std::vector<float>* compArr = compv->as_float_array();
    assert(compArr && compArr->size() == points.size());
    assert(!compv->is_lazy());
    assert(sv->is_lazy());  // original opinion untouched
    std::cout << "  composition materialize is independent of source" << std::endl;
  }

  // ---- A3: writer byte pass-through ----------------------------------------
  {
    USDCLoadResult lr3 = LoadUSDCFromMemory(buf.data(), buf.size());
    assert(lr3.success);
    const Layer* layer = lr3.stage.GetRootLayer();
    assert(layer);

    CrateWriter writer;
    std::vector<uint8_t> out;
    CrateWriteResult wres = writer.WriteLayerToMemory(out, *layer);
    assert(wres.success);
    // Both numeric arrays (points Vec3f, indices Int) copied verbatim.
    assert(wres.arrays_passed_through >= 2);
    assert(wres.arrays_reencoded == 0);
    std::cout << "  writer passed through " << wres.arrays_passed_through
              << " arrays (" << wres.arrays_reencoded << " reencoded)" << std::endl;

    // Re-read the pass-through output and verify contents are bit-identical.
    USDCLoadResult lr4 = LoadUSDCFromMemory(out.data(), out.size());
    assert(lr4.success);
    const PrimSpec* ps4 = lr4.stage.GetRootLayer()->prim_at_path("/Mesh1");
    assert(ps4);
    const std::vector<float>* pa = ps4->property_value("points")->as_float_array();
    assert(pa && pa->size() == points.size());
    for (size_t i = 0; i < points.size(); i++) assert((*pa)[i] == points[i]);
    const std::vector<int32_t>* ix =
        ps4->property_value("faceVertexIndices")->as_int_array();
    assert(ix && *ix == indices);
    std::cout << "  pass-through output re-reads identically" << std::endl;

    // Dirty flag: a mutable accessor marks the value so pass-through is skipped.
    Value dv = *layer->prim_at_path("/Mesh1")->property_value("points");
    assert(dv.is_lazy() && !dv.is_dirty());
    (void)dv.as_float_array();  // non-const accessor materializes + dirties
    assert(!dv.is_lazy() && dv.is_dirty());
    std::cout << "  mutable access sets dirty (disables pass-through)" << std::endl;
  }

  // ---- C1: pipeline facade (read -> flatten -> write) ----------------------
  {
    pipeline::FlattenOptions fopts;
    pipeline::FlattenStats fstats;
    std::vector<uint8_t> fout;
    std::string ferr;
    bool ok = pipeline::FlattenUSDCToUSDC(buf.data(), buf.size(), fout, fopts,
                                          &fstats, &ferr);
    assert(ok);
    assert(fstats.arrays_passed_through >= 2);
    assert(fstats.arrays_reencoded == 0);
    std::cout << "  FlattenUSDCToUSDC: " << fstats.input_bytes << " -> "
              << fstats.output_bytes << " bytes, passthrough="
              << fstats.arrays_passed_through << std::endl;

    // Flattened output re-reads with identical array contents.
    USDCLoadResult lr5 = LoadUSDCFromMemory(fout.data(), fout.size());
    assert(lr5.success);
    const PrimSpec* ps5 = lr5.stage.GetRootLayer()->prim_at_path("/Mesh1");
    assert(ps5);
    const std::vector<float>* fpa = ps5->property_value("points")->as_float_array();
    assert(fpa && fpa->size() == points.size());
    for (size_t i = 0; i < points.size(); i++) assert((*fpa)[i] == points[i]);
    std::cout << "  facade output re-reads identically" << std::endl;

    // Owned (single-copy) facade path produces the same result.
    pipeline::FlattenStats ostats;
    std::vector<uint8_t> oout;
    std::string oin(reinterpret_cast<const char*>(buf.data()), buf.size());
    bool ook = pipeline::FlattenUSDCToUSDCOwned(std::move(oin), oout, {}, &ostats, &ferr);
    assert(ook);
    assert(ostats.arrays_passed_through == fstats.arrays_passed_through);
    assert(oout.size() == fout.size());
    std::cout << "  owned facade path matches (passthrough="
              << ostats.arrays_passed_through << ")" << std::endl;
  }

  std::cout << "All lazy array tests passed!" << std::endl;
  return 0;
}
