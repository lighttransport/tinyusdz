// SPDX-License-Identifier: Apache-2.0
// Copyright 2022-2023 Syoyo Fujita.
// Copyright 2024-Present Light Transport Entertainment Inc.
//
// Self-contained port of the filesystem essentials from legacy src/io-util.cc
// into tinyusdz::next::io. mmap + whole-file read/write + existence + path
// helpers. No dependency on any legacy tinyusdz header (the two str-util inlines
// used by JoinPath are copied in below).

#include "file-util.hh"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <limits>

#ifdef _WIN32
#ifdef _MSC_VER
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <io.h>
#include <windows.h>
#ifndef TINYUSDZ_NEXT_IO_MMAP_SUPPORTED
#define TINYUSDZ_NEXT_IO_MMAP_SUPPORTED (1)
#endif
#ifdef _MSC_VER
#undef NOMINMAX
#endif
#undef WIN32_LEAN_AND_MEAN
#if defined(__GLIBCXX__)  // mingw
#include <fcntl.h>
#include <ext/stdio_filebuf.h>
#endif
#else  // !_WIN32
#if defined(TINYUSDZ_BUILD_IOS) || defined(TARGET_OS_IPHONE) ||   \
    defined(TARGET_IPHONE_SIMULATOR) || defined(__ANDROID__) ||   \
    defined(__EMSCRIPTEN__) || defined(__wasi__)
// non-posix: no mmap
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#ifndef TINYUSDZ_NEXT_IO_MMAP_SUPPORTED
#define TINYUSDZ_NEXT_IO_MMAP_SUPPORTED (1)
#endif
#endif
#endif  // _WIN32

#ifndef TINYUSDZ_NEXT_IO_MMAP_SUPPORTED
#define TINYUSDZ_NEXT_IO_MMAP_SUPPORTED (0)
#endif

#if !defined(_WIN32)
#include <unistd.h>  // getcwd
#endif

