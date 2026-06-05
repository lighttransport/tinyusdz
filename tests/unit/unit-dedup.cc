#ifdef _MSC_VER
#define NOMINMAX
#endif

#define TEST_NO_MAIN
#include "acutest.h"

#include "unit-dedup.h"
#include "../../src/crate-reader.hh"
#include "../../src/crate-writer.hh"
#include "../../src/tinyusdz.hh"
#include "../../src/core/prim.hh"
#include "../../src/core/prim-spec.hh"
#include "../../src/primvar.hh"
#include "../../src/layer.hh"
#include "../../src/stream-reader.hh"
#include "../../src/value-types.hh"
#include "../../src/timesamples.hh"
#include "../../src/io-util.hh"
#include <cctype>
#include <cmath>
#include <functional>
#include <fstream>
#include <limits>

using namespace tinyusdz;
using namespace tinyusdz::experimental;

using CrateDataTypeId = tinyusdz::crate::CrateDataTypeId;
using CrateValueRep = tinyusdz::crate::ValueRep;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static size_t GetFileSize(const std::string& filename) {
  std::ifstream file(filename, std::ios::binary | std::ios::ate);
  if (!file.is_open()) return 0;
  return static_cast<size_t>(file.tellg());
}

// Build a Stage with a single Xform prim carrying one custom timesampled
// attribute named "animAttr".
static Stage MakeAnimStage(const std::string& primName,
                           const std::string& typeName,
                           const value::TimeSamples& ts,
                           const value::Value& defaultValue) {
  Stage stage;

  Xform xform;
  xform.name = primName;
  xform.spec = Specifier::Def;

  Attribute attr;
  attr.set_type_name(typeName);
  primvar::PrimVar pv;
  pv._ts = ts;
  pv._value = defaultValue;
  attr.set_var(pv);

  xform.props["animAttr"] = Property(attr, /* custom */ false);

  Prim prim(primName, xform);
  stage.root_prims().emplace_back(prim);
  return stage;
}

static void AddCustomAttribute(Xform* xform, const std::string& name,
                               const std::string& typeName,
                               const value::Value& defaultValue,
                               const value::TimeSamples* ts = nullptr) {
  Attribute attr;
  attr.set_type_name(typeName);
  primvar::PrimVar pv;
  pv._value = defaultValue;
  if (ts) {
    pv._ts = *ts;
  }
  attr.set_var(pv);
  xform->props[name] = Property(attr, /* custom */ false);
}

static Stage MakeMultiAttrAnimStage(
    const std::string& primName,
    const std::string& typeName,
    const std::vector<value::TimeSamples>& samples,
    const value::Value& defaultValue) {
  Stage stage;

  Xform xform;
  xform.name = primName;
  xform.spec = Specifier::Def;

  for (size_t i = 0; i < samples.size(); ++i) {
    AddCustomAttribute(&xform, "animAttr_" + std::to_string(i), typeName,
                       defaultValue, &samples[i]);
  }

  Prim prim(primName, xform);
  stage.root_prims().emplace_back(prim);
  return stage;
}

// `compress` toggles the crate's section-level LZ4 compression. The size-based
// dedup checks pass compress=false so block-level deduplication is visible in
// the file size (otherwise LZ4 collapses repeated raw blocks and masks it). The
// compressed-int test passes compress=true to exercise per-array integer
// compression (and the deduplicated-ValueRep compressed-bit path).
static bool WriteStageUSDC(const Stage& stage, const std::string& filename,
                           bool dedup, bool compress = false) {
  std::string err;
  CrateWriter writer(filename);
  CrateWriter::Options opts;
  opts.enable_deduplication = dedup;
  opts.enable_compression = compress;
  writer.SetOptions(opts);
  if (!writer.Open(&err)) return false;
  if (!writer.ConvertStageToSpecs(stage, &err)) return false;
  if (!writer.Finalize(&err)) return false;
  writer.Close();
  return true;
}

// Read back a written file as a Layer and return the named attribute's
// TimeSamples (or nullptr).
static const value::TimeSamples* GetLayerAttrTS(const Layer& layer,
                                                const std::string& primName,
                                                const std::string& propName) {
  const auto& pss = layer.primspecs();
  auto pit = pss.find(primName);
  if (pit == pss.end()) return nullptr;
  const auto& props = pit->second.props();
  auto prit = props.find(propName);
  if (prit == props.end() || !prit->second.is_attribute()) return nullptr;
  return &prit->second.get_attribute().get_var().ts_raw();
}

static bool ReadFieldValueRepsByToken(const std::string& filename,
                                      const std::string& field_name,
                                      std::vector<CrateValueRep>* reps,
                                      std::string* err) {
  std::vector<uint8_t> data;
  if (!tinyusdz::io::ReadWholeFile(&data, err, filename, 0, nullptr)) {
    return false;
  }

  StreamReader sr(data.data(), data.size(), /* swap_endian */ false);
  tinyusdz::crate::CrateReaderConfig config;
  config.numThreads = 1;
  tinyusdz::crate::CrateReader reader(&sr, config);

  if (!reader.ReadBootStrap() || !reader.ReadTOC() || !reader.ReadTokens() ||
      !reader.ReadFields()) {
    if (err) *err = reader.GetError();
    return false;
  }

  for (const auto& field : reader.GetFields()) {
    auto token = reader.GetToken(field.token_index);
    if (token && token->str() == field_name) {
      reps->push_back(field.value_rep);
    }
  }

  return true;
}

static bool HasValueRep(const std::vector<CrateValueRep>& reps,
                        CrateDataTypeId type_id, bool is_inlined) {
  for (const auto& rep : reps) {
    if (rep.GetType() == static_cast<int32_t>(type_id) &&
        rep.IsInlined() == is_inlined) {
      return true;
    }
  }
  return false;
}

template <typename T>
static void CheckVectorSample(const value::TimeSamples& ts, size_t sample_idx,
                              const std::vector<T>& expected) {
  std::vector<T> got;
  bool blocked = false;
  TEST_CHECK(ts.get_vector_at<T>(sample_idx, &got, &blocked));
  TEST_CHECK(!blocked);
  TEST_CHECK(got.size() == expected.size());
  if (got.size() != expected.size()) {
    TEST_MSG("sample %zu type_id=%u array_count=%zu data_offsets=%zu data=%zu "
             "got=%zu expected=%zu",
             sample_idx, ts.type_id(), ts.get_array_count(sample_idx),
             ts.get_data_offsets().size(), ts.get_data().size(), got.size(),
             expected.size());
  }
  if (got.size() == expected.size()) {
    for (size_t i = 0; i < got.size(); i++) {
      TEST_CHECK(got[i] == expected[i]);
    }
  }
}

