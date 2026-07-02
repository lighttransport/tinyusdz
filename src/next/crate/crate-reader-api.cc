// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - USDC CrateReader public API wrappers

#include "crate-reader-internal.hh"

#include "crate-data-source.hh"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace tinyusdz {
namespace next {

CrateReadResult CrateReader::Impl::ReadFromString(std::string&& bytes) {
  if (options_.max_memory && bytes.size() > options_.max_memory) {
    result_ = CrateReadResult();
    AddError("Input exceeds max_memory budget");
    return std::move(result_);
  }

  source_ = CrateDataSource::Adopt(std::move(bytes), CrateVersion{});
  return ParseFromSource();
}

CrateReadResult CrateReader::Impl::ParseFromSource() {
  using Clock = std::chrono::steady_clock;
  const bool timing = options_.enable_timing;
  const auto t_parse_start = Clock::now();
  auto elapsed_ms = [](Clock::duration d) {
    return std::chrono::duration<double, std::milli>(d).count();
  };
  auto log_phase = [&](const char* name, Clock::time_point a,
                       Clock::time_point b) {
    if (!timing) return;
    std::fprintf(stderr, "[next_crate_read] %s=%.1fms\n", name,
                 elapsed_ms(b - a));
  };

  result_ = CrateReadResult();
  result_.source_was_mmap = source_ && source_->is_mmapped();
  tokens_.clear();
  string_indices_.clear();
  fields_.clear();
  fieldset_indices_.clear();
  fieldset_offsets_.clear();
  fieldset_counts_.clear();
  fieldset_index_to_id_.clear();
  specs_.clear();
  paths_.clear();

  if (!source_ || source_->size() < kCrateBootstrapSize) {
    AddError("Invalid input data");
    return std::move(result_);
  }

  reader_ = std::make_unique<StreamReader>(source_->base(), source_->size());

#if defined(TINYUSDZ_NEXT_PARALLEL_BUILD_STAGE)
  // The async fieldsets decode must be joined on EVERY exit (it writes Impl
  // members and installs a decode ctx over the source buffer).
  struct FieldsetsJoinGuard {
    Impl* impl;
    ~FieldsetsJoinGuard() { impl->JoinFieldsetsDecode(); }
  } fieldsets_join_guard{this};
#endif

  auto t0 = Clock::now();
  if (!ReadBootstrap()) return std::move(result_);
  auto t1 = Clock::now();
  log_phase("bootstrap", t0, t1);
  source_->set_version(version_);
  t0 = Clock::now();
  if (!ReadTOC()) return std::move(result_);
  t1 = Clock::now();
  log_phase("toc", t0, t1);
  t0 = Clock::now();
  if (!ReadTokens()) return std::move(result_);
  t1 = Clock::now();
  log_phase("tokens", t0, t1);
  t0 = Clock::now();
  if (!ReadStrings()) return std::move(result_);
  t1 = Clock::now();
  log_phase("strings", t0, t1);
  t0 = Clock::now();
  if (!ReadFields()) return std::move(result_);
  t1 = Clock::now();
  log_phase("fields", t0, t1);
  t0 = Clock::now();
  if (!ReadFieldsets()) return std::move(result_);
  t1 = Clock::now();
  log_phase("fieldsets", t0, t1);
  t0 = Clock::now();
  if (!ReadSpecs()) return std::move(result_);
  t1 = Clock::now();
  log_phase("specs", t0, t1);
  t0 = Clock::now();
  if (!ReadPaths()) return std::move(result_);
  t1 = Clock::now();
  log_phase("paths", t0, t1);
  if (!JoinFieldsetsDecode()) return std::move(result_);
  t0 = Clock::now();
  if (!BuildStage()) return std::move(result_);
  t1 = Clock::now();
  log_phase("build_stage", t0, t1);

  result_.success = result_.errors.empty();
  result_.version = version_;
  if (timing) {
    const auto t_end = Clock::now();
    std::fprintf(stderr,
                 "[next_crate_read] total=%.1fms bytes=%zu tokens=%zu strings=%zu "
                 "fields=%zu fieldsets=%zu specs=%zu paths=%zu mmap=%d finalize=%d\n",
                 elapsed_ms(t_end - t_parse_start),
                 source_ ? source_->size() : size_t{0}, tokens_.size(),
                 string_indices_.size(), fields_.size(), fieldset_counts_.size(),
                 specs_.size(), paths_.size(), result_.source_was_mmap ? 1 : 0,
                 options_.finalize_stage ? 1 : 0);
  }
  return std::move(result_);
}

CrateReadResult CrateReader::Impl::Read(const uint8_t* data, size_t size) {
  if (!data) {
    result_ = CrateReadResult();
    AddError("Invalid input data");
    return std::move(result_);
  }
  if (options_.max_memory && size > options_.max_memory) {
    result_ = CrateReadResult();
    AddError("Input exceeds max_memory budget");
    return std::move(result_);
  }
  return ReadFromString(
      std::string(reinterpret_cast<const char*>(data), size));
}

CrateReadResult CrateReader::Impl::ReadBorrowed(const uint8_t* data,
                                                size_t size) {
  if (!data) {
    result_ = CrateReadResult();
    AddError("Invalid input data");
    return std::move(result_);
  }
  if (options_.max_memory && size > options_.max_memory) {
    result_ = CrateReadResult();
    AddError("Input exceeds max_memory budget");
    return std::move(result_);
  }
  source_ = CrateDataSource::AdoptBorrowed(data, size, CrateVersion{});
  return ParseFromSource();
}

CrateReadResult CrateReader::Impl::ReadOwned(std::string&& owned) {
  return ReadFromString(std::move(owned));
}

CrateReadResult CrateReader::Impl::ReadFile(const char* filename) {
  if (options_.max_memory) {
    std::ifstream probe(filename, std::ios::binary | std::ios::ate);
    if (!probe.is_open()) {
      CrateReadResult result;
      result.errors.push_back({0, std::string("Failed to open file: ") + filename});
      return result;
    }
    std::streamsize probed_size = probe.tellg();
    if (probed_size < 0) {
      CrateReadResult result;
      result.errors.push_back({0, "Failed to determine file size"});
      return result;
    }
    if (static_cast<uint64_t>(probed_size) >
        static_cast<uint64_t>(options_.max_memory)) {
      CrateReadResult result;
      result.errors.push_back({0, "File exceeds max_memory budget"});
      return result;
    }
  }

  if (options_.use_mmap) {
    if (auto src = CrateDataSource::MmapFile(filename)) {
      source_ = std::move(src);
      return ParseFromSource();
    }
  }

  std::ifstream file(filename, std::ios::binary | std::ios::ate);
  if (!file.is_open()) {
    CrateReadResult result;
    result.errors.push_back({0, std::string("Failed to open file: ") + filename});
    return result;
  }

  std::streamsize size = file.tellg();
  if (size < 0) {
    CrateReadResult result;
    result.errors.push_back({0, "Failed to determine file size"});
    return result;
  }
  if (options_.max_memory &&
      static_cast<uint64_t>(size) > static_cast<uint64_t>(options_.max_memory)) {
    CrateReadResult result;
    result.errors.push_back({0, "File exceeds max_memory budget"});
    return result;
  }
  file.seekg(0, std::ios::beg);

  std::string data(static_cast<size_t>(size), '\0');
  if (!file.read(&data[0], size)) {
    CrateReadResult result;
    result.errors.push_back({0, "Failed to read file contents"});
    return result;
  }

  return ReadOwned(std::move(data));
}

CrateReader::CrateReader(const CrateReadOptions& options)
    : impl_(std::make_unique<Impl>(options)) {}

CrateReader::~CrateReader() = default;

CrateReader::CrateReader(CrateReader&&) noexcept = default;
CrateReader& CrateReader::operator=(CrateReader&&) noexcept = default;

CrateReadResult CrateReader::Read(const uint8_t* data, size_t size) {
  return impl_->Read(data, size);
}
CrateReadResult CrateReader::ReadBorrowed(const uint8_t* data, size_t size) {
  return impl_->ReadBorrowed(data, size);
}

CrateReadResult CrateReader::ReadOwned(std::string&& owned) {
  return impl_->ReadOwned(std::move(owned));
}

CrateReadResult CrateReader::ReadFile(const char* filename) {
  return impl_->ReadFile(filename);
}

std::vector<std::string> CrateReader::tokens() const {
  return impl_->tokens();
}

std::vector<std::string> CrateReader::paths() const {
  return impl_->paths();
}

const std::vector<CrateField>& CrateReader::fields() const {
  return impl_->fields();
}

const std::vector<CrateSpec>& CrateReader::specs() const {
  return impl_->specs();
}

const std::vector<uint32_t>& CrateReader::fieldset_indices() const {
  return impl_->fieldset_indices();
}

bool IsUSDCData(const uint8_t* data, size_t size) {
  if (!data || size < 8) return false;
  return std::memcmp(data, kCrateMagic, 8) == 0;
}

bool IsUSDCFile(const char* filename) {
  std::ifstream file(filename, std::ios::binary);
  if (!file.is_open()) return false;

  char magic[8];
  file.read(magic, 8);
  if (!file) return false;

  return std::memcmp(magic, kCrateMagic, 8) == 0;
}

}  // namespace next
}  // namespace tinyusdz
