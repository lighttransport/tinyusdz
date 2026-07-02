// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - generated malformed USDC/crate regressions
//
// These cases are intentionally synthetic. Each buffer is just valid enough to
// reach the targeted reader guard, then must reject cleanly without crashing or
// allocating based on hostile counts.

#include <cassert>
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

#include "next/crate/crate-format.hh"
#include "next/reader/usdc-reader.hh"

using namespace tinyusdz::next;

namespace {

void PutU32(std::vector<uint8_t>* out, uint32_t v) {
  for (int i = 0; i < 4; ++i) out->push_back(uint8_t(v >> (i * 8)));
}

void PutU64(std::vector<uint8_t>* out, uint64_t v) {
  for (int i = 0; i < 8; ++i) out->push_back(uint8_t(v >> (i * 8)));
}

void PatchI64(std::vector<uint8_t>* out, size_t pos, int64_t v) {
  assert(pos + 8 <= out->size());
  uint64_t uv = static_cast<uint64_t>(v);
  for (int i = 0; i < 8; ++i) (*out)[pos + size_t(i)] = uint8_t(uv >> (i * 8));
}

std::vector<uint8_t> Bootstrap(int64_t toc_offset) {
  std::vector<uint8_t> out(kCrateBootstrapSize, 0);
  std::memcpy(out.data(), kCrateMagic, 8);
  out[8] = 0;
  out[9] = 8;
  out[10] = 0;
  PatchI64(&out, 16, toc_offset);
  return out;
}

struct Section {
  std::string name;
  std::vector<uint8_t> payload;
  bool override_bounds = false;
  int64_t start = 0;
  int64_t size = 0;
};

class CrateShell {
 public:
  void AddSection(std::string name, std::vector<uint8_t> payload) {
    sections_.push_back({std::move(name), std::move(payload), false, 0, 0});
  }

  void AddBadSection(std::string name, int64_t start, int64_t size) {
    sections_.push_back({std::move(name), {}, true, start, size});
  }

  std::vector<uint8_t> Build() const {
    const size_t toc_offset = kCrateBootstrapSize;
    const size_t toc_size = 8 + sections_.size() * (16 + 8 + 8);
    int64_t payload_offset = static_cast<int64_t>(toc_offset + toc_size);

    std::vector<uint8_t> out = Bootstrap(static_cast<int64_t>(toc_offset));
    PutU64(&out, static_cast<uint64_t>(sections_.size()));

    for (const Section& s : sections_) {
      char name[16] = {};
      std::memcpy(name, s.name.c_str(), std::min<size_t>(s.name.size(), 15));
      out.insert(out.end(), name, name + 16);
      const int64_t start = s.override_bounds ? s.start : payload_offset;
      const int64_t size =
          s.override_bounds ? s.size : static_cast<int64_t>(s.payload.size());
      PutU64(&out, static_cast<uint64_t>(start));
      PutU64(&out, static_cast<uint64_t>(size));
      if (!s.override_bounds) payload_offset += size;
    }

    for (const Section& s : sections_) {
      if (!s.override_bounds) {
        out.insert(out.end(), s.payload.begin(), s.payload.end());
      }
    }
    return out;
  }