static void CheckTexcoord2fVectorSample(
    const value::TimeSamples& ts, size_t sample_idx,
    const std::vector<value::texcoord2f>& expected) {
  std::vector<value::texcoord2f> got_role;
  bool blocked = false;
  if (ts.get_vector_at<value::texcoord2f>(sample_idx, &got_role, &blocked)) {
    TEST_CHECK(!blocked);
    TEST_CHECK(got_role.size() == expected.size());
    if (got_role.size() != expected.size()) {
      TEST_MSG("sample %zu role type_id=%u array_count=%zu data_offsets=%zu "
               "data=%zu got=%zu expected=%zu",
               sample_idx, ts.type_id(), ts.get_array_count(sample_idx),
               ts.get_data_offsets().size(), ts.get_data().size(),
               got_role.size(), expected.size());
    }
    if (got_role.size() == expected.size()) {
      for (size_t i = 0; i < got_role.size(); i++) {
        TEST_CHECK(got_role[i][0] == expected[i][0]);
        TEST_CHECK(got_role[i][1] == expected[i][1]);
      }
    }
    return;
  }

  std::vector<value::float2> got_base;
  TEST_CHECK(ts.get_vector_at<value::float2>(sample_idx, &got_base, &blocked));
  TEST_CHECK(!blocked);
  TEST_CHECK(got_base.size() == expected.size());
  if (got_base.size() != expected.size()) {
    TEST_MSG("sample %zu base type_id=%u array_count=%zu data_offsets=%zu "
             "data=%zu got=%zu expected=%zu",
             sample_idx, ts.type_id(), ts.get_array_count(sample_idx),
             ts.get_data_offsets().size(), ts.get_data().size(),
             got_base.size(), expected.size());
  }
  if (got_base.size() == expected.size()) {
    for (size_t i = 0; i < got_base.size(); i++) {
      TEST_CHECK(got_base[i][0] == expected[i][0]);
      TEST_CHECK(got_base[i][1] == expected[i][1]);
    }
  }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

// Test 1: Basic deduplication - repeated float arrays
void dedup_float_array_test(void) {
  value::TimeSamples ts;
  std::vector<float> array1 = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};
  std::vector<float> array2 = {6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
  for (int frame = 1; frame <= 100; frame++) {
    ts.add_sample(static_cast<double>(frame),
                  value::Value(frame <= 50 ? array1 : array2));
  }
  Stage stage = MakeAnimStage("DedupTest", "float[]", ts, value::Value(array1));

  std::string f_dedup = "/tmp/test_dedup_enabled.usdc";
  TEST_CHECK(WriteStageUSDC(stage, f_dedup, true));
  // Note: identical raw (uncompressed) float[] blocks are already collapsed by
  // the lower-level value-data packer, so file size does not isolate timesample
  // dedup here (see int/matrix tests for the size-visible cases). Verify the
  // deduplicated file loads and preserves the sample count.
  Layer layer;
  std::string warn, err;
  TEST_CHECK(tinyusdz::LoadLayerFromFile(f_dedup, &layer, &warn, &err));
  const value::TimeSamples* rts = GetLayerAttrTS(layer, "DedupTest", "animAttr");
  TEST_CHECK(rts != nullptr);
  if (rts) TEST_CHECK(rts->size() == 100);
}

// Test 2: Double array deduplication (all-same array)
void dedup_double_array_test(void) {
  value::TimeSamples ts;
  std::vector<double> constant_array = {1.5, 2.5, 3.5, 4.5};
  for (int frame = 1; frame <= 50; frame++) {
    ts.add_sample(static_cast<double>(frame), value::Value(constant_array));
  }
  Stage stage = MakeAnimStage("DedupDoubleTest", "double[]", ts,
                              value::Value(constant_array));

  std::string filename = "/tmp/test_dedup_double.usdc";
  TEST_CHECK(WriteStageUSDC(stage, filename, true));

  Layer layer;
  std::string warn, err;
  TEST_CHECK(tinyusdz::LoadLayerFromFile(filename, &layer, &warn, &err));
  const value::TimeSamples* rts =
      GetLayerAttrTS(layer, "DedupDoubleTest", "animAttr");
  TEST_CHECK(rts != nullptr);
  if (rts) TEST_CHECK(rts->size() == 50);
}

// Test 3: Int array deduplication
void dedup_int_array_test(void) {
  value::TimeSamples ts;
  std::vector<int32_t> array1 = {10, 20, 30};
  std::vector<int32_t> array2 = {40, 50, 60};
  std::vector<int32_t> array3 = {70, 80, 90};
  for (int frame = 1; frame <= 30; frame++) {
    int pattern = ((frame - 1) / 2) % 3;
    const std::vector<int32_t>& a =
        (pattern == 0) ? array1 : (pattern == 1) ? array2 : array3;
    ts.add_sample(static_cast<double>(frame), value::Value(a));
  }
  Stage stage = MakeAnimStage("DedupIntTest", "int[]", ts, value::Value(array1));

  std::string f_dedup = "/tmp/test_dedup_int_enabled.usdc";
  std::string f_no = "/tmp/test_dedup_int_disabled.usdc";
  TEST_CHECK(WriteStageUSDC(stage, f_dedup, true));
  TEST_CHECK(WriteStageUSDC(stage, f_no, false));
  TEST_CHECK(GetFileSize(f_dedup) < GetFileSize(f_no));
}

// Test 4: No deduplication opportunity (all unique arrays)
void dedup_unique_arrays_test(void) {
  value::TimeSamples ts;
  for (int frame = 1; frame <= 20; frame++) {
    std::vector<float> unique_array;
    for (int i = 0; i < 5; i++) {
      unique_array.push_back(static_cast<float>(frame * 10 + i));
    }
    ts.add_sample(static_cast<double>(frame), value::Value(unique_array));
  }
  std::vector<float> default_arr = {0.0f};
  Stage stage =
      MakeAnimStage("UniqueArraysTest", "float[]", ts, value::Value(default_arr));

  std::string f_dedup = "/tmp/test_unique_dedup.usdc";
  std::string f_no = "/tmp/test_unique_no_dedup.usdc";
  TEST_CHECK(WriteStageUSDC(stage, f_dedup, true));
  TEST_CHECK(WriteStageUSDC(stage, f_no, false));

  size_t size_dedup = GetFileSize(f_dedup);
  size_t size_no_dedup = GetFileSize(f_no);
  // All arrays unique -> sizes should be nearly identical.
  double ratio = double(size_dedup) / double(size_no_dedup);
  TEST_MSG("unique-array dedup/no-dedup ratio: %.3f", ratio);
  TEST_CHECK(ratio > 0.95 && ratio < 1.05);
}

// Test 5: String array deduplication
void dedup_string_array_test(void) {
  value::TimeSamples ts;
  std::vector<std::string> repeated_array = {"hello", "world", "usd"};
  std::vector<std::string> different_array = {"foo", "bar", "baz"};
  for (int frame = 1; frame <= 50; frame++) {
    ts.add_sample(static_cast<double>(frame),
                  value::Value(frame <= 30 ? repeated_array : different_array));
  }
  Stage stage = MakeAnimStage("StringArrayTest", "string[]", ts,
                              value::Value(repeated_array));

  std::string f_dedup = "/tmp/test_dedup_string_enabled.usdc";
  TEST_CHECK(WriteStageUSDC(stage, f_dedup, true));
  // Note: identical raw (uncompressed) array blocks are already collapsed by the
  // lower-level value-data packer, so file size does not isolate timesample
  // dedup for string[]; verify the result loads and preserves the sample count.
  Layer layer;
  std::string warn, err;
  TEST_CHECK(tinyusdz::LoadLayerFromFile(f_dedup, &layer, &warn, &err));
  const value::TimeSamples* rts =
      GetLayerAttrTS(layer, "StringArrayTest", "animAttr");
  TEST_CHECK(rts != nullptr);
  if (rts) TEST_CHECK(rts->size() == 50);
}

// Test 6: Matrix4d scalar deduplication (transform animations)
void dedup_matrix4d_test(void) {
  value::TimeSamples ts;
  value::matrix4d identity_matrix;
  value::matrix4d transform_matrix;
  for (int i = 0; i < 4; i++) {
    for (int j = 0; j < 4; j++) {
      identity_matrix.m[i][j] = (i == j) ? 1.0 : 0.0;
      transform_matrix.m[i][j] = (i == j) ? 2.0 : 0.5;
    }
  }
  for (int frame = 1; frame <= 50; frame++) {
    ts.add_sample(static_cast<double>(frame),
                  value::Value(frame <= 40 ? identity_matrix : transform_matrix));
  }
  Stage stage = MakeAnimStage("MatrixTest", "matrix4d", ts,
                              value::Value(identity_matrix));

  std::string f_dedup = "/tmp/test_dedup_matrix_enabled.usdc";
  std::string f_no = "/tmp/test_dedup_matrix_disabled.usdc";
  TEST_CHECK(WriteStageUSDC(stage, f_dedup, true));
  TEST_CHECK(WriteStageUSDC(stage, f_no, false));

  size_t size_dedup = GetFileSize(f_dedup);
  size_t size_no_dedup = GetFileSize(f_no);
  TEST_CHECK(size_dedup < size_no_dedup);
  if (size_no_dedup > 0) {
    double savings = 100.0 * (1.0 - double(size_dedup) / double(size_no_dedup));
    TEST_MSG("matrix4d dedup savings: %.1f%%", savings);
    TEST_CHECK(savings > 20.0);
  }
}

// Test 7: Global cross-attribute TimeSamples value deduplication.
// Each attribute has unique values across time, so per-attribute dedup has no
// opportunity. The same curve repeated across attributes should share the
// writer-wide ValueReps.
void dedup_cross_attribute_timesamples_test(void) {
  const size_t k_attrs = 8;
  const int k_frames = 48;
  std::vector<value::TimeSamples> samples(k_attrs);

  for (size_t a = 0; a < k_attrs; ++a) {
    for (int f = 0; f < k_frames; ++f) {
      value::double3 v = {{double(f) * 1.25, double(f * f) + 0.5,
                           double(f) * -3.0}};
      samples[a].add_sample(double(f), value::Value(v));
    }
  }

  value::double3 default_value = {{0.0, 0.0, 0.0}};
  Stage stage = MakeMultiAttrAnimStage("CrossAttrTS", "double3", samples,
                                       value::Value(default_value));

  const std::string f_dedup = "/tmp/test_cross_attr_ts_dedup.usdc";
  const std::string f_no = "/tmp/test_cross_attr_ts_no_dedup.usdc";
  TEST_CHECK(WriteStageUSDC(stage, f_dedup, true));
  TEST_CHECK(WriteStageUSDC(stage, f_no, false));

  const size_t size_dedup = GetFileSize(f_dedup);
  const size_t size_no = GetFileSize(f_no);
  TEST_MSG("cross-attr timesample dedup %zu vs no-dedup %zu", size_dedup,
           size_no);
  TEST_CHECK(size_dedup < size_no);

  Layer layer;
  std::string warn, err;
  TEST_CHECK(tinyusdz::LoadLayerFromFile(f_dedup, &layer, &warn, &err));
  for (size_t a = 0; a < k_attrs; ++a) {
    const value::TimeSamples* rts =
        GetLayerAttrTS(layer, "CrossAttrTS", "animAttr_" + std::to_string(a));
    TEST_CHECK(rts != nullptr);
    if (rts) TEST_CHECK(rts->size() == size_t(k_frames));
  }
}

// Test 8: Global default-value scalar deduplication.
// Covers matrix4d, double3 and scalar double defaults. The scalar-double-only
// half uses a non-float-exact double so it still catches the out-of-line
// primitive scalar path now that OpenUSD-style inline doubles are supported.
void dedup_default_scalar_values_test(void) {
  value::matrix4d mat;
  for (int r = 0; r < 4; ++r) {
    for (int c = 0; c < 4; ++c) {
      mat.m[r][c] = (r == c) ? 1.0 : 0.0;
    }
  }
  value::double3 vec = {{1.25, 2.5, 3.75}};

  {
    Stage stage;
    Xform xform;
    xform.name = "DefaultScalars";
    xform.spec = Specifier::Def;
    for (int i = 0; i < 48; ++i) {
      AddCustomAttribute(&xform, "m_" + std::to_string(i), "matrix4d",
                         value::Value(mat));
      AddCustomAttribute(&xform, "v_" + std::to_string(i), "double3",
                         value::Value(vec));
    }
    Prim prim("DefaultScalars", xform);
    stage.root_prims().emplace_back(prim);

    const std::string f_dedup = "/tmp/test_default_scalar_dedup.usdc";
    const std::string f_no = "/tmp/test_default_scalar_no_dedup.usdc";
    TEST_CHECK(WriteStageUSDC(stage, f_dedup, true));
    TEST_CHECK(WriteStageUSDC(stage, f_no, false));
    const size_t size_dedup = GetFileSize(f_dedup);
    const size_t size_no = GetFileSize(f_no);
    TEST_MSG("default matrix/double3 dedup %zu vs no-dedup %zu", size_dedup,
             size_no);
    TEST_CHECK(size_dedup < size_no);
  }

  {
    Stage stage;
    Xform xform;
    xform.name = "DefaultDoubles";
    xform.spec = Specifier::Def;
    for (int i = 0; i < 1024; ++i) {
      AddCustomAttribute(&xform, "d_" + std::to_string(i), "double",
                         value::Value(0.1));
    }
    Prim prim("DefaultDoubles", xform);
    stage.root_prims().emplace_back(prim);

    const std::string f_dedup = "/tmp/test_default_double_dedup.usdc";
    const std::string f_no = "/tmp/test_default_double_no_dedup.usdc";
    TEST_CHECK(WriteStageUSDC(stage, f_dedup, true));
    TEST_CHECK(WriteStageUSDC(stage, f_no, false));
    const size_t size_dedup = GetFileSize(f_dedup);
    const size_t size_no = GetFileSize(f_no);
    TEST_MSG("default double dedup %zu vs no-dedup %zu", size_dedup,
             size_no);
    TEST_CHECK(size_dedup < size_no);
  }
}

void inline_openusd_value_coverage_test(void) {
  value::matrix2d m2 = {};
  m2.m[0][0] = -1.0;
  m2.m[1][1] = 2.0;

  value::matrix3d m3 = {};
  m3.m[0][0] = 1.0;
  m3.m[1][1] = -2.0;
  m3.m[2][2] = 3.0;

  value::matrix4d m4 = {};
  m4.m[0][0] = 1.0;
  m4.m[1][1] = 2.0;
  m4.m[2][2] = 3.0;
  m4.m[3][3] = 4.0;

  value::matrix4d m4_noninline = m4;
  m4_noninline.m[0][1] = 0.5;

  value::half4 h4 = {{
      value::float_to_half_full(1.0f),
      value::float_to_half_full(-2.0f),
      value::float_to_half_full(3.0f),
      value::float_to_half_full(4.0f),
  }};
  value::double4 d4 = {{1.0, 2.0, -3.0, 4.0}};
  int64_t i64_inline = int64_t((std::numeric_limits<int32_t>::min)());
  int64_t i64_noninline = int64_t((std::numeric_limits<int32_t>::min)()) - 1;
  uint64_t u64_inline = uint64_t((std::numeric_limits<uint32_t>::max)());
  uint64_t u64_noninline = uint64_t((std::numeric_limits<uint32_t>::max)()) + 1;

  Stage stage;
  Xform xform;
  xform.name = "InlineCoverage";
  xform.spec = Specifier::Def;

  AddCustomAttribute(&xform, "m2", "matrix2d", value::Value(m2));
  AddCustomAttribute(&xform, "m3", "matrix3d", value::Value(m3));
  AddCustomAttribute(&xform, "m4", "matrix4d", value::Value(m4));
  AddCustomAttribute(&xform, "m4_noninline", "matrix4d",
                     value::Value(m4_noninline));
  AddCustomAttribute(&xform, "h4", "half4", value::Value(h4));
  AddCustomAttribute(&xform, "d4", "double4", value::Value(d4));
  AddCustomAttribute(&xform, "i64_inline", "int64",
                     value::Value(i64_inline));
  AddCustomAttribute(&xform, "i64_noninline", "int64",
                     value::Value(i64_noninline));
  AddCustomAttribute(&xform, "u64_inline", "uint64",
                     value::Value(u64_inline));
  AddCustomAttribute(&xform, "u64_noninline", "uint64",
                     value::Value(u64_noninline));

  Attribute attr_with_custom_data;
  attr_with_custom_data.set_type_name("double");
  primvar::PrimVar pv;
  pv._value = value::Value(1.0);
  attr_with_custom_data.set_var(pv);
  attr_with_custom_data.metas().set_customData(Dictionary());
  xform.props["emptyCustomData"] =
      Property(attr_with_custom_data, /* custom */ false);

  Prim prim("InlineCoverage", xform);
  stage.root_prims().emplace_back(prim);

  const std::string filename = "/tmp/test_inline_openusd_coverage.usdc";
  TEST_CHECK(WriteStageUSDC(stage, filename, true));

  std::vector<CrateValueRep> default_reps;
  std::string err;
  bool ret = ReadFieldValueRepsByToken(filename, "default", &default_reps, &err);
  TEST_CHECK(ret);
  if (!ret) {
    TEST_MSG("failed to read default ValueReps: %s", err.c_str());
    return;
  }

  TEST_CHECK(HasValueRep(default_reps, CrateDataTypeId::CRATE_DATA_TYPE_MATRIX2D,
                         true));
  TEST_CHECK(HasValueRep(default_reps, CrateDataTypeId::CRATE_DATA_TYPE_MATRIX3D,
                         true));
  TEST_CHECK(HasValueRep(default_reps, CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D,
                         true));
  TEST_CHECK(HasValueRep(default_reps, CrateDataTypeId::CRATE_DATA_TYPE_MATRIX4D,
                         false));
  TEST_CHECK(HasValueRep(default_reps, CrateDataTypeId::CRATE_DATA_TYPE_VEC4H,
                         true));
  TEST_CHECK(HasValueRep(default_reps, CrateDataTypeId::CRATE_DATA_TYPE_VEC4D,
                         true));
  TEST_CHECK(HasValueRep(default_reps, CrateDataTypeId::CRATE_DATA_TYPE_INT64,
                         true));
  TEST_CHECK(HasValueRep(default_reps, CrateDataTypeId::CRATE_DATA_TYPE_INT64,
                         false));
  TEST_CHECK(HasValueRep(default_reps, CrateDataTypeId::CRATE_DATA_TYPE_UINT64,
                         true));
  TEST_CHECK(HasValueRep(default_reps, CrateDataTypeId::CRATE_DATA_TYPE_UINT64,
                         false));

  std::vector<CrateValueRep> custom_data_reps;
  ret = ReadFieldValueRepsByToken(filename, "customData", &custom_data_reps,
                                  &err);
  TEST_CHECK(ret);
  if (!ret) {
    TEST_MSG("failed to read customData ValueReps: %s", err.c_str());
    return;
  }
  TEST_CHECK(HasValueRep(custom_data_reps,
                         CrateDataTypeId::CRATE_DATA_TYPE_DICTIONARY,
                         true));

  Layer layer;
  std::string warn;
  err.clear();
  TEST_CHECK(tinyusdz::LoadLayerFromFile(filename, &layer, &warn, &err));
  if (!err.empty()) {
    TEST_MSG("LoadLayerFromFile error: %s", err.c_str());
  }
}

// Test 9: Global dedup of TimeSamples time arrays.
// Values are intentionally unique per attribute and frame; the shared time-code
// arrays are the primary dedup opportunity.
void dedup_shared_times_arrays_test(void) {
  const size_t k_attrs = 8;
  const int k_frames = 128;
  std::vector<value::TimeSamples> samples(k_attrs);

  for (size_t a = 0; a < k_attrs; ++a) {
    for (int f = 0; f < k_frames; ++f) {
      value::double3 v = {{double(a) * 10000.0 + double(f),
                           double(a) * -17.0 + double(f) * 0.25,
                           double(a + 1) * double(f + 3)}};
      samples[a].add_sample(double(f) * 0.5, value::Value(v));
    }
  }

  value::double3 default_value = {{-1.0, -2.0, -3.0}};
  Stage stage = MakeMultiAttrAnimStage("SharedTimes", "double3", samples,
                                       value::Value(default_value));

  const std::string f_dedup = "/tmp/test_shared_times_dedup.usdc";
  const std::string f_no = "/tmp/test_shared_times_no_dedup.usdc";
  TEST_CHECK(WriteStageUSDC(stage, f_dedup, true));
  TEST_CHECK(WriteStageUSDC(stage, f_no, false));
  const size_t size_dedup = GetFileSize(f_dedup);
  const size_t size_no = GetFileSize(f_no);
  TEST_MSG("shared times-array dedup %zu vs no-dedup %zu", size_dedup,
           size_no);
  TEST_CHECK(size_dedup < size_no);
}

// Test 10: Dedup retention respects the memory budget without failing writes.
void dedup_low_memory_budget_test(void) {
  std::vector<float> large_array(32768);
  for (size_t i = 0; i < large_array.size(); ++i) {
    large_array[i] = float(i) * 0.125f;
  }

  value::TimeSamples ts;
  for (int f = 0; f < 4; ++f) {
    ts.add_sample(double(f), value::Value(large_array));
  }
  Stage stage = MakeAnimStage("LowMemDedup", "float[]", ts,
                              value::Value(large_array));

  const std::string filename = "/tmp/test_low_memory_dedup.usdc";
  std::string err;
  CrateWriter writer(filename);
  CrateWriter::Options opts;
  opts.enable_deduplication = true;
  opts.enable_compression = false;
  opts.max_memory_bytes = 64 * 1024;
  writer.SetOptions(opts);
  TEST_CHECK(writer.Open(&err));
  TEST_CHECK(writer.ConvertStageToSpecs(stage, &err));
  TEST_CHECK(writer.Finalize(&err));
  writer.Close();

  Layer layer;
  std::string warn;
  TEST_CHECK(tinyusdz::LoadLayerFromFile(filename, &layer, &warn, &err));
  const value::TimeSamples* rts =
      GetLayerAttrTS(layer, "LowMemDedup", "animAttr");
  TEST_CHECK(rts != nullptr);
  if (rts) TEST_CHECK(rts->size() == 4);
}

// Role-type array (texcoord2f[]) deduplication.
// Role types normalize to their base type (float2) before write; this verifies
// (a) repeated role arrays dedup, (b) two distinct-but-equal arrays also dedup
// (hash path, not just read-side offset sharing), and (c) values read back
// correctly.
void dedup_role_array_test(void) {
  // Same array repeated, plus a fresh-but-equal copy, plus a distinct array.
  const std::vector<value::texcoord2f> uvA_expected = {
      {0.0f, 0.0f}, {1.0f, 0.0f}, {1.0f, 1.0f}};
  const std::vector<value::texcoord2f> uvB_expected = {
      {0.5f, 0.5f}, {0.25f, 0.75f}};

  value::TimeSamples ts;
  std::vector<std::vector<value::texcoord2f>> expected;
  for (int f = 0; f < 64; f++) {  // 64 identical samples
    ts.add_sample(double(f), value::Value(std::vector<value::texcoord2f>(
                               uvA_expected)));
    expected.push_back(uvA_expected);
  }
  ts.add_sample(64.0, value::Value(std::vector<value::texcoord2f>(
                         uvA_expected)));  // equal-but-distinct -> hash dedup
  expected.push_back(uvA_expected);
  ts.add_sample(65.0, value::Value(std::vector<value::texcoord2f>(
                         uvB_expected)));  // genuinely different
  expected.push_back(uvB_expected);

  Stage stage =
      MakeAnimStage("RoleArrayTest", "texCoord2f[]", ts,
                    value::Value(std::vector<value::texcoord2f>(
                        uvA_expected)));

  std::string f_dedup = "/tmp/test_dedup_role_enabled.usdc";
  std::string f_no = "/tmp/test_dedup_role_disabled.usdc";
  TEST_CHECK(WriteStageUSDC(stage, f_dedup, true));
  TEST_CHECK(WriteStageUSDC(stage, f_no, false));

  // 66 samples, only 2 unique values -> dedup must be much smaller.
  size_t size_dedup = GetFileSize(f_dedup);
  size_t size_no_dedup = GetFileSize(f_no);
  TEST_MSG("role-array dedup %zu vs no-dedup %zu", size_dedup, size_no_dedup);
  TEST_CHECK(size_dedup < size_no_dedup);

  // Read back: the deduplicated file must load and preserve every sample value.
  Layer layer;
  std::string warn, err;
  TEST_CHECK(tinyusdz::LoadLayerFromFile(f_dedup, &layer, &warn, &err));
  const value::TimeSamples* rts =
      GetLayerAttrTS(layer, "RoleArrayTest", "animAttr");
  TEST_CHECK(rts != nullptr);
  if (rts) {
    TEST_CHECK(rts->size() == expected.size());
    if (rts->size() == expected.size()) {
      for (size_t i = 0; i < expected.size(); i++) {
        CheckTexcoord2fVectorSample(*rts, i, expected[i]);
      }
    }
  }
}

// Test 8: Compressed integer array deduplication.
// Large int32[] arrays are written with integer compression. With dedup on, the
// many repeated frames collapse to a single compressed block (so the file is
// much smaller) and the deduplicated ValueRep reuses the original verbatim,
// preserving its compressed bit. This verifies dedup fires for compressed arrays
// and the result loads cleanly.
void dedup_compressed_int_array_test(void) {
  const int kN = 4096;
  std::vector<int32_t> arrA(kN), arrB(kN);
  for (int i = 0; i < kN; i++) {
    arrA[i] = i * 7 - 3;        // smoothly varying -> compresses well
    arrB[i] = (i % 16) - 8;     // small-range -> compresses well
  }
  const std::vector<int32_t> arrA_expected = arrA;
  const std::vector<int32_t> arrB_expected = arrB;

  value::TimeSamples ts;
  std::vector<const std::vector<int32_t>*> expected;
  const int kFrames = 32;
  for (int f = 0; f < kFrames; f++) {
    const std::vector<int32_t>& a =
        (f < kFrames / 2) ? arrA_expected : arrB_expected;
    ts.add_sample(double(f), value::Value(std::vector<int32_t>(a)));
    expected.push_back(&a);
  }
  Stage stage =
      MakeAnimStage("CompIntTest", "int[]", ts,
                    value::Value(std::vector<int32_t>(arrA_expected)));

  std::string f_dedup = "/tmp/test_dedup_compint_enabled.usdc";
  std::string f_no = "/tmp/test_dedup_compint_disabled.usdc";
  TEST_CHECK(WriteStageUSDC(stage, f_dedup, true, /*compress=*/true));
  TEST_CHECK(WriteStageUSDC(stage, f_no, false, /*compress=*/true));

  // Only 2 unique 16 KB arrays across 32 frames -> dedup must be much smaller.
  size_t size_dedup = GetFileSize(f_dedup);
  size_t size_no_dedup = GetFileSize(f_no);
  TEST_MSG("compressed-int dedup %zu vs no-dedup %zu", size_dedup, size_no_dedup);
  TEST_CHECK(size_dedup < size_no_dedup);

  // The deduplicated compressed file must load and preserve the sample count.
  Layer layer;
  std::string warn, err;
  TEST_CHECK(tinyusdz::LoadLayerFromFile(f_dedup, &layer, &warn, &err));
  const value::TimeSamples* rts = GetLayerAttrTS(layer, "CompIntTest", "animAttr");
  TEST_CHECK(rts != nullptr);
  if (rts) {
    TEST_CHECK(rts->size() == size_t(kFrames));
    if (rts->size() == size_t(kFrames)) {
      for (size_t i = 0; i < expected.size(); i++) {
        CheckVectorSample<int32_t>(*rts, i, *expected[i]);
      }
    }
  }

  Layer layer_no;
  warn.clear();
  err.clear();
  TEST_CHECK(tinyusdz::LoadLayerFromFile(f_no, &layer_no, &warn, &err));
  const value::TimeSamples* rts_no =
      GetLayerAttrTS(layer_no, "CompIntTest", "animAttr");
  TEST_CHECK(rts_no != nullptr);
  if (rts_no) {
    TEST_CHECK(rts_no->size() == size_t(kFrames));
    if (rts_no->size() == size_t(kFrames)) {
      for (size_t i = 0; i < expected.size(); i++) {
        CheckVectorSample<int32_t>(*rts_no, i, *expected[i]);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Regression: every array-valued timesample type must deduplicate
// identical-across-frames samples. A type the writer's dedup descriptor does
// not recognize is silently re-expanded to N full copies on write - the
// std::vector<bool> inflation bug (bit-packed, no contiguous .data(), so it was
// excluded from ComputeValueDedupDescriptor; outpost_19's animated bool[]
// visibility masks blew an 78 MB USDC up to 384 MB on roundtrip).
//
// For each array type we write K *identical* arrays and K *distinct* arrays,
// both with dedup ON, and require the identical file to be far smaller than the
// distinct one. A missing/broken descriptor makes identical ~= distinct.
// ---------------------------------------------------------------------------

static std::string SanitizeForPath(const char* s) {
  std::string out;
  for (const char* p = s; *p; ++p) {
    out += std::isalnum(static_cast<unsigned char>(*p)) ? *p : '_';
  }
  return out;
}

template <typename T>
static void CheckTSArrayNoInflation(
    const char* type_name, int k_frames,
    const std::function<std::vector<T>(int /*frame*/)>& make_for_frame) {
  const std::vector<T> seed = make_for_frame(0);

  value::TimeSamples ts_id, ts_uq;
  for (int f = 0; f < k_frames; ++f) {
    ts_id.add_sample(double(f), value::Value(seed));               // identical
    ts_uq.add_sample(double(f), value::Value(make_for_frame(f)));  // distinct
  }

  Stage s_id = MakeAnimStage("TSInfl", type_name, ts_id, value::Value(seed));
  Stage s_uq = MakeAnimStage("TSInfl", type_name, ts_uq, value::Value(seed));

  const std::string base = "/tmp/tsinfl_" + SanitizeForPath(type_name);
  const std::string f_id = base + "_id.usdc";
  const std::string f_uq = base + "_uq.usdc";
  TEST_CHECK_(WriteStageUSDC(s_id, f_id, /*dedup*/ true),
              "%s: identical-array write failed", type_name);
  TEST_CHECK_(WriteStageUSDC(s_uq, f_uq, /*dedup*/ true),
              "%s: distinct-array write failed", type_name);

  const size_t s_identical = GetFileSize(f_id);
  const size_t s_distinct = GetFileSize(f_uq);
  TEST_MSG("%-11s identical=%zu distinct=%zu ratio=%.3f", type_name, s_identical,
           s_distinct, s_distinct ? double(s_identical) / double(s_distinct) : 0.0);
  TEST_CHECK_(s_identical > 0 && s_distinct > 0, "%s: empty USDC output",
              type_name);
  // K identical arrays must collapse well below K distinct ones. The bool[] bug
  // produced identical ~= distinct (dedup skipped -> N full copies written).
  TEST_CHECK_(s_identical * 2 < s_distinct,
              "%s: identical-array timesamples did NOT deduplicate "
              "(identical=%zu, distinct=%zu) -> data inflation",
              type_name, s_identical, s_distinct);
}

void timesample_array_dedup_no_inflation_test(void) {
  const int K = 64;
  const size_t N = 256;  // > K so the bool flip-bit generator yields K distinct

  // --- scalar-element arrays ---
  CheckTSArrayNoInflation<bool>("bool[]", K, [&](int f) {
    std::vector<bool> v(N);
    for (size_t i = 0; i < N; ++i) v[i] = ((i % 3 == 0) != (i == size_t(f)));
    return v;
  });
  CheckTSArrayNoInflation<uint8_t>("uchar[]", K, [&](int f) {
    std::vector<uint8_t> v(N);
    for (size_t i = 0; i < N; ++i) v[i] = uint8_t((i * 7 + f) & 0xFF);
    return v;
  });
  CheckTSArrayNoInflation<int32_t>("int[]", K, [&](int f) {
    std::vector<int32_t> v(N);
    for (size_t i = 0; i < N; ++i) v[i] = int32_t(i * 7 + f * 100003);
    return v;
  });
  CheckTSArrayNoInflation<uint32_t>("uint[]", K, [&](int f) {
    std::vector<uint32_t> v(N);
    for (size_t i = 0; i < N; ++i) v[i] = uint32_t(i * 7 + f * 100003);
    return v;
  });
  CheckTSArrayNoInflation<int64_t>("int64[]", K, [&](int f) {
    std::vector<int64_t> v(N);
    for (size_t i = 0; i < N; ++i) v[i] = int64_t(i) * 7 + int64_t(f) * 1000003;
    return v;
  });
  CheckTSArrayNoInflation<uint64_t>("uint64[]", K, [&](int f) {
    std::vector<uint64_t> v(N);
    for (size_t i = 0; i < N; ++i) v[i] = uint64_t(i) * 7 + uint64_t(f) * 1000003;
    return v;
  });
  CheckTSArrayNoInflation<value::half>("half[]", K, [&](int f) {
    std::vector<value::half> v(N);
    for (size_t i = 0; i < N; ++i)
      v[i] = value::float_to_half_full(float(i) * 0.5f + float(f));
    return v;
  });
  CheckTSArrayNoInflation<float>("float[]", K, [&](int f) {
    std::vector<float> v(N);
    for (size_t i = 0; i < N; ++i) v[i] = float(i) * 0.5f + float(f);
    return v;
  });
  CheckTSArrayNoInflation<double>("double[]", K, [&](int f) {
    std::vector<double> v(N);
    for (size_t i = 0; i < N; ++i) v[i] = double(i) * 0.5 + double(f);
    return v;
  });

  // --- multi-component (std::array) arrays ---
  CheckTSArrayNoInflation<value::float2>("float2[]", K, [&](int f) {
    std::vector<value::float2> v(N);
    for (size_t i = 0; i < N; ++i) v[i] = {{float(i) + f, float(i) * 2 + f}};
    return v;
  });
  CheckTSArrayNoInflation<value::float3>("float3[]", K, [&](int f) {
    std::vector<value::float3> v(N);
    for (size_t i = 0; i < N; ++i)
      v[i] = {{float(i) + f, float(i) * 2 + f, float(i) * 3 + f}};
    return v;
  });
  CheckTSArrayNoInflation<value::float4>("float4[]", K, [&](int f) {
    std::vector<value::float4> v(N);
    for (size_t i = 0; i < N; ++i)
      v[i] = {{float(i) + f, float(i) + 1, float(i) + 2, float(i) * 4 + f}};
    return v;
  });
  CheckTSArrayNoInflation<value::double2>("double2[]", K, [&](int f) {
    std::vector<value::double2> v(N);
    for (size_t i = 0; i < N; ++i) v[i] = {{double(i) + f, double(i) * 2 + f}};
    return v;
  });
  CheckTSArrayNoInflation<value::double3>("double3[]", K, [&](int f) {
    std::vector<value::double3> v(N);
    for (size_t i = 0; i < N; ++i)
      v[i] = {{double(i) + f, double(i) * 2 + f, double(i) * 3 + f}};
    return v;
  });
  CheckTSArrayNoInflation<value::double4>("double4[]", K, [&](int f) {
    std::vector<value::double4> v(N);
    for (size_t i = 0; i < N; ++i)
      v[i] = {{double(i) + f, double(i) + 1, double(i) + 2, double(i) * 4 + f}};
    return v;
  });
  CheckTSArrayNoInflation<value::int2>("int2[]", K, [&](int f) {
    std::vector<value::int2> v(N);
    for (size_t i = 0; i < N; ++i) v[i] = {{int32_t(i + f), int32_t(i * 2 + f)}};
    return v;
  });
  CheckTSArrayNoInflation<value::int3>("int3[]", K, [&](int f) {
    std::vector<value::int3> v(N);
    for (size_t i = 0; i < N; ++i)
      v[i] = {{int32_t(i + f), int32_t(i * 2 + f), int32_t(i * 3 + f)}};
    return v;
  });
  CheckTSArrayNoInflation<value::int4>("int4[]", K, [&](int f) {
    std::vector<value::int4> v(N);
    for (size_t i = 0; i < N; ++i)
      v[i] = {{int32_t(i + f), int32_t(i + 1), int32_t(i + 2), int32_t(i * 4 + f)}};
    return v;
  });
  CheckTSArrayNoInflation<value::half3>("half3[]", K, [&](int f) {
    std::vector<value::half3> v(N);
    for (size_t i = 0; i < N; ++i) {
      v[i] = {{value::float_to_half_full(float(i) + f),
               value::float_to_half_full(float(i) * 2 + f),
               value::float_to_half_full(float(i) * 3 + f)}};
    }
    return v;
  });

  // --- role types: must normalize to their base (float3 / float2) before the
  // dedup descriptor, so they dedup via the base-type path ---
  CheckTSArrayNoInflation<value::point3f>("point3f[]", K, [&](int f) {
    std::vector<value::point3f> v(N);
    for (size_t i = 0; i < N; ++i)
      v[i] = {float(i) + f, float(i) * 2 + f, float(i) * 3 + f};
    return v;
  });
  CheckTSArrayNoInflation<value::texcoord2f>("texcoord2f[]", K, [&](int f) {
    std::vector<value::texcoord2f> v(N);
    for (size_t i = 0; i < N; ++i) v[i] = {float(i) + f, float(i) * 2 + f};
    return v;
  });

  // --- matrices ---
  CheckTSArrayNoInflation<value::matrix2d>("matrix2d[]", K, [&](int f) {
    std::vector<value::matrix2d> v(N);
    for (size_t i = 0; i < N; ++i)
      for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 2; ++c) v[i].m[r][c] = double(i * 4 + r * 2 + c) + f;
    return v;
  });
  CheckTSArrayNoInflation<value::matrix3d>("matrix3d[]", K, [&](int f) {
    std::vector<value::matrix3d> v(N);
    for (size_t i = 0; i < N; ++i)
      for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) v[i].m[r][c] = double(i * 9 + r * 3 + c) + f;
    return v;
  });
  CheckTSArrayNoInflation<value::matrix4d>("matrix4d[]", K, [&](int f) {
    std::vector<value::matrix4d> v(N);
    for (size_t i = 0; i < N; ++i)
      for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) v[i].m[r][c] = double(i * 16 + r * 4 + c) + f;
    return v;
  });

  // --- quaternions ---
  CheckTSArrayNoInflation<value::quatf>("quatf[]", K, [&](int f) {
    std::vector<value::quatf> v(N);
    for (size_t i = 0; i < N; ++i) {
      v[i].imag = {{float(i) + f, float(i) + 1, float(i) + 2}};
      v[i].real = float(i) * 0.5f + f;
    }
    return v;
  });
  CheckTSArrayNoInflation<value::quatd>("quatd[]", K, [&](int f) {
    std::vector<value::quatd> v(N);
    for (size_t i = 0; i < N; ++i) {
      v[i].imag = {{double(i) + f, double(i) + 1, double(i) + 2}};
      v[i].real = double(i) * 0.5 + f;
    }
    return v;
  });
  CheckTSArrayNoInflation<value::quath>("quath[]", K, [&](int f) {
    std::vector<value::quath> v(N);
    for (size_t i = 0; i < N; ++i) {
      v[i].imag = {{value::float_to_half_full(float(i) + f),
                    value::float_to_half_full(float(i) + 1),
                    value::float_to_half_full(float(i) + 2)}};
      v[i].real = value::float_to_half_full(float(i) * 0.5f + f);
    }
    return v;
  });

  // --- string / token (also collapse at the value-data packer; identical must
  // still land far below distinct) ---
  CheckTSArrayNoInflation<std::string>("string[]", K, [&](int f) {
    std::vector<std::string> v(N);
    for (size_t i = 0; i < N; ++i)
      v[i] = "s" + std::to_string(i) + "_" + std::to_string(f);
    return v;
  });
  CheckTSArrayNoInflation<value::token>("token[]", K, [&](int f) {
    std::vector<value::token> v(N);
    for (size_t i = 0; i < N; ++i)
      v[i] = value::token("t" + std::to_string(i) + "_" + std::to_string(f));
    return v;
  });
  CheckTSArrayNoInflation<value::AssetPath>("asset[]", K, [&](int f) {
    std::vector<value::AssetPath> v(N);
    for (size_t i = 0; i < N; ++i)
      v[i] = value::AssetPath("a" + std::to_string(i) + "_" + std::to_string(f) +
                              ".usd");
    return v;
  });
}

// End-to-end uchar[] USDC roundtrip: a single attribute carries both a default
// uchar[] value (exercises the general value read path, ReadValue UCHAR) and
// animated uchar[] timesamples (exercises UnpackTimeSampleValue_UCHAR). Verifies
// every byte survives write -> read.
void uchar_roundtrip_test(void) {
  const std::vector<uint8_t> default_arr = {0, 1, 127, 128, 254, 255};

  value::TimeSamples ts;
  std::vector<std::vector<uint8_t>> expected;
  for (int f = 0; f < 8; ++f) {
    std::vector<uint8_t> a(6);
    for (size_t i = 0; i < a.size(); ++i) {
      a[i] = static_cast<uint8_t>((i * 37 + f * 53) & 0xFF);
    }
    expected.push_back(a);  // capture before value::Value(a) takes ownership of a
    ts.add_sample(double(f), value::Value(a));
  }

  Stage stage =
      MakeAnimStage("UCharPrim", "uchar[]", ts, value::Value(default_arr));
  const std::string fn = "/tmp/uchar_roundtrip.usdc";
  TEST_CHECK(WriteStageUSDC(stage, fn, /*dedup*/ true));

  Layer layer;
  std::string warn, err;
  TEST_CHECK(tinyusdz::LoadLayerFromFile(fn, &layer, &warn, &err));

  const auto& pss = layer.primspecs();
  auto pit = pss.find("UCharPrim");
  TEST_CHECK(pit != pss.end());
  if (pit == pss.end()) return;
  auto prit = pit->second.props().find("animAttr");
  TEST_CHECK(prit != pit->second.props().end() && prit->second.is_attribute());
  if (prit == pit->second.props().end() || !prit->second.is_attribute()) return;
  const primvar::PrimVar& pv = prit->second.get_attribute().get_var();

  // (a) default uchar[] value survives the general value read path.
  const value::Value& dv = pv.value_raw();
  const std::vector<uint8_t>* dptr = dv.as<std::vector<uint8_t>>();
  TEST_CHECK(dptr != nullptr);
  if (dptr) TEST_CHECK(*dptr == default_arr);

  // (b) uchar[] timesamples survive UnpackTimeSampleValue_UCHAR.
  const value::TimeSamples& rts = pv.ts_raw();
  TEST_CHECK(rts.size() == expected.size());
  for (size_t i = 0; i < expected.size() && i < rts.size(); ++i) {
    std::vector<uint8_t> got;
    bool blocked = false;
    TEST_CHECK(rts.get_vector_at<uint8_t>(i, &got, &blocked));
    TEST_CHECK(!blocked);
    TEST_CHECK(got == expected[i]);
  }
}
