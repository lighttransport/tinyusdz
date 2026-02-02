// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Memory-mapped file wrapper
// Cross-platform mmap for zero-copy file access

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <memory>

namespace tinyusdz {
namespace next {

/// Memory-mapped file for read-only access
/// Provides zero-copy access to file contents
class MmapFile {
public:
  /// Default constructor - no file mapped
  MmapFile();

  /// Destructor - unmaps file
  ~MmapFile();

  /// Non-copyable
  MmapFile(const MmapFile&) = delete;
  MmapFile& operator=(const MmapFile&) = delete;

  /// Moveable
  MmapFile(MmapFile&& other) noexcept;
  MmapFile& operator=(MmapFile&& other) noexcept;

  /// Map a file for reading
  /// @param filename Path to file
  /// @param err Error message on failure
  /// @return true on success
  bool Open(const char* filename, std::string* err = nullptr);
  bool Open(const std::string& filename, std::string* err = nullptr) {
    return Open(filename.c_str(), err);
  }

  /// Unmap the file
  void Close();

  /// Check if a file is currently mapped
  bool IsOpen() const { return data_ != nullptr; }

  /// Get pointer to mapped data
  const uint8_t* data() const { return data_; }

  /// Get size of mapped data in bytes
  size_t size() const { return size_; }

  /// Get filename
  const std::string& filename() const { return filename_; }

  /// Convenience: get pointer at offset (with bounds check)
  const uint8_t* at(size_t offset) const {
    return (offset < size_) ? (data_ + offset) : nullptr;
  }

  /// Convenience: check if range is valid
  bool is_valid_range(size_t offset, size_t length) const {
    return offset + length <= size_ && offset + length >= offset;  // overflow check
  }

private:
  uint8_t* data_ = nullptr;
  size_t size_ = 0;
  std::string filename_;

#ifdef _WIN32
  void* file_handle_ = nullptr;
  void* mapping_handle_ = nullptr;
#else
  int fd_ = -1;
#endif
};

/// RAII wrapper that keeps a MmapFile alive
/// Used to ensure mmap'd data remains valid while views are in use
class MmapFileRef {
public:
  MmapFileRef() = default;
  explicit MmapFileRef(std::shared_ptr<MmapFile> file) : file_(std::move(file)) {}

  /// Check if valid
  bool IsValid() const { return file_ && file_->IsOpen(); }

  /// Get the underlying mmap file
  MmapFile* get() const { return file_.get(); }
  MmapFile* operator->() const { return file_.get(); }
  MmapFile& operator*() const { return *file_; }

  /// Get reference count
  long use_count() const { return file_.use_count(); }

private:
  std::shared_ptr<MmapFile> file_;
};

/// Create a new MmapFile and return a shared reference
inline MmapFileRef MakeMmapFile(const std::string& filename, std::string* err = nullptr) {
  auto file = std::make_shared<MmapFile>();
  if (!file->Open(filename, err)) {
    return MmapFileRef();
  }
  return MmapFileRef(std::move(file));
}

}  // namespace next
}  // namespace tinyusdz