 private:
  std::vector<Section> sections_;
};

std::vector<uint8_t> U64Section(uint64_t v) {
  std::vector<uint8_t> out;
  PutU64(&out, v);
  return out;
}

std::vector<uint8_t> TokensEmpty() {
  std::vector<uint8_t> out;
  PutU64(&out, 0);  // token count
  PutU64(&out, 0);  // uncompressed size
  PutU64(&out, 0);  // compressed size
  return out;
}

std::vector<uint8_t> TokensFromRaw(uint64_t count,
                                   const std::vector<uint8_t>& raw) {
  std::vector<uint8_t> out;
  PutU64(&out, count);
  PutU64(&out, static_cast<uint64_t>(raw.size()));
  CompressResult cr = CompressCrateBlob(raw.data(), raw.size());
  assert(cr.success);
  PutU64(&out, static_cast<uint64_t>(cr.data.size()));
  out.insert(out.end(), cr.data.begin(), cr.data.end());
  return out;
}

std::vector<uint8_t> FieldsEmpty() {
  std::vector<uint8_t> out;
  PutU64(&out, 0);  // field count
  PutU64(&out, 0);  // field token-index payload bytes
  PutU64(&out, 0);  // field value-rep payload bytes
  return out;
}

std::vector<uint8_t> StringsTable(const std::vector<uint32_t>& token_indices) {
  std::vector<uint8_t> out;
  PutU64(&out, static_cast<uint64_t>(token_indices.size()));
  for (uint32_t idx : token_indices) PutU32(&out, idx);
  return out;
}

std::vector<uint8_t> CompressedFieldIndices(
    const std::vector<uint32_t>& indices) {
  std::vector<uint8_t> delta =
      EncodeDeltaU32(indices.data(), indices.size());
  CompressResult cr = CompressCrateBlob(delta.data(), delta.size());
  assert(cr.success);
  return cr.data;
}

std::vector<uint8_t> CompressedValueReps(
    const std::vector<ValueRep>& reps) {
  std::vector<uint8_t> raw(reps.size() * sizeof(uint64_t));
  for (size_t i = 0; i < reps.size(); ++i) {
    const uint64_t v = reps[i].raw();
    std::memcpy(raw.data() + i * sizeof(uint64_t), &v, sizeof(uint64_t));
  }
  CompressResult cr = CompressCrateBlob(raw.data(), raw.size());
  assert(cr.success);
  return cr.data;
}

void AppendCompressedU32Array(std::vector<uint8_t>* out,
                              const std::vector<uint32_t>& values) {
  std::vector<uint8_t> data = CompressedFieldIndices(values);
  PutU64(out, static_cast<uint64_t>(data.size()));
  out->insert(out->end(), data.begin(), data.end());
}

std::vector<uint8_t> FieldTable(const std::vector<uint32_t>& token_indices,
                                const std::vector<ValueRep>& reps) {
  assert(token_indices.size() == reps.size());
  std::vector<uint8_t> out;
  PutU64(&out, static_cast<uint64_t>(token_indices.size()));
  std::vector<uint8_t> indices = CompressedFieldIndices(token_indices);
  PutU64(&out, static_cast<uint64_t>(indices.size()));
  out.insert(out.end(), indices.begin(), indices.end());
  std::vector<uint8_t> value_reps = CompressedValueReps(reps);
  PutU64(&out, static_cast<uint64_t>(value_reps.size()));
  out.insert(out.end(), value_reps.begin(), value_reps.end());
  return out;
}

std::vector<uint8_t> FieldTableRawReps(
    const std::vector<uint32_t>& token_indices,
    const std::vector<ValueRep>& reps) {
  assert(token_indices.size() == reps.size());
  std::vector<uint8_t> out;
  PutU64(&out, static_cast<uint64_t>(token_indices.size()));
  std::vector<uint8_t> indices = CompressedFieldIndices(token_indices);
  PutU64(&out, static_cast<uint64_t>(indices.size()));
  out.insert(out.end(), indices.begin(), indices.end());
  PutU64(&out, static_cast<uint64_t>(reps.size() * sizeof(uint64_t)));
  for (const ValueRep& rep : reps) PutU64(&out, rep.raw());
  return out;
}

std::vector<uint8_t> FieldsetsEmpty() {
  return U64Section(0);
}

std::vector<uint8_t> FieldsetsCompressed(
    const std::vector<uint32_t>& field_indices) {
  std::vector<uint8_t> out;
  PutU64(&out, static_cast<uint64_t>(field_indices.size()));
  AppendCompressedU32Array(&out, field_indices);
  return out;
}

std::vector<uint8_t> SpecsEmpty() {
  return U64Section(0);
}

std::vector<uint8_t> SpecsCompressed(const std::vector<uint32_t>& path_indices,
                                     const std::vector<uint32_t>& fieldsets,
                                     const std::vector<uint32_t>& spec_types) {
  assert(path_indices.size() == fieldsets.size());
  assert(path_indices.size() == spec_types.size());
  std::vector<uint8_t> out;
  PutU64(&out, static_cast<uint64_t>(path_indices.size()));
  AppendCompressedU32Array(&out, path_indices);
  AppendCompressedU32Array(&out, fieldsets);
  AppendCompressedU32Array(&out, spec_types);
  return out;
}

std::vector<uint8_t> PathsRootOnly() {
  return U64Section(0);
}

std::vector<uint8_t> PathsCompressedCount(uint64_t total_paths,
                                          const std::vector<uint32_t>& path_indices,
                                          const std::vector<uint32_t>& element_tokens,
                                          const std::vector<uint32_t>& jumps) {
  assert(path_indices.size() == element_tokens.size());
  assert(path_indices.size() == jumps.size());
  std::vector<uint8_t> out;
  PutU64(&out, total_paths);
  PutU64(&out, static_cast<uint64_t>(path_indices.size()));
  AppendCompressedU32Array(&out, path_indices);
  AppendCompressedU32Array(&out, element_tokens);
  AppendCompressedU32Array(&out, jumps);
  return out;
}

std::vector<uint8_t> PathsCompressed(const std::vector<uint32_t>& path_indices,
                                     const std::vector<uint32_t>& element_tokens,
                                     const std::vector<uint32_t>& jumps) {
  return PathsCompressedCount(static_cast<uint64_t>(path_indices.size()),
                              path_indices, element_tokens, jumps);
}

std::vector<uint8_t> BuildFieldValuePayloadCase(
    CrateTypeId type, bool is_array, const std::vector<uint8_t>& payload) {
  const std::vector<uint8_t> tokens = TokensFromRaw(1, {'a', '\0'});
  const std::vector<uint8_t> dummy_fields = FieldTableRawReps(
      {0}, {ValueRep::Make(type, 0, is_array, false, false)});
  const size_t section_count = 3;
  const size_t payload_base = kCrateBootstrapSize + 8 + section_count * (16 + 8 + 8);
  const size_t payload_start = payload_base + tokens.size() + dummy_fields.size();
  std::vector<uint8_t> fields = FieldTableRawReps(
      {0}, {ValueRep::Make(type, static_cast<uint64_t>(payload_start),
                           is_array, false, false)});
  CrateShell c;
  c.AddSection("TOKENS", tokens);
  c.AddSection("FIELDS", std::move(fields));
  c.AddSection("JUNK", payload);
  return c.Build();
}

std::vector<uint8_t> BuildFieldValuePayloadCaseWithStrings(
    CrateTypeId type, const std::vector<uint8_t>& payload) {
  const std::vector<uint8_t> tokens = TokensFromRaw(1, {'a', '\0'});
  const std::vector<uint8_t> strings = StringsTable({0});
  const std::vector<uint8_t> dummy_fields = FieldTableRawReps(
      {0}, {ValueRep::Make(type, 0, false, false, false)});
  const size_t section_count = 4;
  const size_t payload_base = kCrateBootstrapSize + 8 + section_count * (16 + 8 + 8);
  const size_t payload_start =
      payload_base + tokens.size() + strings.size() + dummy_fields.size();
  std::vector<uint8_t> fields = FieldTableRawReps(
      {0}, {ValueRep::Make(type, static_cast<uint64_t>(payload_start),
                           false, false, false)});
  CrateShell c;
  c.AddSection("TOKENS", tokens);
  c.AddSection("STRINGS", strings);
  c.AddSection("FIELDS", std::move(fields));
  c.AddSection("JUNK", payload);
  return c.Build();
}

CrateShell PrefixThroughTokens() {
  CrateShell c;
  c.AddSection("TOKENS", TokensEmpty());
  return c;
}

CrateShell PrefixThroughFields() {
  CrateShell c = PrefixThroughTokens();
  c.AddSection("FIELDS", FieldsEmpty());
  return c;
}

CrateShell PrefixThroughFieldsets() {
  CrateShell c = PrefixThroughFields();
  c.AddSection("FIELDSETS", FieldsetsEmpty());
  return c;
}

CrateShell PrefixThroughSpecs() {
  CrateShell c = PrefixThroughFieldsets();
  c.AddSection("SPECS", SpecsEmpty());
  return c;
}

USDCLoadOptions TightOptions() {
  USDCLoadOptions opts;
  opts.crate_options.max_tokens = 1;
  opts.crate_options.max_strings = 1;
  opts.crate_options.max_fields = 1;
  opts.crate_options.max_specs = 1;
  opts.crate_options.max_paths = 1;
  opts.crate_options.max_array_elements = 16;
  return opts;
}

USDCLoadOptions TightMemoryOptions(size_t bytes) {
  USDCLoadOptions opts = TightOptions();
  opts.crate_options.max_memory = bytes;
  return opts;
}

void ExpectReject(const char* name, const std::vector<uint8_t>& bytes,
                  const USDCLoadOptions& opts = TightOptions()) {
  USDCLoadResult r = LoadUSDCFromMemory(bytes.data(), bytes.size(), opts);
  if (r.success) {
    std::cerr << "Malformed case unexpectedly loaded: " << name << std::endl;
  }
  assert(!r.success);
  assert(!r.errors.empty() || !r.error_summary.empty());
  std::cout << "  rejected " << name << std::endl;
}

}  // namespace

