// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - Memory-mapped file implementation

#include "mmap-file.hh"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif

namespace tinyusdz {
namespace next {

MmapFile::MmapFile() = default;

MmapFile::~MmapFile() {
  Close();
}

MmapFile::MmapFile(MmapFile&& other) noexcept
    : data_(other.data_),
      size_(other.size_),
      filename_(std::move(other.filename_))
#ifdef _WIN32
      , file_handle_(other.file_handle_)
      , mapping_handle_(other.mapping_handle_)
#else
      , fd_(other.fd_)
#endif
{
  other.data_ = nullptr;
  other.size_ = 0;
#ifdef _WIN32
  other.file_handle_ = nullptr;
  other.mapping_handle_ = nullptr;
#else
  other.fd_ = -1;
#endif
}

MmapFile& MmapFile::operator=(MmapFile&& other) noexcept {
  if (this != &other) {
    Close();
    data_ = other.data_;
    size_ = other.size_;
    filename_ = std::move(other.filename_);
#ifdef _WIN32
    file_handle_ = other.file_handle_;
    mapping_handle_ = other.mapping_handle_;
    other.file_handle_ = nullptr;
    other.mapping_handle_ = nullptr;
#else
    fd_ = other.fd_;
    other.fd_ = -1;
#endif
    other.data_ = nullptr;
    other.size_ = 0;
  }
  return *this;
}

#ifdef _WIN32

bool MmapFile::Open(const char* filename, std::string* err) {
  Close();

  // Open file
  file_handle_ = CreateFileA(
      filename,
      GENERIC_READ,
      FILE_SHARE_READ,
      nullptr,
      OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL,
      nullptr);

  if (file_handle_ == INVALID_HANDLE_VALUE) {
    if (err) {
      *err = "Failed to open file: " + std::string(filename);
    }
    file_handle_ = nullptr;
    return false;
  }

  // Get file size
  LARGE_INTEGER file_size;
  if (!GetFileSizeEx(file_handle_, &file_size)) {
    if (err) {
      *err = "Failed to get file size";
    }
    CloseHandle(file_handle_);
    file_handle_ = nullptr;
    return false;
  }

  size_ = static_cast<size_t>(file_size.QuadPart);

  if (size_ == 0) {
    // Empty file - no need to mmap
    CloseHandle(file_handle_);
    file_handle_ = nullptr;
    filename_ = filename;
    return true;
  }

  // Create file mapping
  mapping_handle_ = CreateFileMappingA(
      file_handle_,
      nullptr,
      PAGE_READONLY,
      0, 0,
      nullptr);

  if (!mapping_handle_) {
    if (err) {
      *err = "Failed to create file mapping";
    }
    CloseHandle(file_handle_);
    file_handle_ = nullptr;
    return false;
  }

  // Map view
  data_ = static_cast<uint8_t*>(MapViewOfFile(
      mapping_handle_,
      FILE_MAP_READ,
      0, 0, 0));

  if (!data_) {
    if (err) {
      *err = "Failed to map file view";
    }
    CloseHandle(mapping_handle_);
    CloseHandle(file_handle_);
    mapping_handle_ = nullptr;
    file_handle_ = nullptr;
    return false;
  }

  filename_ = filename;
  return true;
}

void MmapFile::Close() {
  if (data_) {
    UnmapViewOfFile(data_);
    data_ = nullptr;
  }
  if (mapping_handle_) {
    CloseHandle(mapping_handle_);
    mapping_handle_ = nullptr;
  }
  if (file_handle_) {
    CloseHandle(file_handle_);
    file_handle_ = nullptr;
  }
  size_ = 0;
  filename_.clear();
}

#else  // POSIX

bool MmapFile::Open(const char* filename, std::string* err) {
  Close();

  // Open file
  fd_ = ::open(filename, O_RDONLY);
  if (fd_ < 0) {
    if (err) {
      *err = "Failed to open file: " + std::string(filename) + " (" + strerror(errno) + ")";
    }
    return false;
  }

  // Get file size
  struct stat st;
  if (fstat(fd_, &st) < 0) {
    if (err) {
      *err = "Failed to stat file: " + std::string(strerror(errno));
    }
    ::close(fd_);
    fd_ = -1;
    return false;
  }

  size_ = static_cast<size_t>(st.st_size);

  if (size_ == 0) {
    // Empty file - no need to mmap
    ::close(fd_);
    fd_ = -1;
    filename_ = filename;
    return true;
  }

  // Map file
  void* mapped = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);
  if (mapped == MAP_FAILED) {
    if (err) {
      *err = "Failed to mmap file: " + std::string(strerror(errno));
    }
    ::close(fd_);
    fd_ = -1;
    size_ = 0;
    return false;
  }

  data_ = static_cast<uint8_t*>(mapped);
  filename_ = filename;

  // Hint to kernel that we'll read sequentially
  #ifdef MADV_SEQUENTIAL
  madvise(data_, size_, MADV_SEQUENTIAL);
  #endif

  return true;
}

void MmapFile::Close() {
  if (data_ && size_ > 0) {
    ::munmap(data_, size_);
    data_ = nullptr;
  }
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
  }
  size_ = 0;
  filename_.clear();
}

#endif  // _WIN32

}  // namespace next
}  // namespace tinyusdz
