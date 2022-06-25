#pragma once

#include <cstddef>
#include <string>
#include <vector>

#ifdef TINYUSDZ_ANDROID_LOAD_FROM_ASSETS
#include <android/asset_manager.h>
#endif

namespace tinyusdz {
namespace io {

#ifdef TINYUSDZ_ANDROID_LOAD_FROM_ASSETS
extern AAssetManager *asset_manager;
#endif

#ifdef _WIN32
std::wstring UTF8ToWchar(const std::string &str);
std::string WcharToUTF8(const std::wstring &wstr);
#endif

std::string ExpandFilePath(const std::string &filepath,
                           void *userdata = nullptr);

bool ReadWholeFile(std::vector<uint8_t> *out, std::string *err,
                   const std::string &filepath, size_t filesize_max = 0,
                   void *userdata = nullptr);

std::string GetBaseDir(const std::string &filepath);
std::string JoinPath(const std::string &dir, const std::string &filename);
bool IsAbsPath(const std::string &filepath);

bool IsUDIMPath(const std::string &filepath);

//
// diffuse.<UDIM>.png => "diffuse.", ".png"
//
bool SplitUDIMPath(const std::string &filepath, std::string *pre,
                   std::string *post);

}  // namespace io
}  // namespace tinyusdz