int main() {
  std::cout << "=== TinyUSDZ Next Malformed USDC Tests ===" << std::endl;

  {
    std::vector<uint8_t> bytes(kCrateBootstrapSize, 0);
    std::memcpy(bytes.data(), "NOT-USDC", 8);
    ExpectReject("bad magic", bytes);
  }

  {
    std::vector<uint8_t> bytes(kCrateMagic, kCrateMagic + 8);
    ExpectReject("truncated bootstrap", bytes);
  }

  ExpectReject("toc offset before bootstrap", Bootstrap(24));
  ExpectReject("toc offset past eof", Bootstrap(999999));

  {
    std::vector<uint8_t> bytes = Bootstrap(static_cast<int64_t>(kCrateBootstrapSize));
    PutU64(&bytes, 101);
    ExpectReject("too many toc sections", bytes);
  }

  {
    std::vector<uint8_t> bytes = Bootstrap(static_cast<int64_t>(kCrateBootstrapSize));
    PutU64(&bytes, 1);
    bytes.insert(bytes.end(), {'T', 'O', 'K', 'E', 'N', 'S'});
    ExpectReject("truncated toc entry name", bytes);
  }

  {
    std::vector<uint8_t> bytes = Bootstrap(static_cast<int64_t>(kCrateBootstrapSize));
    PutU64(&bytes, 1);
    char name[16] = {};
    std::memcpy(name, "TOKENS", 6);
    bytes.insert(bytes.end(), name, name + 16);
    PutU64(&bytes, 128);
    ExpectReject("truncated toc entry size", bytes);
  }

  {
    CrateShell c;
    c.AddBadSection("TOKENS", -1, 1);
    ExpectReject("negative section start", c.Build());
  }

  {
    CrateShell c;
    c.AddBadSection("TOKENS", static_cast<int64_t>(kCrateBootstrapSize), -1);
    ExpectReject("negative section size", c.Build());
  }

  {
    CrateShell c;
    c.AddBadSection("TOKENS", 999999, 1);
    ExpectReject("section range past eof", c.Build());
  }

  {
    CrateShell c;
    c.AddBadSection("TOKENS", 100, INT64_MAX);
    ExpectReject("section size overflow", c.Build());
  }

  {
    CrateShell c;
    ExpectReject("missing tokens section", c.Build());
  }

  {
    CrateShell c;
    c.AddSection("TOKENS", U64Section(2));
    ExpectReject("token count over cap", c.Build());
  }

  {
    CrateShell c;
    c.AddSection("TOKENS", TokensFromRaw(1, {'x'}));
    ExpectReject("token table missing terminator", c.Build());
  }

  {
    CrateShell c;
    std::vector<uint8_t> tokens;
    PutU64(&tokens, 1);
    PutU64(&tokens, 1024);  // claimed uncompressed bytes, no allocation allowed
    PutU64(&tokens, 0);
    ExpectReject("token table over memory budget", c.Build(),
                 TightMemoryOptions(128));
  }

  {
    CrateShell c = PrefixThroughTokens();
    c.AddSection("STRINGS", U64Section(2));
    ExpectReject("string count over cap", c.Build());
  }

  {
    CrateShell c;
    c.AddSection("TOKENS", TokensFromRaw(1, {'\0'}));
    c.AddSection("STRINGS", StringsTable({1}));
    ExpectReject("string token index out of range", c.Build());
  }

  {
    CrateShell c = PrefixThroughTokens();
    c.AddSection("FIELDS", U64Section(2));
    ExpectReject("field count over cap", c.Build());
  }

  {
    CrateShell c = PrefixThroughFields();
    c.AddSection("FIELDSETS", U64Section(2));
    ExpectReject("fieldset index count over cap", c.Build());
  }

  {
    CrateShell c = PrefixThroughFieldsets();
    c.AddSection("SPECS", U64Section(2));
    ExpectReject("spec count over cap", c.Build());
  }

  {
    CrateShell c = PrefixThroughSpecs();
    c.AddSection("PATHS", U64Section(2));
    ExpectReject("path count over cap", c.Build());
  }

  {
    CrateShell c = PrefixThroughSpecs();
    ExpectReject("missing paths section", c.Build());
  }

  {
    CrateShell c = PrefixThroughTokens();
    std::vector<uint8_t> fields;
    PutU64(&fields, 1);  // field count
    PutU64(&fields, 64); // token-index payload claims bytes not present
    c.AddSection("FIELDS", std::move(fields));
    ExpectReject("truncated field index payload", c.Build());
  }

  {
    CrateShell c = PrefixThroughTokens();
    std::vector<uint8_t> fields;
    PutU64(&fields, 1);
    PutU64(&fields, 0);
    c.AddSection("FIELDS", std::move(fields));
    ExpectReject("empty field index payload", c.Build());
  }

  {
    CrateShell c = PrefixThroughTokens();
    std::vector<uint8_t> fields;
    PutU64(&fields, 1);
    std::vector<uint8_t> indices = CompressedFieldIndices({0});
    PutU64(&fields, static_cast<uint64_t>(indices.size()));
    fields.insert(fields.end(), indices.begin(), indices.end());
    c.AddSection("FIELDS", std::move(fields));
    ExpectReject("missing field value-rep size", c.Build());
  }

  {
    CrateShell c = PrefixThroughTokens();
    std::vector<uint8_t> fields;
    PutU64(&fields, 1);
    std::vector<uint8_t> indices = CompressedFieldIndices({0});
    PutU64(&fields, static_cast<uint64_t>(indices.size()));
    fields.insert(fields.end(), indices.begin(), indices.end());
    PutU64(&fields, 16);  // value-rep payload claims bytes not present
    c.AddSection("FIELDS", std::move(fields));
    ExpectReject("truncated compressed field value reps", c.Build());
  }

  {
    CrateShell c;
    c.AddSection("TOKENS", TokensFromRaw(1, {'a', '\0'}));
    const uint64_t raw = (static_cast<uint64_t>(CrateTypeId::Int) << 48) |
                         0x800000000000ull;
    c.AddSection("FIELDS", FieldTable({0}, {ValueRep(raw)}));
    ExpectReject("negative ValueRep payload offset", c.Build());
  }

  {
    CrateShell c;
    c.AddSection("TOKENS", TokensFromRaw(1, {'a', '\0'}));
    c.AddSection("FIELDS", FieldTable(
        {0},
        {ValueRep::Make(CrateTypeId::Int, 999999, false, false, false)}));
    ExpectReject("ValueRep payload offset outside file", c.Build());
  }

  {
    std::vector<uint8_t> payload(4, 0);
    ExpectReject("truncated scalar ValueRep payload",
                 BuildFieldValuePayloadCase(CrateTypeId::Double, false, payload));
  }

  {
    std::vector<uint8_t> payload;
    PutU64(&payload, 17);
    ExpectReject("array ValueRep count over cap",
                 BuildFieldValuePayloadCase(CrateTypeId::Int, true, payload));
  }

  {
    std::vector<uint8_t> payload;
    PutU64(&payload, 17);
    ExpectReject("dictionary ValueRep count over cap",
                 BuildFieldValuePayloadCase(CrateTypeId::Dictionary, false, payload));
  }

  {
    std::vector<uint8_t> payload;
    PutU64(&payload, 1);
    ExpectReject("dictionary ValueRep entry truncated",
                 BuildFieldValuePayloadCaseWithStrings(CrateTypeId::Dictionary, payload));
  }

  {
    std::vector<uint8_t> payload;
    PutU64(&payload, 1);
    PutU32(&payload, 0);
    PutU64(&payload, 0);  // recursive ValueRep cannot point into the offset field
    PutU64(&payload, ValueRep::Make(CrateTypeId::Invalid, 0, false, true).raw());
    ExpectReject("dictionary recursive offset invalid",
                 BuildFieldValuePayloadCaseWithStrings(CrateTypeId::Dictionary, payload));
  }

  {
    std::vector<uint8_t> payload;
    PutU64(&payload, 1);
    PutU32(&payload, 0);
    PutU64(&payload, 1024);  // recursive ValueRep points beyond EOF
    PutU64(&payload, ValueRep::Make(CrateTypeId::Invalid, 0, false, true).raw());
    ExpectReject("dictionary recursive ValueRep outside file",
                 BuildFieldValuePayloadCaseWithStrings(CrateTypeId::Dictionary, payload));
  }

  {
    std::vector<uint8_t> payload = {0x02};
    ExpectReject("truncated list-op ValueRep payload",
                 BuildFieldValuePayloadCase(CrateTypeId::TokenListOp, false, payload));
  }

  {
    std::vector<uint8_t> payload = {0x02};
    PutU64(&payload, 17);
    ExpectReject("list-op ValueRep run count over cap",
                 BuildFieldValuePayloadCase(CrateTypeId::TokenListOp, false, payload));
  }

  {
    std::vector<uint8_t> payload = {0x02};
    PutU64(&payload, 1);
    ExpectReject("list-op ValueRep run items truncated",
                 BuildFieldValuePayloadCase(CrateTypeId::TokenListOp, false, payload));
  }

  {
    CrateShell c;
    c.AddSection("TOKENS", TokensFromRaw(1, {'a', '\0'}));
    c.AddSection("FIELDS", FieldTable(
        {1},
        {ValueRep::Make(CrateTypeId::Invalid, 0, false, false, false)}));
    ExpectReject("field token index out of range", c.Build());
  }

  {
    CrateShell c = PrefixThroughFields();
    std::vector<uint8_t> fieldsets;
    PutU64(&fieldsets, 1);
    PutU32(&fieldsets, 0);  // not a valid compressed or legacy table payload
    c.AddSection("FIELDSETS", std::move(fieldsets));
    ExpectReject("malformed fieldset payload", c.Build());
  }

  {
    CrateShell c = PrefixThroughFieldsets();
    std::vector<uint8_t> specs;
    PutU64(&specs, 1);
    PutU64(&specs, 128);  // first compressed specs array exceeds section
    c.AddSection("SPECS", std::move(specs));
    ExpectReject("malformed specs payload", c.Build());
  }

  {
    CrateShell c = PrefixThroughFieldsets();
    std::vector<uint8_t> specs;
    PutU64(&specs, 1);
    AppendCompressedU32Array(&specs, {0});
    c.AddSection("SPECS", std::move(specs));
    ExpectReject("missing second specs array", c.Build());
  }

  {
    CrateShell c = PrefixThroughFieldsets();
    std::vector<uint8_t> specs;
    PutU64(&specs, 1);
    AppendCompressedU32Array(&specs, {0});
    AppendCompressedU32Array(&specs, {0});
    PutU64(&specs, 128);  // third compressed specs array exceeds section
    c.AddSection("SPECS", std::move(specs));
    ExpectReject("truncated third specs array", c.Build());
  }

  {
    CrateShell c = PrefixThroughFields();
    c.AddSection("FIELDSETS", FieldsetsCompressed({0xFFFFFFFFu}));
    c.AddSection("SPECS", SpecsCompressed(
        {0}, {1}, {static_cast<uint32_t>(SpecType::PseudoRoot)}));
    ExpectReject("spec fieldset index out of range", c.Build());
  }

  {
    CrateShell c = PrefixThroughFields();
    c.AddSection("FIELDSETS", FieldsetsCompressed({0xFFFFFFFFu}));
    c.AddSection("SPECS", SpecsCompressed({0}, {0}, {static_cast<uint32_t>(SpecType::PseudoRoot)}));
    std::vector<uint8_t> paths;
    PutU64(&paths, 1);
    PutU64(&paths, 1);
    AppendCompressedU32Array(&paths, {0});
    c.AddSection("PATHS", std::move(paths));
    ExpectReject("missing path element tokens array", c.Build());
  }

  {
    CrateShell c = PrefixThroughFields();
    c.AddSection("FIELDSETS", FieldsetsCompressed({0xFFFFFFFFu}));
    c.AddSection("SPECS", SpecsCompressed({0}, {0}, {static_cast<uint32_t>(SpecType::PseudoRoot)}));
    std::vector<uint8_t> paths;
    PutU64(&paths, 1);
    PutU64(&paths, 1);
    AppendCompressedU32Array(&paths, {0});
    AppendCompressedU32Array(&paths, {0});
    PutU64(&paths, 128);  // jump-index compressed data exceeds section
    c.AddSection("PATHS", std::move(paths));
    ExpectReject("truncated path jump array", c.Build());
  }

  {
    CrateShell c;
    c.AddSection("TOKENS", TokensFromRaw(1, {'\0'}));
    c.AddSection("FIELDS", FieldsEmpty());
    c.AddSection("FIELDSETS", FieldsetsCompressed({0xFFFFFFFFu}));
    c.AddSection("SPECS", SpecsCompressed(
        {0}, {0}, {static_cast<uint32_t>(SpecType::PseudoRoot)}));
    c.AddSection("PATHS", PathsCompressed({1}, {0}, {0xFFFFFFFEu}));
    ExpectReject("path table index out of range", c.Build());
  }

  {
    CrateShell c;
    c.AddSection("TOKENS", TokensFromRaw(1, {'\0'}));
    c.AddSection("FIELDS", FieldsEmpty());
    c.AddSection("FIELDSETS", FieldsetsCompressed({0xFFFFFFFFu}));
    c.AddSection("SPECS", SpecsCompressed(
        {0}, {0}, {static_cast<uint32_t>(SpecType::PseudoRoot)}));
    c.AddSection("PATHS", PathsCompressed({0}, {1}, {0xFFFFFFFEu}));
    ExpectReject("path element token index out of range", c.Build());
  }

  {
    CrateShell c;
    c.AddSection("TOKENS", TokensFromRaw(1, {'\0'}));
    c.AddSection("FIELDS", FieldsEmpty());
    c.AddSection("FIELDSETS", FieldsetsCompressed({0xFFFFFFFFu}));
    c.AddSection("SPECS", SpecsCompressed(
        {0}, {0}, {static_cast<uint32_t>(SpecType::PseudoRoot)}));
    c.AddSection("PATHS", PathsCompressed({0}, {0}, {3}));
    ExpectReject("path jump outside encoded table", c.Build());
  }

  {
    CrateShell c;
    c.AddSection("TOKENS", TokensFromRaw(1, {'\0'}));
    c.AddSection("FIELDS", FieldsEmpty());
    c.AddSection("FIELDSETS", FieldsetsCompressed({0xFFFFFFFFu}));
    c.AddSection("SPECS", SpecsCompressed(
        {0}, {0}, {static_cast<uint32_t>(SpecType::PseudoRoot)}));
    c.AddSection("PATHS", PathsCompressedCount(2, {0, 0}, {0, 0}, {0, 0xFFFFFFFEu}));
    ExpectReject("duplicate path table index", c.Build());
  }

  {
    CrateShell c;
    c.AddSection("TOKENS", TokensFromRaw(1, {'\0'}));
    c.AddSection("FIELDS", FieldsEmpty());
    c.AddSection("FIELDSETS", FieldsetsCompressed({0xFFFFFFFFu}));
    c.AddSection("SPECS", SpecsCompressed(
        {1}, {0}, {static_cast<uint32_t>(SpecType::PseudoRoot)}));
    c.AddSection("PATHS", PathsCompressedCount(2, {0}, {0}, {0xFFFFFFFEu}));
    ExpectReject("spec references empty path slot", c.Build());
  }

  std::cout << "All malformed USDC tests passed!" << std::endl;
  return 0;
}
