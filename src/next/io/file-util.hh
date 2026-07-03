// SPDX-License-Identifier: Apache-2.0
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// TinyUSDZ Next - filesystem utility (the "next" file-I/O helper).
//
// This is a self-contained port of the filesystem essentials from the legacy
// src/io-util.cc: memory-mapping, whole-file read/write, existence checks, and
// pure path helpers. It is the ONLY place in next that performs raw filesystem
// I/O (mmap / fopen / ifstream). next-core proper is memory-only (freestanding);
// this utility is compiled separately (the `next_io` target) and is NOT part of
// the WASM / TINYUSDZ_NEXT_CORE_MINIMAL build. Tools mmap a file through this
// helper and feed the bytes to the memory-based Load*/Write* core API.
//
// Namespace tinyusdz::next::io (distinct from the legacy tinyusdz::io).

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tinyusdz {
namespace next {
namespace io {

// Memory-mapped file handle. NOTE: gate on `_WIN32` (predefined by the compiler
// on Windows), NOT bare `WIN32` (only defined once <windows.h> is included) --
// using `WIN32` made this struct's layout differ between TUs that include
// <windows.h> and those that don't, corrupting `addr` in the caller.
struct MMapFileHandle {
  std::string filename;
#if defined(_WIN32)
  std::wstring unicode_filename;
  void *hFile = nullptr;
#endif
  bool writable = false;
  uint8_t *addr = nullptr;
  uint64_t size = 0;
};

/// True when this platform/build supports mmap.
bool IsMMapSupported();

/// Memory-map `filepath` (UTF-8) read-only (writable=false) or read-write.
/// Returns false when the file is missing/empty/invalid or mmap is unavailable;
/// `err` receives a message. On success the mapping lives until UnmapFile().
bool MMapFile(const std::string &filepath, MMapFileHandle *handle, bool writable,
              std::string *err);
#if defined(_WIN32)
bool MMapFile(const std::wstring &filepath, MMapFileHandle *handle,
              bool writable, std::string *err);
#endif

/// Release a mapping obtained from MMapFile.
bool UnmapFile(const MMapFileHandle &handle, std::string *err);

/// Read the whole file into `*out`. `filesize_max` (0 = unlimited) rejects
/// oversized files. Returns false and sets `err` on any failure.
bool ReadWholeFile(std::vector<uint8_t> *out, std::string *err,
                   const std::string &filepath, size_t filesize_max = 0);

/// Read up to `max_read_bytes` from the start of the file (for magic sniffing).
bool ReadFileHeader(std::vector<uint8_t> *out, std::string *err,
                    const std::string &filepath, size_t max_read_bytes = 128);

/// Write `content_bytes` bytes to `filepath` (creates/truncates).
bool WriteWholeFile(const std::string &filepath, const unsigned char *contents,
                    size_t content_bytes, std::string *err);

/// True if a regular file exists at `filepath`.
bool FileExists(const std::string &filepath);

/// Current working directory (".", if unavailable).
std::string GetCurrentDir();

// --- pure path helpers (no filesystem access) ------------------------------
std::string GetBaseDir(const std::string &filepath);
std::string GetFileExtension(const std::string &filepath);
std::string GetBaseFilename(const std::string &filepath);
bool IsAbsPath(const std::string &filepath);
std::string JoinPath(const std::string &dir, const std::string &filename);
std::string NormalizePath(const std::string &path);

}  // namespace io
}  // namespace next
}  // namespace tinyusdz
