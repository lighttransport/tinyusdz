// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Lazy array reference tests
//
// Verifies that numeric POD arrays read from USDC come back as lazy references
// into the retained source buffer, materialize correctly on access, and share
// the backing buffer across copies (shared_ptr refcount).

#include "test-check.hh"
#include <cstdint>
#include <cstdio>
#include <fstream>
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
#include "next/types/value-view.hh"
#include "next/writer/usdc-writer.hh"

using namespace tinyusdz::next;

int main() {
  std::cout << "=== TinyUSDZ Next Lazy Array Tests ===" << std::endl;

  // Empty ArrayView must be safe to use with begin()/end() range APIs.
  {
    Value empty = Value::MakeFloatArray(std::vector<float>{});
    ArrayScratch<float> scratch;
    ArrayView<float> view;
    NEXT_CHECK(GetFloatArrayView(empty, &scratch, &view));
    NEXT_CHECK(view.empty());
    NEXT_CHECK(view.begin() == view.end());
    std::vector<float> copied(view.begin(), view.end());
    NEXT_CHECK(copied.empty());
    NEXT_CHECK(view.size_bytes() == 0);
  }

  // ---- Build a stage with numeric POD arrays --------------------------------
  std::vector<float> points;        // 128 vec3f
  for (int i = 0; i < 128 * 3; i++) points.push_back(static_cast<float>(i) * 0.5f);
  std::vector<int32_t> indices;     // 256 ints
  for (int i = 0; i < 256; i++) indices.push_back(i * 7 - 11);
  std::vector<uint32_t> uids;       // 96 uints
  for (uint32_t i = 0; i < 96; i++) uids.push_back(i * 13u + 5u);
  std::vector<uint64_t> hashes;     // 32 uint64s
  for (uint64_t i = 0; i < 32; i++) hashes.push_back((i << 40) | (i * 17u));
  std::vector<float> quats;         // 64 quatf
  for (int i = 0; i < 64; i++) {
    quats.push_back(0.0f);
    quats.push_back(float(i));
    quats.push_back(0.0f);
    quats.push_back(1.0f);
  }
  std::vector<double> matrices;     // 8 matrix4d
  for (int i = 0; i < 8 * 16; i++) matrices.push_back(double(i) * 0.125);
  std::vector<float> tangents;      // 32 vec4f
  for (int i = 0; i < 32 * 4; i++) tangents.push_back(float(i) * 0.25f);
  std::vector<double> extents;      // 48 vec2d
  for (int i = 0; i < 48 * 2; i++) extents.push_back(double(i) * -0.5);
  std::vector<float> velocities0;   // 16 vec3f time sample
  std::vector<float> velocities1;   // 16 vec3f time sample
  for (int i = 0; i < 16 * 3; i++) {
    velocities0.push_back(float(i) * 0.125f);
    velocities1.push_back(float(i) * -0.25f + 7.0f);
  }

  StageBuilder sb;
  sb.SetDefaultPrim("Mesh1");
  LayerBuilder& lb = sb.GetLayerBuilder();
  lb.begin_prim("Mesh1", "Mesh");
  lb.add_property("points", Value::MakeFloat3Array(points));
  lb.add_property("faceVertexIndices", Value::MakeIntArray(indices));
  lb.add_property("primvars:ids", Value::MakeUIntArray(uids));
  lb.add_property("primvars:hashes", Value::MakeUInt64Array(hashes));
  lb.add_property("orientations",
                  Value::MakeFloatCompArray(std::vector<float>(quats),
                                            TypeId::Quatf, 4));
  lb.add_property("xforms",
                  Value::MakeDoubleCompArray(std::vector<double>(matrices),
                                             TypeId::Matrix4d, 16));
  lb.add_property("tangents",
                  Value::MakeFloatCompArray(std::vector<float>(tangents),
                                            TypeId::Float4, 4));
  lb.add_property("extent",
                  Value::MakeDoubleCompArray(std::vector<double>(extents),
                                             TypeId::Double2, 2));
  lb.add_time_sample("velocities", 0.0,
                     Value::MakeFloat3Array(std::vector<float>(velocities0)));
  lb.add_time_sample("velocities", 1.0,
                     Value::MakeFloat3Array(std::vector<float>(velocities1)));
  lb.end_prim();
  lb.finalize();
  Stage stage = sb.Build();

  // ---- Write to USDC in memory ---------------------------------------------
  std::vector<uint8_t> buf;
  USDCWriteResult wr = WriteUSDCToMemory(buf, stage);
  NEXT_CHECK(wr.success);
  NEXT_CHECK(!buf.empty());

  // ---- Read it back ---------------------------------------------------------
  USDCLoadResult lr = LoadUSDCFromMemory(buf.data(), buf.size());
  if (!lr.success) {
    std::cerr << "Load failed";
    if (!lr.errors.empty()) std::cerr << ": " << lr.errors[0].message;
    std::cerr << std::endl;
    return 1;
  }

  UsdPrim mesh = lr.stage.GetPrimAtPath("/Mesh1");
  NEXT_CHECK(mesh.IsValid());
  const PrimSpec* ps = mesh.GetPrimSpec();
  NEXT_CHECK(ps);

  // ---- TimeSamples: lazy array values --------------------------------------
  NEXT_CHECK(mesh.HasTimeSamples("velocities"));
  std::vector<double> velocity_times = mesh.GetTimeSampleTimes("velocities");
  NEXT_CHECK(velocity_times.size() == 2);
  NEXT_CHECK(velocity_times[0] == 0.0);
  NEXT_CHECK(velocity_times[1] == 1.0);
  Value s0, s1;
  const Value* v0 = mesh.GetValueAtTime("velocities", 0.0, &s0);
  const Value* v1 = mesh.GetValueAtTime("velocities", 1.0, &s1);
  NEXT_CHECK(v0 && v1);
  NEXT_CHECK(v0->is_array() && v1->is_array());
  NEXT_CHECK(v0->is_lazy() && v1->is_lazy());
  NEXT_CHECK(!v0->is_dirty() && !v1->is_dirty());
  NEXT_CHECK(v0->array_size() == velocities0.size() / 3);
  NEXT_CHECK(v1->array_size() == velocities1.size() / 3);
  {
    ArrayScratch<float> scratch;
    ArrayView<float> view;
    NEXT_CHECK(GetFloatArrayView(*v0, &scratch, &view));
    NEXT_CHECK(view.size == velocities0.size());
    for (size_t i = 0; i < velocities0.size(); i++) {
      NEXT_CHECK(view[i] == velocities0[i]);
    }
    NEXT_CHECK(v0->is_lazy());
  }
  Value v1_copy = v1->materialized_copy();
  NEXT_CHECK(!v1_copy.is_lazy());
  NEXT_CHECK(v1->is_lazy());
  const std::vector<float>* v1_arr = v1_copy.as_float_array();
  NEXT_CHECK(v1_arr && *v1_arr == velocities1);
  std::cout << "  time-sampled array values came back lazy" << std::endl;

  // ---- points: lazy Vec3f -> Float3 ----------------------------------------
  const Value* pv = ps->property_value("points");
  NEXT_CHECK(pv);
  NEXT_CHECK(pv->is_array());
  NEXT_CHECK(pv->is_lazy());                          // lazy BEFORE any access
  NEXT_CHECK(!pv->is_dirty());
  NEXT_CHECK(pv->array_size() == points.size() / 3);  // count without materializing
  std::cout << "  points came back lazy (count=" << pv->array_size() << ")" << std::endl;

  // Copy the lazy value: must stay lazy and SHARE the source buffer.
  {
    Value copy = *pv;
    NEXT_CHECK(copy.is_lazy());
    NEXT_CHECK(copy.lazy_ref() && pv->lazy_ref());
    NEXT_CHECK(copy.lazy_ref()->source.get() == pv->lazy_ref()->source.get());
    long uc_before = pv->lazy_ref()->source.use_count();
    NEXT_CHECK(uc_before >= 2);  // pv + copy both reference it
    // Materializing the copy must not disturb the original.
    const std::vector<float>* carr = copy.as_float_array();
    NEXT_CHECK(carr && carr->size() == points.size());
    NEXT_CHECK(!copy.is_lazy());
    NEXT_CHECK(pv->is_lazy());   // original untouched
    (void)uc_before;
  }
  // After the copy is destroyed, the original still resolves.

  // materialized_copy(): decode into a temp WITHOUT disturbing the lazy original
  // (the writer's transient-decode path that keeps peak RSS bounded to ~one
  // array). The decoded temp must match the source bytes, and pv stays lazy.
  {
    Value mc = pv->materialized_copy();
    NEXT_CHECK(!mc.is_lazy());                 // returned value is decoded
    NEXT_CHECK(pv->is_lazy());                 // ORIGINAL untouched (still lazy)
    const std::vector<float>* mcarr = mc.as_float_array();
    NEXT_CHECK(mcarr && mcarr->size() == points.size());
    for (size_t i = 0; i < points.size(); i++) NEXT_CHECK((*mcarr)[i] == points[i]);
  }
  NEXT_CHECK(pv->is_lazy());  // still lazy after the temp is destroyed

  // Borrowed read-only view: direct pointer into the retained crate payload for
  // uncompressed POD arrays, without materializing or dirtying the Value.
  {
    ArrayScratch<float> scratch;
    ArrayView<float> view;
    NEXT_CHECK(GetFloatArrayView(*pv, &scratch, &view));
    NEXT_CHECK(view.borrowed);
    NEXT_CHECK(view.size == points.size());
    NEXT_CHECK(scratch.storage.empty());
    for (size_t i = 0; i < points.size(); i++) NEXT_CHECK(view[i] == points[i]);
    NEXT_CHECK(pv->is_lazy());
    NEXT_CHECK(!pv->is_dirty());
    std::cout << "  points borrowed view reads without materializing" << std::endl;
  }

  // Materialize the original via accessor; verify contents byte-for-byte.
  const std::vector<float>* arr = pv->as_float_array();
  NEXT_CHECK(arr);
  NEXT_CHECK(arr->size() == points.size());
  for (size_t i = 0; i < points.size(); i++) NEXT_CHECK((*arr)[i] == points[i]);
  NEXT_CHECK(!pv->is_lazy());  // materialized after access
  std::cout << "  points materialized correctly (" << arr->size() << " floats)" << std::endl;

  // ---- faceVertexIndices: lazy Int -----------------------------------------
  const Value* iv = ps->property_value("faceVertexIndices");
  NEXT_CHECK(iv);
  NEXT_CHECK(iv->is_lazy());
  {
    ArrayScratch<int32_t> scratch;
    ArrayView<int32_t> view;
    NEXT_CHECK(GetIntArrayView(*iv, &scratch, &view));
    NEXT_CHECK(view.size == indices.size());
    for (size_t i = 0; i < indices.size(); i++) NEXT_CHECK(view[i] == indices[i]);
    NEXT_CHECK(iv->is_lazy());
    NEXT_CHECK(!iv->is_dirty());
    std::cout << "  indices view reads without materializing source"
              << (view.borrowed ? " (borrowed)" : " (scratch)") << std::endl;
  }
  const std::vector<int32_t>* ia = iv->as_int_array();
  NEXT_CHECK(ia);
  NEXT_CHECK(*ia == indices);
  NEXT_CHECK(!iv->is_lazy());
  std::cout << "  indices materialized correctly (" << ia->size() << " ints)" << std::endl;

  // ---- unsigned integer arrays: lazy UInt / UInt64 views --------------------
  const Value* uv = ps->property_value("primvars:ids");
  NEXT_CHECK(uv && uv->is_lazy());
  {
    ArrayScratch<uint32_t> scratch;
    ArrayView<uint32_t> view;
    NEXT_CHECK(GetUIntArrayView(*uv, &scratch, &view));
    NEXT_CHECK(view.size == uids.size());
    NEXT_CHECK(view.size_bytes() == uids.size() * sizeof(uint32_t));
    for (size_t i = 0; i < uids.size(); i++) NEXT_CHECK(view[i] == uids[i]);
    NEXT_CHECK(uv->is_lazy());
    NEXT_CHECK(!uv->is_dirty());
  }
  const Value* u64v = ps->property_value("primvars:hashes");
  NEXT_CHECK(u64v && u64v->is_lazy());
  {
    ArrayScratch<uint64_t> scratch;
    ArrayView<uint64_t> view;
    NEXT_CHECK(GetUInt64ArrayView(*u64v, &scratch, &view));
    NEXT_CHECK(view.size == hashes.size());
    NEXT_CHECK(view.size_bytes() == hashes.size() * sizeof(uint64_t));
    for (size_t i = 0; i < hashes.size(); i++) NEXT_CHECK(view[i] == hashes[i]);
    NEXT_CHECK(u64v->is_lazy());
    NEXT_CHECK(!u64v->is_dirty());
    std::cout << "  unsigned integer views read without materializing source"
              << (view.borrowed ? " (borrowed)" : " (scratch)") << std::endl;
  }

  // ---- orientations / xforms: newly supported vector/matrix array laziness --
  const Value* qv = ps->property_value("orientations");
  NEXT_CHECK(qv && qv->is_array() && qv->is_lazy());
  const std::vector<float>* qa = qv->as_float_array();
  NEXT_CHECK(qa && *qa == quats);
  const Value* mv = ps->property_value("xforms");
  NEXT_CHECK(mv && mv->is_array() && mv->is_lazy());
  const std::vector<double>* ma = mv->as_double_array();
  NEXT_CHECK(ma && *ma == matrices);
  std::cout << "  quat/matrix arrays came back lazy and materialized correctly" << std::endl;

  // ---- vec4f / vec2d: doc-named POD types must also be lazy -----------------
  const Value* tv = ps->property_value("tangents");
  NEXT_CHECK(tv && tv->is_array() && tv->is_lazy());
  const std::vector<float>* ta = tv->as_float_array();
  NEXT_CHECK(ta && *ta == tangents);
  const Value* ev = ps->property_value("extent");
  NEXT_CHECK(ev && ev->is_array() && ev->is_lazy());
  const std::vector<double>* ea = ev->as_double_array();
  NEXT_CHECK(ea && *ea == extents);
  std::cout << "  vec4f/vec2d arrays came back lazy and materialized correctly" << std::endl;

  // ---- A2: composition (Clone + Compositor) preserves laziness -------------
  {
    USDCLoadResult lr2 = LoadUSDCFromMemory(buf.data(), buf.size());
    NEXT_CHECK(lr2.success);
    const Layer* layer = lr2.stage.GetRootLayer();
    NEXT_CHECK(layer);
    const PrimSpec* sps = layer->prim_at_path("/Mesh1");
    NEXT_CHECK(sps);
    const Value* sv = sps->property_value("points");
    NEXT_CHECK(sv && sv->is_lazy());
    const CrateDataSource* src = sv->lazy_ref()->source.get();
    long uc0 = sv->lazy_ref()->source.use_count();

    // PrimSpec::Clone() preserves laziness and shares the source buffer.
    PrimSpec clone = sps->Clone();
    const Value* cv = clone.property_value("points");
    NEXT_CHECK(cv && cv->is_lazy());
    NEXT_CHECK(cv->lazy_ref()->source.get() == src);
    NEXT_CHECK(sv->lazy_ref()->source.use_count() > uc0);  // buffer shared, not copied
    std::cout << "  Clone() preserved lazy + shared source" << std::endl;

    // Compositor::Compose() preserves laziness and shares the source buffer.
    Compositor comp;
    std::unique_ptr<Layer> composed = comp.Compose(*layer);
    NEXT_CHECK(composed);
    const PrimSpec* cps = composed->prim_at_path("/Mesh1");
    NEXT_CHECK(cps);
    const Value* compv = cps->property_value("points");
    NEXT_CHECK(compv && compv->is_lazy());
    NEXT_CHECK(compv->lazy_ref()->source.get() == src);
    std::cout << "  Compose() preserved lazy + shared source" << std::endl;

    // Materializing the composed value must not disturb the source layer value.
    const std::vector<float>* compArr = compv->as_float_array();
    NEXT_CHECK(compArr && compArr->size() == points.size());
    NEXT_CHECK(!compv->is_lazy());
    NEXT_CHECK(sv->is_lazy());  // original opinion untouched
    std::cout << "  composition materialize is independent of source" << std::endl;
  }

  // ---- A3: writer byte pass-through ----------------------------------------
  {
    USDCLoadResult lr3 = LoadUSDCFromMemory(buf.data(), buf.size());
    NEXT_CHECK(lr3.success);
    const Layer* layer = lr3.stage.GetRootLayer();
    NEXT_CHECK(layer);

    CrateWriter writer;
    std::vector<uint8_t> out;
    CrateWriteResult wres = writer.WriteLayerToMemory(out, *layer);
    NEXT_CHECK(wres.success);
    // Numeric arrays (points Vec3f, indices Int, ids UInt, hashes UInt64,
    // orientations Quatf, xforms Matrix4d, tangents Vec4f, extent Vec2d, and
    // two velocities TimeSamples) copied verbatim.
    NEXT_CHECK(wres.arrays_passed_through >= 10);
    NEXT_CHECK(wres.arrays_reencoded == 0);
    std::cout << "  writer passed through " << wres.arrays_passed_through
              << " arrays (" << wres.arrays_reencoded << " reencoded)" << std::endl;

    // Re-read the pass-through output and verify contents are bit-identical.
    USDCLoadResult lr4 = LoadUSDCFromMemory(out.data(), out.size());
    NEXT_CHECK(lr4.success);
    UsdPrim mesh4 = lr4.stage.GetPrimAtPath("/Mesh1");
    NEXT_CHECK(mesh4.IsValid());
    const PrimSpec* ps4 = lr4.stage.GetRootLayer()->prim_at_path("/Mesh1");
    NEXT_CHECK(ps4);
    const std::vector<float>* pa = ps4->property_value("points")->as_float_array();
    NEXT_CHECK(pa && pa->size() == points.size());
    for (size_t i = 0; i < points.size(); i++) NEXT_CHECK((*pa)[i] == points[i]);
    const std::vector<int32_t>* ix =
        ps4->property_value("faceVertexIndices")->as_int_array();
    NEXT_CHECK(ix && *ix == indices);
    const std::vector<uint32_t>* ux =
        ps4->property_value("primvars:ids")->as_uint_array();
    NEXT_CHECK(ux && *ux == uids);
    const std::vector<uint64_t>* u64x =
        ps4->property_value("primvars:hashes")->as_uint64_array();
    NEXT_CHECK(u64x && *u64x == hashes);
    const std::vector<float>* oq =
        ps4->property_value("orientations")->as_float_array();
    NEXT_CHECK(oq && *oq == quats);
    const std::vector<double>* xm =
        ps4->property_value("xforms")->as_double_array();
    NEXT_CHECK(xm && *xm == matrices);
    Value rs0, rs1;
    const Value* rv0 = mesh4.GetValueAtTime("velocities", 0.0, &rs0);
    const Value* rv1 = mesh4.GetValueAtTime("velocities", 1.0, &rs1);
    NEXT_CHECK(rv0 && rv1 && rv0->is_lazy() && rv1->is_lazy());
    const std::vector<float>* rv0a = rv0->as_float_array();
    const std::vector<float>* rv1a = rv1->as_float_array();
    NEXT_CHECK(rv0a && *rv0a == velocities0);
    NEXT_CHECK(rv1a && *rv1a == velocities1);
    std::cout << "  pass-through output re-reads identically" << std::endl;

    // Dirty flag: a mutable accessor marks the value so pass-through is skipped.
    Value dv = *layer->prim_at_path("/Mesh1")->property_value("points");
    NEXT_CHECK(dv.is_lazy() && !dv.is_dirty());
    (void)dv.as_float_array();  // non-const accessor materializes + dirties
    NEXT_CHECK(!dv.is_lazy() && dv.is_dirty());
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
    NEXT_CHECK(ok);
    NEXT_CHECK(fstats.arrays_passed_through >= 10);
    NEXT_CHECK(fstats.arrays_reencoded == 0);
    std::cout << "  FlattenUSDCToUSDC: " << fstats.input_bytes << " -> "
              << fstats.output_bytes << " bytes, passthrough="
              << fstats.arrays_passed_through << std::endl;

    // Flattened output re-reads with identical array contents.
    USDCLoadResult lr5 = LoadUSDCFromMemory(fout.data(), fout.size());
    NEXT_CHECK(lr5.success);
    UsdPrim mesh5 = lr5.stage.GetPrimAtPath("/Mesh1");
    NEXT_CHECK(mesh5.IsValid());
    const PrimSpec* ps5 = lr5.stage.GetRootLayer()->prim_at_path("/Mesh1");
    NEXT_CHECK(ps5);
    const std::vector<float>* fpa = ps5->property_value("points")->as_float_array();
    NEXT_CHECK(fpa && fpa->size() == points.size());
    for (size_t i = 0; i < points.size(); i++) NEXT_CHECK((*fpa)[i] == points[i]);
    const std::vector<float>* fqa =
        ps5->property_value("orientations")->as_float_array();
    NEXT_CHECK(fqa && *fqa == quats);
    const std::vector<double>* fma =
        ps5->property_value("xforms")->as_double_array();
    NEXT_CHECK(fma && *fma == matrices);
    Value fs0, fs1;
    const Value* fv0 = mesh5.GetValueAtTime("velocities", 0.0, &fs0);
    const Value* fv1 = mesh5.GetValueAtTime("velocities", 1.0, &fs1);
    NEXT_CHECK(fv0 && fv1);
    const std::vector<float>* fv0a = fv0->as_float_array();
    const std::vector<float>* fv1a = fv1->as_float_array();
    NEXT_CHECK(fv0a && *fv0a == velocities0);
    NEXT_CHECK(fv1a && *fv1a == velocities1);
    std::cout << "  facade output re-reads identically" << std::endl;

    // Filesystem facade: USDA root + USDA sublayer must compose and write USDC.
    {
      const char* sub_path = "/tmp/tinyusdz_next_flatten_usda_sub.usda";
      const char* ref_path = "/tmp/tinyusdz_next_flatten_usda_ref.usda";
      const char* root_path = "/tmp/tinyusdz_next_flatten_usda_root.usda";
      {
        std::ofstream sub(sub_path, std::ios::binary);
        sub << "#usda 1.0\n"
               "def Xform \"World\"\n"
               "{\n"
               "  def Mesh \"FromSub\"\n"
               "  {\n"
               "    int[] faceVertexCounts = [3]\n"
               "    int[] faceVertexIndices = [0, 1, 2]\n"
               "    point3f[] points = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]\n"
               "  }\n"
               "}\n";
      }
      {
        std::ofstream ref(ref_path, std::ios::binary);
        ref << "#usda 1.0\n"
               "def Xform \"Referenced\"\n"
               "{\n"
               "  def Mesh \"FromReference\"\n"
               "  {\n"
               "    int[] faceVertexCounts = [3]\n"
               "    int[] faceVertexIndices = [0, 1, 2]\n"
               "    point3f[] points = [(0, 0, 0), (0, 1, 0), (0, 0, 1)]\n"
               "  }\n"
               "}\n";
      }
      {
        std::ofstream root(root_path, std::ios::binary);
        root << "#usda 1.0\n"
                "(\n"
                "  subLayers = [@./tinyusdz_next_flatten_usda_sub.usda@]\n"
                ")\n"
                "def Xform \"World\"\n"
                "{\n"
                "  def Xform \"Local\" {}\n"
                "  def Xform \"RefSlot\" (\n"
                "    prepend references = [@./tinyusdz_next_flatten_usda_ref.usda@</Referenced>]\n"
                "  ) {}\n"
                "}\n";
      }
      pipeline::FlattenStats file_stats;
      std::vector<uint8_t> file_out;
      std::string file_err;
      NEXT_CHECK(pipeline::FlattenUSDFileToUSDC(root_path, file_out, fopts,
                                            &file_stats, &file_err));
      NEXT_CHECK(!file_out.empty());
      USDCLoadResult file_lr = LoadUSDCFromMemory(file_out.data(),
                                                  file_out.size());
      NEXT_CHECK(file_lr.success);
      NEXT_CHECK(file_lr.stage.GetPrimAtPath("/World/FromSub").IsValid());
      NEXT_CHECK(file_lr.stage.GetPrimAtPath("/World/Local").IsValid());
      NEXT_CHECK(file_lr.stage.GetPrimAtPath("/World/RefSlot/FromReference").IsValid());
      NEXT_CHECK(file_stats.prim_count >= 3);
      std::cout << "  FlattenUSDFileToUSDC composes USDA sublayers/references" << std::endl;
    }

    // Owned (single-copy) facade path produces the same result.
    pipeline::FlattenStats ostats;
    std::vector<uint8_t> oout;
    std::string oin(reinterpret_cast<const char*>(buf.data()), buf.size());
    bool ook = pipeline::FlattenUSDCToUSDCOwned(std::move(oin), oout, {}, &ostats, &ferr);
    NEXT_CHECK(ook);
    NEXT_CHECK(ostats.arrays_passed_through == fstats.arrays_passed_through);
    NEXT_CHECK(oout.size() == fout.size());
    std::cout << "  owned facade path matches (passthrough="
              << ostats.arrays_passed_through << ")" << std::endl;
  }

  // ---- 8.3: mmap-backed CrateDataSource ------------------------------------
  {
    const char* path = "/tmp/next_lazy_mmap.usdc";
    {
      std::ofstream f(path, std::ios::binary);
      f.write(reinterpret_cast<const char*>(buf.data()),
              static_cast<std::streamsize>(buf.size()));
    }

    // Default load path memory-maps the file. Lazy values read straight from
    // the mapping; their CrateDataSource reports is_mmapped().
    USDCLoadResult lm = LoadUSDCFromFile(path);
    NEXT_CHECK(lm.success);
    const PrimSpec* mps = lm.stage.GetRootLayer()->prim_at_path("/Mesh1");
    NEXT_CHECK(mps);
    const Value* mpv = mps->property_value("points");
    NEXT_CHECK(mpv && mpv->is_lazy());
    NEXT_CHECK(mpv->lazy_ref()->source->is_mmapped() &&
           "default file load should be mmap-backed");
    const std::vector<float>* mpa = mpv->as_float_array();
    NEXT_CHECK(mpa && mpa->size() == points.size());
    for (size_t i = 0; i < points.size(); i++) NEXT_CHECK((*mpa)[i] == points[i]);
    // The mapping must outlive the reader: a lazy value materialized after the
    // load (above) already proved the shared_ptr keeps the mapping alive.
    NEXT_CHECK(lm.source_was_mmap);
    std::cout << "  file load is mmap-backed and materializes correctly"
              << std::endl;

    // Opting out falls back to an owned heap buffer (not mmapped), same data.
    USDCLoadOptions opt;
    opt.crate_options.use_mmap = false;
    USDCLoadResult lo = LoadUSDCFromFile(path, opt);
    NEXT_CHECK(lo.success);
    NEXT_CHECK(!lo.source_was_mmap);
    const Value* opv =
        lo.stage.GetRootLayer()->prim_at_path("/Mesh1")->property_value("points");
    NEXT_CHECK(opv && opv->is_lazy());
    NEXT_CHECK(!opv->lazy_ref()->source->is_mmapped() &&
           "use_mmap=false must use the owned buffer");
    const std::vector<float>* opa = opv->as_float_array();
    NEXT_CHECK(opa && *opa == *mpa);
    std::cout << "  use_mmap=false falls back to owned buffer, same data"
              << std::endl;

    pipeline::FlattenStats mmap_stats;
    std::vector<uint8_t> mmap_out;
    std::string mmap_err;
    NEXT_CHECK(pipeline::FlattenUSDFileToUSDC(path, mmap_out, {}, &mmap_stats,
                                          &mmap_err));
    NEXT_CHECK(mmap_stats.input_was_mmap);
    std::cout << "  file-path flatten reports mmap input attribution"
              << std::endl;

    std::remove(path);
  }

  std::cout << "All lazy array tests passed!" << std::endl;
  return 0;
}