namespace tinyusdz {
namespace next {
namespace io {

namespace {

// Copied from legacy str-util.hh (the only JoinPath dependency); kept local so
// this utility is self-contained.
inline bool startsWith(const std::string &str, const std::string &t) {
  return (str.size() >= t.size()) &&
         std::equal(std::begin(t), std::end(t), std::begin(str));
}
inline std::string removePrefix(const std::string &str,
                                const std::string &prefix) {
  if (startsWith(str, prefix)) return str.substr(prefix.length());
  return str;
}

#if defined(_WIN32)
static std::string FormatWinErr(DWORD err) {
  LPSTR buf = nullptr;
  size_t size = FormatMessageA(
      FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
          FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      reinterpret_cast<LPSTR>(&buf), 0, nullptr);
  if (!size) return "FormatMessageA failed";
  std::string ret(buf, size);
  LocalFree(buf);
  return ret;
}

static std::wstring UTF8ToWchar(const std::string &str) {
  int n = MultiByteToWideChar(CP_UTF8, 0, str.data(), int(str.size()), nullptr, 0);
  std::wstring wstr(size_t(n), 0);
  MultiByteToWideChar(CP_UTF8, 0, str.data(), int(str.size()), &wstr[0],
                      int(wstr.size()));
  return wstr;
}
static std::string WcharToUTF8(const std::wstring &wstr) {
  int n = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), int(wstr.size()), nullptr,
                              0, nullptr, nullptr);
  std::string str(size_t(n), 0);
  WideCharToMultiByte(CP_UTF8, 0, wstr.data(), int(wstr.size()), &str[0],
                      int(str.size()), nullptr, nullptr);
  return str;
}

static bool MMapFileImplWin32(HANDLE hFile, MMapFileHandle *handle,
                              bool writable, std::string *err) {
  uint64_t size = 0;
  LARGE_INTEGER sz{};
  if (!GetFileSizeEx(hFile, &sz)) {
    if (err) *err += "GetFileSizeEx failed: " + FormatWinErr(GetLastError());
    return false;
  }
  size = static_cast<uint64_t>(sz.QuadPart);

  HANDLE hMapping = CreateFileMapping(
      hFile, nullptr, writable ? PAGE_READWRITE : PAGE_READONLY, 0, 0, nullptr);
  if (hMapping == nullptr) {
    if (err) *err += "CreateFileMapping failed: " + FormatWinErr(GetLastError());
    return false;
  }
  void *addr = MapViewOfFile(hMapping, FILE_MAP_READ, 0, 0, 0);
  DWORD lastError = GetLastError();
  CloseHandle(hMapping);
  if (!addr) {
    if (err) *err += "MapViewOfFile failed: " + FormatWinErr(lastError);
    return false;
  }
  handle->addr = reinterpret_cast<uint8_t *>(addr);
  handle->size = size;
  handle->writable = writable;
  return true;
}
#endif  // _WIN32

}  // namespace

bool IsMMapSupported() {
#if TINYUSDZ_NEXT_IO_MMAP_SUPPORTED
  return true;
#else
  return false;
#endif
}

bool MMapFile(const std::string &filepath, MMapFileHandle *handle, bool writable,
              std::string *err) {
#if TINYUSDZ_NEXT_IO_MMAP_SUPPORTED
#if defined(_WIN32)
  std::wstring unicode_filepath = UTF8ToWchar(filepath);
  HANDLE hFile =
      CreateFileW(unicode_filepath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    if (err) *err += "Failed to open file.";
    return false;
  }
  if (!MMapFileImplWin32(hFile, handle, writable, err)) {
    CloseHandle(hFile);
    return false;
  }
  handle->hFile = hFile;
  handle->filename = filepath;
  handle->unicode_filename = unicode_filepath;
  return true;
#else  // POSIX
  FILE *fp = fopen(filepath.c_str(), writable ? "rw" : "r");
  if (!fp) {
    if (err) *err += "fopen failed.";
    return false;
  }
  if (std::fseek(fp, 0, SEEK_END) != 0) {
    if (err) *err += "Failed to fseek.";
    fclose(fp);
    return false;
  }
  size_t size = size_t(std::ftell(fp));
  std::fseek(fp, 0, SEEK_SET);
  if (size == 0) {
    if (err) *err += "File size is zero.";
    fclose(fp);
    return false;
  }
  int fd = fileno(fp);
  void *addr = mmap(nullptr, size, writable ? PROT_READ | PROT_WRITE : PROT_READ,
                    MAP_PRIVATE, fd, 0);
  if (addr == MAP_FAILED) {
    if (err) *err += "mmap failed.";
    fclose(fp);
    return false;
  }
  handle->addr = reinterpret_cast<uint8_t *>(addr);
  handle->size = uint64_t(size);
  handle->writable = writable;
  handle->filename = filepath;
  fclose(fp);  // mapping survives fclose
  return true;
#endif
#else   // !MMAP_SUPPORTED
  (void)filepath;
  (void)handle;
  (void)writable;
  if (err) *err += "mmap is not supported on this platform.";
  return false;
#endif
}

#if defined(_WIN32)
bool MMapFile(const std::wstring &unicode_filepath, MMapFileHandle *handle,
              bool writable, std::string *err) {
#if TINYUSDZ_NEXT_IO_MMAP_SUPPORTED
  HANDLE hFile =
      CreateFileW(unicode_filepath.c_str(), GENERIC_READ, FILE_SHARE_READ,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hFile == INVALID_HANDLE_VALUE) {
    if (err) *err += "Failed to open file.";
    return false;
  }
  if (!MMapFileImplWin32(hFile, handle, writable, err)) {
    CloseHandle(hFile);
    return false;
  }
  handle->hFile = hFile;
  handle->filename = WcharToUTF8(unicode_filepath);
  handle->unicode_filename = unicode_filepath;
  return true;
#else
  (void)unicode_filepath;
  (void)handle;
  (void)writable;
  (void)err;
  return false;
#endif
}
#endif

bool UnmapFile(const MMapFileHandle &handle, std::string *err) {
#if TINYUSDZ_NEXT_IO_MMAP_SUPPORTED
#if defined(_WIN32)
  bool result = true;
  if (handle.addr && handle.size) {
    if (!UnmapViewOfFile(handle.addr)) {
      if (err) *err += "warning: UnmapViewOfFile failed.";
    }
  } else {
    result = false;
  }
  if (handle.hFile != nullptr) {
    if (!CloseHandle(handle.hFile)) {
      if (err) *err += "CloseHandle failed.";
      result = false;
    }
  }
  return result;
#else  // POSIX
  if (handle.addr && handle.size) {
    if (munmap(reinterpret_cast<void *>(handle.addr), size_t(handle.size)) != 0) {
      if (err) *err += "warning: munmap failed.";
    }
    return true;
  }
  return false;
#endif
#else
  (void)handle;
  (void)err;
  return false;
#endif
}

bool ReadWholeFile(std::vector<uint8_t> *out, std::string *err,
                   const std::string &filepath, size_t filesize_max) {
#if defined(_WIN32) && defined(__GLIBCXX__)  // mingw
  int fd = _wopen(UTF8ToWchar(filepath).c_str(), _O_RDONLY | _O_BINARY);
  __gnu_cxx::stdio_filebuf<char> wfile_buf(fd, std::ios_base::in);
  std::istream f(&wfile_buf);
#elif defined(_WIN32) && (defined(_MSC_VER) || defined(_LIBCPP_VERSION))
  std::ifstream f(UTF8ToWchar(filepath).c_str(), std::ifstream::binary);
#else
  std::ifstream f(filepath.c_str(), std::ifstream::binary);
#endif
  if (!f) {
    if (err) *err += "File open error : " + filepath + "\n";
    return false;
  }
  (void)f.peek();  // directories/pipes fail here on posix gnustl/libc++
  if (!f) {
    if (err) *err += "File read error (empty or invalid) : " + filepath + "\n";
    return false;
  }
  f.seekg(0, f.end);
  size_t sz = static_cast<size_t>(f.tellg());
  f.seekg(0, f.beg);
  if (int64_t(sz) < 0) {
    if (err) *err += "Invalid file size (a directory?) : " + filepath + "\n";
    return false;
  } else if (sz == 0) {
    if (err) *err += "File is empty : " + filepath + "\n";
    return false;
  } else if (uint64_t(sz) >= uint64_t((std::numeric_limits<int64_t>::max)())) {
    if (err) *err += "Invalid file (pipe or special device?) : " + filepath + "\n";
    return false;
  }
  if ((filesize_max > 0) && (sz > filesize_max)) {
    if (err) {
      *err += "File size is too large : " + filepath +
              " sz = " + std::to_string(sz) +
              ", allowed max = " + std::to_string(filesize_max) + "\n";
    }
    return false;
  }
  out->resize(sz);
  f.read(reinterpret_cast<char *>(out->data()),
         static_cast<std::streamsize>(sz));
  if (!f) {
    if (err) *err += "Failed to read file : " + filepath + "\n";
    return false;
  }
  return true;
}

bool ReadFileHeader(std::vector<uint8_t> *out, std::string *err,
                    const std::string &filepath, size_t max_read_bytes) {
  std::ifstream f(filepath.c_str(), std::ifstream::binary);
  if (!f) {
    if (err) *err += "File open error : " + filepath + "\n";
    return false;
  }
  out->resize(max_read_bytes);
  f.read(reinterpret_cast<char *>(out->data()),
         static_cast<std::streamsize>(max_read_bytes));
  out->resize(static_cast<size_t>(f.gcount()));
  return true;
}

bool WriteWholeFile(const std::string &filepath, const unsigned char *contents,
                    size_t content_bytes, std::string *err) {
#if defined(_WIN32) && defined(__GLIBCXX__)  // mingw
  int fd = _wopen(UTF8ToWchar(filepath).c_str(),
                  _O_CREAT | _O_WRONLY | _O_TRUNC | _O_BINARY);
  __gnu_cxx::stdio_filebuf<char> wfile_buf(
      fd, std::ios_base::out | std::ios_base::binary);
  std::ostream f(&wfile_buf);
#elif defined(_WIN32) && (defined(_MSC_VER) || defined(_LIBCPP_VERSION))
  std::ofstream f(UTF8ToWchar(filepath).c_str(), std::ofstream::binary);
#else
  std::ofstream f(filepath.c_str(), std::ofstream::binary);
#endif
  if (!f) {
    if (err) *err += "File open error for writing : " + filepath + "\n";
    return false;
  }
  f.write(reinterpret_cast<const char *>(contents),
          static_cast<std::streamsize>(content_bytes));
  if (!f) {
    if (err) *err += "File write error : " + filepath + "\n";
    return false;
  }
  return true;
}

bool FileExists(const std::string &filepath) {
#if defined(_WIN32)
  FILE *fp = nullptr;
  if (_wfopen_s(&fp, UTF8ToWchar(filepath).c_str(), L"rb") != 0) return false;
#else
  FILE *fp = fopen(filepath.c_str(), "rb");
#endif
  if (fp) {
    fclose(fp);
    return true;
  }
  return false;
}

std::string GetCurrentDir() {
#if defined(_WIN32)
  wchar_t buf[MAX_PATH] = {};
  if (_wgetcwd(buf, MAX_PATH)) return WcharToUTF8(buf);
  return ".";
#elif defined(__EMSCRIPTEN__) || defined(__wasi__)
  return ".";
#else
  char buf[4096] = {};
  if (getcwd(buf, sizeof(buf))) return std::string(buf);
  return ".";
#endif
}

std::string GetBaseDir(const std::string &filepath) {
  if (filepath.find_last_of("/\\") != std::string::npos)
    return filepath.substr(0, filepath.find_last_of("/\\"));
  return "";
}

std::string GetFileExtension(const std::string &filename) {
  if (filename.find_last_of(".") != std::string::npos)
    return filename.substr(filename.find_last_of(".") + 1);
  return "";
}

std::string GetBaseFilename(const std::string &filepath) {
  auto idx = filepath.find_last_of("/\\");
  if (idx != std::string::npos) return filepath.substr(idx + 1);
  return filepath;
}

bool IsAbsPath(const std::string &filename) {
  if (filename.size() > 0 && filename[0] == '/') return true;
  if (filename.size() > 2 && filename[0] == '\\' && filename[1] == '\\')
    return true;  // UNC
  if (filename.size() > 2) {
    const char c = filename[0];
    const bool is_drive = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
    if (is_drive && filename[1] == ':' &&
        (filename[2] == '/' || filename[2] == '\\'))
      return true;  // Windows drive
  }
  return false;
}

std::string JoinPath(const std::string &dir, const std::string &filename) {
  if (dir.empty()) return filename;
  const char lastChar = *dir.rbegin();
  const std::string basedir = (lastChar != '/') ? dir + "/" : dir;
  if (startsWith(filename, "./")) return basedir + removePrefix(filename, "./");
  return basedir + filename;
}

std::string NormalizePath(const std::string &path) {
  if (path.empty()) return path;
  const bool absolute = (path[0] == '/');
  std::vector<std::string> out;
  size_t i = 0;
  const size_t n = path.size();
  while (i < n) {
    size_t j = path.find('/', i);
    if (j == std::string::npos) j = n;
    const std::string seg = path.substr(i, j - i);
    i = j + 1;
    if (seg.empty() || seg == ".") {
      // collapse
    } else if (seg == "..") {
      if (!out.empty() && out.back() != "..") {
        out.pop_back();
      } else if (!absolute) {
        out.push_back("..");
      }
    } else {
      out.push_back(seg);
    }
  }
  std::string result = absolute ? "/" : std::string();
  for (size_t k = 0; k < out.size(); k++) {
    if (k) result += "/";
    result += out[k];
  }
  if (result.empty()) result = absolute ? "/" : ".";
  return result;
}

}  // namespace io
}  // namespace next
}  // namespace tinyusdz
