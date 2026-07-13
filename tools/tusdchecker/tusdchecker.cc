// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Light Transport Entertainment Inc.
//
// tusdchecker - dependency-free AOUSD Core and USD schema validator.

#include <algorithm>
#include <array>
#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

#include "next/composition/composition.hh"
#include "next/crate/crate-reader.hh"
#include "next/pcp/layer-registry.hh"
#include "next/reader/usdz-reader.hh"
#include "next/resolver/asset-resolver.hh"
#include "next/validation/usd-validation.hh"

namespace {

using tinyusdz::next::GetAOUSDCoreSpecVersionString;
using tinyusdz::next::GetOrderedValidationIssues;
using tinyusdz::next::GetValidationGroupNames;
using tinyusdz::next::Layer;
using tinyusdz::next::USDValidationIssue;
using tinyusdz::next::USDValidationResult;
using tinyusdz::next::USDValidationSeverity;
using tinyusdz::next::ValidationOptions;

constexpr int kExitValid = 0;
constexpr int kExitInvalid = 1;
constexpr int kExitError = 2;
constexpr size_t kDefaultMaxMemoryMb = 1024;

struct Args {
  std::string input;
  std::string output = "stdout";
  ValidationOptions groups;
  size_t max_memory_mb = kDefaultMaxMemoryMb;
  bool json = false;
  bool strict = false;
  bool strict_parse = false;
  bool composed = false;
  bool require_all_groups = false;
};

ValidationOptions AllAvailableGroups() {
  return tinyusdz::next::MakeValidateAllOptions();
}

void PrintUsage(std::ostream& os) {
  os << "Usage: tusdchecker [options] FILE\n"
        "\n"
        "Validate USDA, USDC, USDZ, or MaterialX (.mtlx) against AOUSD Core\n"
        "1.0.1 and TinyUSDZ's structural schema rules. FILE may be '-' for\n"
        "stdin.\n"
        "\n"
        "Options:\n"
        "  -g, --groups LIST     Comma-separated rule groups: "
        "core,geom,shade,\n"
        "                        lux,physics,render,package,crate,arkit (default: "
        "all\n"
        "                        defect-class groups; arkit is opt-in)\n"
        "      --core-only       Run AOUSD Core rules only\n"
        "      --all             Run all defect-class rule groups (default)\n"
        "      --arkit           ARKit/RealityKit USDZ profile "
        "(arkit + core,\n"
        "                        geom,shade,package), like usdchecker "
        "--arkit\n"
        "  -t, --strict          Treat validation and parser warnings as "
        "failure\n"
        "      --strict-parse    Reject non-conforming/unsupported format "
        "data\n"
        "      --composed        Compose external arcs and validate the "
        "flattened stage\n"
        "      --require-all-groups\n"
        "                        Fail if a requested group is inapplicable\n"
        "      --json            Emit stable machine-readable JSON\n"
        "  -o, --out FILE        Write report to FILE, stdout, or stderr\n"
        "      --max-memory-mb N Bound input/parser memory (default: 1024)\n"
        "  -h, --help            Show this help\n"
        "      --version         Show validator/spec version\n"
        "      --list-groups     List rule groups and their coverage\n"
        "\n"
        "Exit status: 0 valid, 1 validation failed, 2 usage/I/O/parse error.\n";
}

bool ParseSize(const std::string& text, size_t* value) {
  if (!value || text.empty() || text[0] == '-') return false;
  errno = 0;
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
  if (errno != 0 || !end || *end != '\0' || parsed == 0 ||
      parsed > std::numeric_limits<size_t>::max()) {
    return false;
  }
  *value = static_cast<size_t>(parsed);
  return true;
}

std::vector<std::string> Split(const std::string& text, char separator) {
  std::vector<std::string> values;
  size_t start = 0;
  while (start <= text.size()) {
    const size_t end = text.find(separator, start);
    values.emplace_back(text.substr(
        start, end == std::string::npos ? std::string::npos : end - start));
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return values;
}

bool SetGroups(const std::string& text, ValidationOptions* groups,
               std::string* error) {
  if (!groups) return false;
  *groups = ValidationOptions();
  groups->core = false;
  for (const std::string& name : Split(text, ',')) {
    bool* flag = nullptr;
    if (name == "core")
      flag = &groups->core;
    else if (name == "geom")
      flag = &groups->geom;
    else if (name == "shade")
      flag = &groups->shade;
    else if (name == "lux")
      flag = &groups->lux;
    else if (name == "physics")
      flag = &groups->physics;
    else if (name == "render")
      flag = &groups->render;
    else if (name == "package")
      flag = &groups->package;
    else if (name == "crate")
      flag = &groups->crate;
    else if (name == "arkit")
      flag = &groups->arkit;
    else {
      if (error) *error = "unknown or empty validation group: '" + name + "'";
      return false;
    }
    *flag = true;
  }
  return true;
}

enum class ParseArgsResult { Run, ExitSuccess, Error };

ParseArgsResult ParseArgs(int argc, char** argv, Args* args,
                          std::string* error) {
  if (!args) return ParseArgsResult::Error;
  for (int i = 1; i < argc; ++i) {
    const std::string arg(argv[i]);
    const auto next_value = [&](const char* option, std::string* value) {
      if (i + 1 >= argc) {
        if (error) *error = std::string("missing value for ") + option;
        return false;
      }
      *value = argv[++i];
      return true;
    };
    if (arg == "-h" || arg == "--help") {
      PrintUsage(std::cout);
      return ParseArgsResult::ExitSuccess;
    } else if (arg == "--version") {
      std::cout << "tusdchecker 0.1 (" << GetAOUSDCoreSpecVersionString()
                << ")\n";
      return ParseArgsResult::ExitSuccess;
    } else if (arg == "--list-groups") {
      std::cout
          << "core      AOUSD layer, composition, metadata, and value rules\n"
          << "geom      UsdGeom and UsdSkel structural rules\n"
          << "shade     UsdShade, preview-surface, and MaterialX rules\n"
          << "lux       UsdLux structural and value rules\n"
          << "physics   UsdPhysics placement, joint, and extension rules\n"
          << "render    UsdRender settings/product/var structural rules\n"
          << "package   USDZ layout, path, portability, and dependency rules\n"
          << "crate     USDC decode and cross-table structural rules\n"
          << "arkit     ARKit/RealityKit USDZ delivery profile (opt-in; not "
             "part of --all)\n";
      return ParseArgsResult::ExitSuccess;
    } else if (arg == "--json") {
      args->json = true;
    } else if (arg == "-t" || arg == "--strict") {
      args->strict = true;
    } else if (arg == "--strict-parse") {
      args->strict_parse = true;
    } else if (arg == "--composed") {
      args->composed = true;
    } else if (arg == "--require-all-groups") {
      args->require_all_groups = true;
    } else if (arg == "--core-only") {
      args->groups = ValidationOptions();
    } else if (arg == "--all") {
      args->groups = AllAvailableGroups();
    } else if (arg == "--arkit") {
      // The ARKit profile: the ARKit-only rules plus the base rule groups that
      // OpenUSD's `usdchecker --arkit` always runs (stage metadata, prim
      // encapsulation, textures, material bindings, package layout).
      args->groups.arkit = true;
      args->groups.core = true;
      args->groups.geom = true;
      args->groups.shade = true;
      args->groups.package = true;
    } else if (arg == "-g" || arg == "--groups") {
      std::string value;
      if (!next_value(arg.c_str(), &value) ||
          !SetGroups(value, &args->groups, error)) {
        return ParseArgsResult::Error;
      }
    } else if (arg == "-o" || arg == "--out") {
      if (!next_value(arg.c_str(), &args->output)) {
        return ParseArgsResult::Error;
      }
    } else if (arg == "--max-memory-mb") {
      std::string value;
      if (!next_value(arg.c_str(), &value) ||
          !ParseSize(value, &args->max_memory_mb)) {
        if (error && error->empty()) {
          *error = "--max-memory-mb requires a positive integer";
        }
        return ParseArgsResult::Error;
      }
    } else if (!arg.empty() && arg[0] == '-' && arg != "-") {
      if (error) *error = "unknown option: " + arg;
      return ParseArgsResult::Error;
    } else if (!args->input.empty()) {
      if (error) *error = "only one input file may be specified";
      return ParseArgsResult::Error;
    } else {
      args->input = arg;
    }
  }
  if (args->input.empty()) {
    if (error) *error = "an input FILE is required";
    return ParseArgsResult::Error;
  }
  // The ARKit profile's package-layout rules (arkit.package.fileExtension /
  // .rootLayer) live in the package container check, which only runs when the
  // package group reads the raw bytes. Selecting `arkit` therefore implies
  // `package`, so `-g arkit` on a .usdz enforces the ARKit package rules rather
  // than silently skipping them while still reporting arkit as checked. (The
  // `--arkit` flag already sets this explicitly.)
  if (args->groups.arkit) args->groups.package = true;
  return ParseArgsResult::Run;
}

std::string JsonEscape(const std::string& text) {
  static const char kHex[] = "0123456789abcdef";
  std::string out;
  out.reserve(text.size() + 8);
  for (unsigned char c : text) {
    switch (c) {
      case '\"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20) {
          out += "\\u00";
          out.push_back(kHex[(c >> 4) & 0xf]);
          out.push_back(kHex[c & 0xf]);
        } else {
          out.push_back(static_cast<char>(c));
        }
    }
  }
  return out;
}

void WriteJsonString(std::ostream& os, const std::string& value) {
  os << '\"' << JsonEscape(value) << '\"';
}

std::string JsonReport(const Args& args, const USDValidationResult& result,
                       const std::string& parser_warnings, bool valid) {
  std::ostringstream os;
  os << "{\n  \"tool\":\"tusdchecker\",\n  \"spec\":";
  WriteJsonString(os, GetAOUSDCoreSpecVersionString());
  os << ",\n  \"input\":";
  WriteJsonString(os, args.input);
  os << ",\n  \"valid\":" << (valid ? "true" : "false")
     << ",\n  \"strict\":" << (args.strict ? "true" : "false")
     << ",\n  \"strictParse\":" << (args.strict_parse ? "true" : "false")
     << ",\n  \"composed\":" << (args.composed ? "true" : "false")
     << ",\n  \"requireAllGroups\":"
     << (args.require_all_groups ? "true" : "false")
     << ",\n  \"requestedGroups\":[";
  const std::vector<std::string> requested =
      GetValidationGroupNames(args.groups);
  for (size_t i = 0; i < requested.size(); ++i) {
    if (i) os << ',';
    WriteJsonString(os, requested[i]);
  }
  os << "],\n  \"checkedGroups\":[";
  const std::vector<std::string> groups =
      GetValidationGroupNames(result.checked_groups);
  for (size_t i = 0; i < groups.size(); ++i) {
    if (i) os << ',';
    WriteJsonString(os, groups[i]);
  }
  os << "],\n  \"skippedGroups\":[";
  size_t skipped_count = 0;
  for (const std::string& name : requested) {
    if (std::find(groups.begin(), groups.end(), name) != groups.end()) continue;
    if (skipped_count++) os << ',';
    WriteJsonString(os, name);
  }
  os << "],\n  \"errorCount\":" << result.error_count()
     << ",\n  \"warningCount\":" << result.warning_count()
     << ",\n  \"parserWarnings\":";
  WriteJsonString(os, parser_warnings);
  os << ",\n  \"issues\":[";
  const auto issues = GetOrderedValidationIssues(result);
  for (size_t i = 0; i < issues.size(); ++i) {
    const USDValidationIssue& issue = *issues[i];
    if (i) os << ',';
    os << "\n    {\"severity\":";
    WriteJsonString(os, issue.severity == USDValidationSeverity::Error
                            ? "error"
                            : "warning");
    os << ",\"ruleId\":";
    WriteJsonString(os, issue.rule_id);
    os << ",\"location\":";
    WriteJsonString(os, issue.location);
    os << ",\"message\":";
    WriteJsonString(os, issue.message);
    os << '}';
  }
  if (!issues.empty()) os << '\n';
  os << "  ]\n}\n";
  return os.str();
}

bool ReadStdin(size_t limit, std::string* data, std::string* error) {
  if (!data) return false;
  char buffer[64 * 1024];
  while (std::cin) {
    std::cin.read(buffer, sizeof(buffer));
    const std::streamsize count = std::cin.gcount();
    if (count > 0) {
      const size_t n = static_cast<size_t>(count);
      if (data->size() > limit || n > limit - data->size()) {
        if (error) *error = "stdin exceeds --max-memory-mb limit";
        return false;
      }
      data->append(buffer, n);
    }
  }
  if (!std::cin.eof()) {
    if (error) *error = "failed while reading stdin";
    return false;
  }
  return true;
}

bool ReadFile(const std::string& filename, size_t limit, std::string* data,
              std::string* error) {
  if (!data) return false;
  std::ifstream stream(filename, std::ios::binary | std::ios::ate);
  if (!stream) {
    if (error) *error = "cannot open input file '" + filename + "'";
    return false;
  }
  const std::streampos end = stream.tellg();
  if (end < std::streampos(0) || static_cast<uint64_t>(end) > limit) {
    if (error) *error = "input exceeds --max-memory-mb limit";
    return false;
  }
  data->resize(static_cast<size_t>(end));
  stream.seekg(0);
  if (!data->empty() &&
      !stream.read(&(*data)[0], static_cast<std::streamsize>(data->size()))) {
    if (error) *error = "failed while reading input file";
    return false;
  }
  return true;
}

void AddIssue(USDValidationResult* result, USDValidationSeverity severity,
              const std::string& rule, const std::string& location,
              const std::string& message) {
  if (!result) return;
  USDValidationIssue issue;
  issue.severity = severity;
  issue.rule_id = rule;
  issue.location = location;
  issue.message = message;
  result->issues.push_back(std::move(issue));
}

bool ReadU16(const uint8_t* bytes, size_t size, size_t pos, uint16_t* out) {
  if (!out || pos > size || size - pos < 2) return false;
  *out = static_cast<uint16_t>(bytes[pos]) |
         static_cast<uint16_t>(bytes[pos + 1] << 8);
  return true;
}

bool ReadU32(const uint8_t* bytes, size_t size, size_t pos, uint32_t* out) {
  if (!out || pos > size || size - pos < 4) return false;
  *out = static_cast<uint32_t>(bytes[pos]) |
         (static_cast<uint32_t>(bytes[pos + 1]) << 8) |
         (static_cast<uint32_t>(bytes[pos + 2]) << 16) |
         (static_cast<uint32_t>(bytes[pos + 3]) << 24);
  return true;
}

struct PackageEntry {
  std::string name;
  size_t local_header_offset = 0;
  size_t data_offset = 0;
  size_t size = 0;
  size_t uncompressed_size = 0;
  uint32_t crc = 0;
  uint16_t flags = 0;
  uint16_t compression = 0;
};

uint32_t ComputeCRC32(const uint8_t* data, size_t size) {
  static const std::array<uint32_t, 256> table = [] {
    std::array<uint32_t, 256> values{};
    for (uint32_t i = 0; i < values.size(); ++i) {
      uint32_t value = i;
      for (int bit = 0; bit < 8; ++bit) {
        value = (value & 1u) ? 0xedb88320u ^ (value >> 1) : value >> 1;
      }
      values[i] = value;
    }
    return values;
  }();
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    crc = table[(crc ^ data[i]) & 0xffu] ^ (crc >> 8);
  }
  return crc ^ 0xffffffffu;
}

std::string LowerExtension(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const size_t dot = path.find_last_of('.');
  if (dot == std::string::npos ||
      (slash != std::string::npos && dot < slash)) {
    return std::string();
  }
  std::string ext = path.substr(dot + 1);
  std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return ext;
}

bool IsUnsafePackagePath(const std::string& path) {
  if (path.empty() || path[0] == '/' || path[0] == '\\' ||
      path.find('\\') != std::string::npos) {
    return true;
  }
  for (const std::string& part : Split(path, '/')) {
    if (part.empty() || part == "." || part == "..") return true;
  }
  return false;
}

std::string NormalizePackagePath(const std::string& root_name,
                                 const std::string& asset_path) {
  if (asset_path.empty() || asset_path[0] == '/' ||
      asset_path.find("//") == 0) {
    return std::string();
  }
  std::vector<std::string> parts;
  const size_t slash = root_name.find_last_of('/');
  if (slash != std::string::npos) {
    parts = Split(root_name.substr(0, slash), '/');
  }
  std::string path = asset_path;
  std::replace(path.begin(), path.end(), '\\', '/');
  for (const std::string& part : Split(path, '/')) {
    if (part.empty() || part == ".") continue;
    if (part == "..") {
      if (parts.empty()) return std::string();
      parts.pop_back();
    } else {
      parts.push_back(part);
    }
  }
  std::string normalized;
  for (const std::string& part : parts) {
    if (!normalized.empty()) normalized += '/';
    normalized += part;
  }
  return normalized;
}

// ARKit (usdchecker --arkit, ARKitFileExtensionChecker): a package may only
// contain USD layers and the portable image formats.
bool IsArkitPackageExtension(const std::string& ext) {
  static const std::unordered_set<std::string> kAllowed = {
      "usd", "usda", "usdc", "usdz", "exr", "jpg", "jpeg", "png"};
  return kAllowed.count(ext) != 0;
}

std::vector<PackageEntry> ParsePackageEntries(const uint8_t* bytes, size_t size,
                                              bool arkit,
                                              USDValidationResult* result) {
  std::vector<PackageEntry> entries;
  std::unordered_set<std::string> names;
  size_t pos = 0;
  while (pos <= size && size - pos >= 4) {
    uint32_t signature = 0;
    if (!ReadU32(bytes, size, pos, &signature) || signature != 0x04034b50u) {
      break;
    }
    if (size - pos < 30) {
      AddIssue(result, USDValidationSeverity::Error,
               "package.structure.header", "<package>",
               "truncated ZIP local-file header");
      break;
    }
    uint16_t flags = 0, compression = 0, name_size = 0, extra_size = 0;
    uint32_t crc = 0, compressed_size = 0, uncompressed_size = 0;
    ReadU16(bytes, size, pos + 6, &flags);
    ReadU16(bytes, size, pos + 8, &compression);
    ReadU32(bytes, size, pos + 14, &crc);
    ReadU32(bytes, size, pos + 18, &compressed_size);
    ReadU32(bytes, size, pos + 22, &uncompressed_size);
    ReadU16(bytes, size, pos + 26, &name_size);
    ReadU16(bytes, size, pos + 28, &extra_size);
    const size_t variable_size = static_cast<size_t>(name_size) + extra_size;
    if (variable_size > size - pos - 30) {
      AddIssue(result, USDValidationSeverity::Error,
               "package.structure.header", "<package>",
               "ZIP entry name or extra field exceeds archive bounds");
      break;
    }
    const size_t data_offset = pos + 30 + variable_size;
    if (static_cast<size_t>(compressed_size) > size - data_offset) {
      AddIssue(result, USDValidationSeverity::Error,
               "package.structure.bounds", "<package>",
               "ZIP entry payload exceeds archive bounds");
      break;
    }
    PackageEntry entry;
    entry.name.assign(reinterpret_cast<const char*>(bytes + pos + 30),
                      name_size);
    entry.local_header_offset = pos;
    entry.data_offset = data_offset;
    entry.size = compressed_size;
    entry.uncompressed_size = uncompressed_size;
    entry.crc = crc;
    entry.flags = flags;
    entry.compression = compression;
    const std::string location = "<package>[" + entry.name + "]";
    if (flags & 0x0001u) {
      AddIssue(result, USDValidationSeverity::Error,
               "package.entry.encryption", location,
               "USDZ entries must not be encrypted");
    }
    if (flags & 0x0008u) {
      AddIssue(result, USDValidationSeverity::Error,
               "package.entry.dataDescriptor", location,
               "USDZ entries must carry sizes in the local-file header");
    }
    if (compression != 0) {
      AddIssue(result, USDValidationSeverity::Error,
               "package.entry.compression", location,
               "USDZ entries must use ZIP store mode (no compression)");
    } else if (compressed_size != uncompressed_size) {
      AddIssue(result, USDValidationSeverity::Error,
               "package.entry.size", location,
               "stored ZIP entry has differing compressed and uncompressed sizes");
    } else if (ComputeCRC32(bytes + data_offset, compressed_size) != crc) {
      AddIssue(result, USDValidationSeverity::Error, "package.entry.crc",
               location, "entry payload does not match its local-header CRC-32");
    }
    if ((data_offset & 63u) != 0u) {
      AddIssue(result, USDValidationSeverity::Error,
               "package.entry.alignment", location,
               "USDZ entry data must begin on a 64-byte boundary");
    }
    if (IsUnsafePackagePath(entry.name)) {
      AddIssue(result, USDValidationSeverity::Error, "package.entry.path",
               location, "package entry name is not a safe relative path");
    }
    if (!names.insert(entry.name).second) {
      AddIssue(result, USDValidationSeverity::Error,
               "package.entry.duplicate", location,
               "package contains a duplicate entry name");
    }
    static const std::unordered_set<std::string> kPortableExtensions = {
        "usd", "usda", "usdc", "png", "jpg", "jpeg", "exr",
        "avif", "m4a", "mp3", "wav"};
    const std::string ext = LowerExtension(entry.name);
    if (arkit) {
      // The ARKit set is stricter than the portable set (no avif/audio) and is
      // a hard requirement, so it replaces the portability warning.
      if (!IsArkitPackageExtension(ext)) {
        AddIssue(result, USDValidationSeverity::Error,
                 "arkit.package.fileExtension", location,
                 "package entry `" + entry.name +
                     "` is not an ARKit-supported file type; a USDZ may only "
                     "contain usd/usda/usdc/usdz layers and exr/jpg/jpeg/png "
                     "textures");
      }
    } else if (!ext.empty() && kPortableExtensions.count(ext) == 0) {
      AddIssue(result, USDValidationSeverity::Warning,
               "package.entry.extension", location,
               "entry type is outside the portable USDZ/AR extension set");
    }
    entries.push_back(entry);
    pos = data_offset + compressed_size;
  }
  return entries;
}

void ValidatePackageCentralDirectory(const uint8_t* bytes, size_t size,
                                     const std::vector<PackageEntry>& entries,
                                     USDValidationResult* result) {
  if (size < 22) {
    AddIssue(result, USDValidationSeverity::Error,
             "package.centralDirectory.missing", "<package>",
             "ZIP end-of-central-directory record is missing");
    return;
  }
  const size_t lower = size > 65557 ? size - 65557 : 0;
  size_t eocd = std::string::npos;
  for (size_t pos = size - 22;; --pos) {
    uint32_t signature = 0;
    if (ReadU32(bytes, size, pos, &signature) && signature == 0x06054b50u) {
      eocd = pos;
      break;
    }
    if (pos == lower) break;
  }
  if (eocd == std::string::npos) {
    AddIssue(result, USDValidationSeverity::Error,
             "package.centralDirectory.missing", "<package>",
             "ZIP end-of-central-directory record is missing");
    return;
  }
  uint16_t disk = 0, central_disk = 0, disk_entries = 0, total_entries = 0;
  uint16_t comment_size = 0;
  uint32_t central_size = 0, central_offset = 0;
  ReadU16(bytes, size, eocd + 4, &disk);
  ReadU16(bytes, size, eocd + 6, &central_disk);
  ReadU16(bytes, size, eocd + 8, &disk_entries);
  ReadU16(bytes, size, eocd + 10, &total_entries);
  ReadU32(bytes, size, eocd + 12, &central_size);
  ReadU32(bytes, size, eocd + 16, &central_offset);
  ReadU16(bytes, size, eocd + 20, &comment_size);
  if (eocd + 22 + comment_size != size) {
    AddIssue(result, USDValidationSeverity::Error,
             "package.centralDirectory.trailingData", "<package>",
             "EOCD comment length does not account for the archive tail");
  }
  if (comment_size != 0) {
    AddIssue(result, USDValidationSeverity::Error,
             "package.centralDirectory.comment", "<package>",
             "USDZ archives must have an empty ZIP comment");
  }
  if (disk != 0 || central_disk != 0 || disk_entries != total_entries) {
    AddIssue(result, USDValidationSeverity::Error,
             "package.centralDirectory.multidisk", "<package>",
             "multi-disk ZIP archives are not valid USDZ packages");
  }
  if (total_entries != entries.size()) {
    AddIssue(result, USDValidationSeverity::Error,
             "package.centralDirectory.entryCount", "<package>",
             "central-directory entry count differs from local headers");
  }
  if (central_offset > eocd || central_size > eocd - central_offset ||
      central_offset + central_size != eocd) {
    AddIssue(result, USDValidationSeverity::Error,
             "package.centralDirectory.bounds", "<package>",
             "central-directory range is invalid or non-contiguous");
    return;
  }
  size_t pos = central_offset;
  size_t index = 0;
  while (pos < eocd) {
    uint32_t signature = 0;
    if (eocd - pos < 46 || !ReadU32(bytes, size, pos, &signature) ||
        signature != 0x02014b50u) {
      AddIssue(result, USDValidationSeverity::Error,
               "package.centralDirectory.structure", "<package>",
               "invalid central-directory entry header");
      return;
    }
    uint16_t flags = 0, compression = 0, name_size = 0, extra_size = 0;
    uint16_t entry_comment_size = 0;
    uint32_t crc = 0, compressed_size = 0, uncompressed_size = 0;
    uint32_t local_offset = 0;
    ReadU16(bytes, size, pos + 8, &flags);
    ReadU16(bytes, size, pos + 10, &compression);
    ReadU32(bytes, size, pos + 16, &crc);
    ReadU32(bytes, size, pos + 20, &compressed_size);
    ReadU32(bytes, size, pos + 24, &uncompressed_size);
    ReadU16(bytes, size, pos + 28, &name_size);
    ReadU16(bytes, size, pos + 30, &extra_size);
    ReadU16(bytes, size, pos + 32, &entry_comment_size);
    ReadU32(bytes, size, pos + 42, &local_offset);
    const size_t variable = static_cast<size_t>(name_size) + extra_size +
                            entry_comment_size;
    if (variable > eocd - pos - 46) {
      AddIssue(result, USDValidationSeverity::Error,
               "package.centralDirectory.bounds", "<package>",
               "central-directory entry exceeds its declared range");
      return;
    }
    const std::string name(reinterpret_cast<const char*>(bytes + pos + 46),
                           name_size);
    const auto local_it = std::find_if(
        entries.begin(), entries.end(), [&](const PackageEntry& entry) {
          return entry.local_header_offset == local_offset;
        });
    if (local_it == entries.end()) {
      AddIssue(result, USDValidationSeverity::Error,
               "package.centralDirectory.mismatch", "<package>[" + name + "]",
               "central directory references no matching local header");
    } else {
      const PackageEntry& local = *local_it;
      if (name != local.name || flags != local.flags ||
          compression != local.compression || crc != local.crc ||
          compressed_size != local.size ||
          uncompressed_size != local.uncompressed_size ||
          local_offset != local.local_header_offset) {
        AddIssue(result, USDValidationSeverity::Error,
                 "package.centralDirectory.mismatch",
                 "<package>[" + name + "]",
                 "central-directory metadata differs from the local header");
      }
    }
    ++index;
    pos += 46 + variable;
  }
  if (index != total_entries) {
    AddIssue(result, USDValidationSeverity::Error,
             "package.centralDirectory.entryCount", "<package>",
             "parsed central-directory entry count differs from EOCD");
  }
}

void CollectValueDependencies(
    const tinyusdz::next::Value& value,
    std::unordered_set<std::string>* dependencies) {
  if (!dependencies) return;
  if (const std::string* asset = value.as_asset_path()) {
    if (!asset->empty()) dependencies->insert(*asset);
    return;
  }
  if (value.is_array() &&
      value.type_id() == tinyusdz::next::TypeId::AssetPath) {
    if (const auto* assets = value.as_token_array()) {
      for (const std::string& asset : *assets) {
        if (!asset.empty()) dependencies->insert(asset);
      }
    }
    return;
  }
  if (const tinyusdz::next::Dict* dict = value.as_dictionary()) {
    for (const auto& entry : dict->entries) {
      CollectValueDependencies(entry.second, dependencies);
    }
  }
}

void CollectLayerDependencies(const Layer& layer,
                              std::unordered_set<std::string>* dependencies) {
  if (!dependencies) return;
  for (const std::string& path : layer.meta().subLayers) {
    if (!path.empty()) dependencies->insert(path);
  }
  if (!layer.meta().colorConfiguration.empty()) {
    dependencies->insert(layer.meta().colorConfiguration);
  }
  CollectValueDependencies(layer.meta().customLayerData, dependencies);
  CollectValueDependencies(layer.meta().expressionVariables, dependencies);
  for (const auto& prim : layer.prims()) {
    const auto collect_arcs = [&](const std::vector<std::string>& arcs) {
      for (const std::string& encoded : arcs) {
        const auto arc = tinyusdz::next::Compositor::ParseReference(encoded);
        if (!arc.asset_path.empty()) dependencies->insert(arc.asset_path);
      }
    };
    collect_arcs(prim.meta().references);
    collect_arcs(prim.meta().payloads);
    CollectValueDependencies(prim.meta().customData(), dependencies);
    CollectValueDependencies(prim.meta().assetInfo(), dependencies);
    CollectValueDependencies(prim.meta().sdrMetadata(), dependencies);
    CollectValueDependencies(prim.meta().clips(), dependencies);
    for (const auto& slot : prim.properties().slots()) {
      const tinyusdz::next::Value* value = prim.property_value(slot.name_id);
      if (value) CollectValueDependencies(*value, dependencies);
      if (const auto* meta = prim.property_meta(slot.name_id)) {
        CollectValueDependencies(meta->customData, dependencies);
        CollectValueDependencies(meta->assetInfo, dependencies);
        CollectValueDependencies(meta->sdrMetadata, dependencies);
      }
      if (const auto* samples = prim.time_samples(slot.name_id)) {
        for (const auto& sample : *samples) {
          if (const tinyusdz::next::Value* sample_value =
                  prim.time_sample_value(sample.second)) {
            CollectValueDependencies(*sample_value, dependencies);
          }
        }
      }
    }
  }
}

void ValidatePackage(const uint8_t* bytes, size_t size, const Layer& layer,
                     bool arkit, USDValidationResult* result,
                     std::vector<PackageEntry>* parsed_entries) {
  if (!result) return;
  result->checked_groups.package = true;
  std::vector<PackageEntry> entries =
      ParsePackageEntries(bytes, size, arkit, result);
  if (entries.empty()) {
    AddIssue(result, USDValidationSeverity::Error, "package.structure.empty",
             "<package>", "USDZ archive contains no local-file entries");
    return;
  }
  ValidatePackageCentralDirectory(bytes, size, entries, result);
  const std::string root_ext = LowerExtension(entries.front().name);
  if (root_ext != "usd" && root_ext != "usda" && root_ext != "usdc") {
    AddIssue(result, USDValidationSeverity::Error, "package.root.first",
             "<package>[" + entries.front().name + "]",
             "the first USDZ entry must be the package root USD layer");
  }
  // UsdUtilsCreateNewARKitUsdzPackage forces the root layer to binary crate.
  if (arkit && root_ext != "usdc") {
    AddIssue(result, USDValidationSeverity::Error, "arkit.package.rootLayer",
             "<package>[" + entries.front().name + "]",
             "the ARKit package root layer must be a `.usdc` crate layer, but "
             "is `." + root_ext + "`");
  }
  std::unordered_set<std::string> entry_names;
  for (const auto& entry : entries) entry_names.insert(entry.name);
  std::unordered_set<std::string> dependencies;
  CollectLayerDependencies(layer, &dependencies);
  for (const std::string& authored : dependencies) {
    const std::string normalized =
        NormalizePackagePath(entries.front().name, authored);
    if (normalized.empty()) {
      AddIssue(result, USDValidationSeverity::Error,
               "package.dependency.external", "<package>",
               "dependency is not package-relative: " + authored);
    } else if (entry_names.count(normalized) == 0) {
      AddIssue(result, USDValidationSeverity::Error,
               "package.dependency.missing", "<package>",
               "authored dependency is absent from package: " + normalized);
    }
  }
  if (parsed_entries) *parsed_entries = std::move(entries);
}

void ValidateCrate(const uint8_t* bytes, size_t size,
                   const std::string& location, size_t max_memory,
                   USDValidationResult* result) {
  if (!result) return;
  result->checked_groups.crate = true;
  tinyusdz::next::CrateReadOptions options;
  options.max_memory = max_memory;
  options.lazy_arrays = true;
  tinyusdz::next::CrateReader reader(options);
  tinyusdz::next::CrateReadResult read = reader.Read(bytes, size);
  if (!read.success) {
    for (const auto& error : read.errors) {
      AddIssue(result, USDValidationSeverity::Error, "crate.structure",
               location, "offset " + std::to_string(error.offset) + ": " +
                             error.message);
    }
    if (read.errors.empty()) {
      AddIssue(result, USDValidationSeverity::Error, "crate.structure",
               location, "Crate reader rejected the container");
    }
    return;
  }
  for (const std::string& warning : read.warnings) {
    AddIssue(result, USDValidationSeverity::Warning, "crate.decode.warning",
             location, warning);
  }
  const auto tokens = reader.tokens();
  const auto& paths = reader.paths();
  const auto& fields = reader.fields();
  const auto& specs = reader.specs();
  const auto& fieldsets = reader.fieldset_indices();
  for (size_t i = 0; i < fields.size(); ++i) {
    if (fields[i].token_index.value >= tokens.size()) {
      AddIssue(result, USDValidationSeverity::Error,
               "crate.field.tokenIndex", location,
               "field " + std::to_string(i) + " references an invalid token index");
    }
  }
  for (size_t i = 0; i < specs.size(); ++i) {
    if (specs[i].path_index.value >= paths.size()) {
      AddIssue(result, USDValidationSeverity::Error,
               "crate.spec.pathIndex", location,
               "spec " + std::to_string(i) + " references an invalid path index");
    }
    const uint32_t start = specs[i].fieldset_index.value;
    if (start >= fieldsets.size()) {
      AddIssue(result, USDValidationSeverity::Error,
               "crate.spec.fieldsetIndex", location,
               "spec " + std::to_string(i) + " references an invalid fieldset index");
      continue;
    }
    bool terminated = false;
    for (size_t j = start; j < fieldsets.size(); ++j) {
      if (fieldsets[j] == 0xffffffffu) {
        terminated = true;
        break;
      }
      if (fieldsets[j] >= fields.size()) {
        AddIssue(result, USDValidationSeverity::Error,
                 "crate.fieldset.fieldIndex", location,
                 "fieldset references an invalid field index");
        break;
      }
    }
    if (!terminated) {
      AddIssue(result, USDValidationSeverity::Error,
               "crate.fieldset.terminator", location,
               "fieldset has no terminating sentinel");
    }
  }
}

std::string ResolveDependencyPath(const std::string& anchor,
                                  const std::string& asset) {
  namespace fs = std::filesystem;
  fs::path path(asset);
  if (path.is_relative()) path = fs::path(anchor).parent_path() / path;
  return path.lexically_normal().string();
}

std::string MapComposedPath(const std::string& source_path,
                            const std::string& source_prefix,
                            const std::string& target_prefix) {
  if (source_prefix == "/") {
    return target_prefix == "/" ? source_path : target_prefix + source_path;
  }
  if (source_path == source_prefix) return target_prefix;
  if (source_path.size() > source_prefix.size() &&
      source_path.compare(0, source_prefix.size(), source_prefix) == 0 &&
      source_path[source_prefix.size()] == '/') {
    return target_prefix + source_path.substr(source_prefix.size());
  }
  return std::string();
}

std::string PropertyTypeName(const tinyusdz::next::PrimSpec& prim,
                             const tinyusdz::next::PropSlot& slot) {
  const std::string& name =
      tinyusdz::next::GetPropNameTable().get(slot.name_id);
  if (const std::string* declared = prim.property_type_name(name)) {
    if (!declared->empty()) return *declared;
  }
  const char* type = tinyusdz::next::GetTypeName(
      static_cast<tinyusdz::next::TypeId>(slot.value_type));
  return type ? std::string(type) : std::to_string(slot.value_type);
}

void CompareLayerTypes(const Layer& stronger, const Layer& weaker,
                       const std::string& weaker_prefix,
                       const std::string& stronger_prefix,
                       USDValidationResult* result) {
  for (const auto& weak_prim : weaker.prims()) {
    const std::string mapped = MapComposedPath(
        weak_prim.path().str(), weaker_prefix, stronger_prefix);
    if (mapped.empty()) continue;
    const tinyusdz::next::PrimSpec* strong_prim =
        stronger.prim_at_path(mapped);
    if (!strong_prim) continue;
    for (const auto& weak_slot : weak_prim.properties().slots()) {
      const tinyusdz::next::PropSlot* strong_slot =
          strong_prim->property(weak_slot.name_id);
      if (!strong_slot) continue;
      const std::string& property =
          tinyusdz::next::GetPropNameTable().get(weak_slot.name_id);
      const bool weak_rel = weak_slot.is_relationship();
      const bool strong_rel = strong_slot->is_relationship();
      if (weak_rel != strong_rel) {
        AddIssue(result, USDValidationSeverity::Error,
                 "core.composition.propertyKindMismatch",
                 mapped + "." + property,
                 "property is authored as both an attribute and a relationship across composition arcs");
        continue;
      }
      if (weak_rel) continue;
      const std::string weak_type = PropertyTypeName(weak_prim, weak_slot);
      const std::string strong_type = PropertyTypeName(*strong_prim, *strong_slot);
      if (weak_type != strong_type) {
        AddIssue(result, USDValidationSeverity::Error,
                 "core.composition.attributeTypeMismatch",
                 mapped + "." + property,
                 "attribute has conflicting declared types '" + strong_type +
                     "' and '" + weak_type + "' across composition arcs");
      }
    }
  }
}

void AuditLayerTypesRecursive(
    const Layer& layer, const std::string& anchor,
    const tinyusdz::next::pcp::LayerLoadOptions& options,
    std::unordered_set<std::string>* visited, USDValidationResult* result) {
  if (!visited || !result) return;
  const auto load_and_audit = [&](const std::string& asset,
                                  const std::string& source_prefix,
                                  const std::string& target_prefix) {
    if (asset.empty()) return;
    const std::string resolved = ResolveDependencyPath(anchor, asset);
    std::string warnings, errors;
    std::shared_ptr<Layer> dependency =
        tinyusdz::next::pcp::LoadLayerFromFile(
            resolved, &warnings, &errors, options);
    if (!dependency) return;
    std::string effective_prefix = source_prefix;
    if (effective_prefix.empty()) {
      if (!dependency->meta().defaultPrim.empty()) {
        effective_prefix = "/" + dependency->meta().defaultPrim;
      } else if (!dependency->root_indices().empty()) {
        const auto* root = dependency->prim(dependency->root_indices().front());
        if (root) effective_prefix = root->path().str();
      }
    }
    if (!effective_prefix.empty()) {
      CompareLayerTypes(layer, *dependency, effective_prefix, target_prefix,
                        result);
    }
    if (visited->insert(resolved).second) {
      AuditLayerTypesRecursive(*dependency, resolved, options, visited, result);
    }
  };

  for (const std::string& sublayer : layer.meta().subLayers) {
    load_and_audit(sublayer, "/", "/");
  }
  for (const auto& prim : layer.prims()) {
    const auto audit_arcs = [&](const std::vector<std::string>& arcs) {
      for (const std::string& encoded : arcs) {
        const auto arc = tinyusdz::next::Compositor::ParseReference(encoded);
        if (!arc.asset_path.empty()) {
          load_and_audit(arc.asset_path, arc.prim_path, prim.path().str());
        }
      }
    };
    audit_arcs(prim.meta().references);
    audit_arcs(prim.meta().payloads);
  }
}

}  // namespace

int main(int argc, char** argv) {
  Args args;
  args.groups = AllAvailableGroups();
  std::string error;
  const ParseArgsResult parsed = ParseArgs(argc, argv, &args, &error);
  if (parsed == ParseArgsResult::ExitSuccess) return kExitValid;
  if (parsed == ParseArgsResult::Error) {
    std::cerr << "tusdchecker: error: " << error << "\n\n";
    PrintUsage(std::cerr);
    return kExitError;
  }

  if (args.max_memory_mb >
      std::numeric_limits<size_t>::max() / (size_t{1024} * 1024)) {
    std::cerr << "tusdchecker: error: --max-memory-mb is too large\n";
    return kExitError;
  }
  const size_t max_memory = args.max_memory_mb * size_t{1024} * 1024;
  tinyusdz::next::pcp::LayerLoadOptions load_options;
  load_options.max_memory = max_memory;
  load_options.strict_aousd_conformance = args.strict_parse;

  std::string parser_warnings;
  std::string parser_errors;
  std::string input_bytes;
  std::shared_ptr<Layer> layer;
  if (args.input == "-") {
    if (!ReadStdin(max_memory, &input_bytes, &error)) {
      std::cerr << "tusdchecker: error: " << error << '\n';
      return kExitError;
    }
    layer = tinyusdz::next::pcp::LoadLayerFromMemory(
        "stdin.usda", reinterpret_cast<const uint8_t*>(input_bytes.data()),
        input_bytes.size(), &parser_warnings, &parser_errors,
        load_options);
  } else if (args.groups.package || args.groups.crate) {
    if (!ReadFile(args.input, max_memory, &input_bytes, &error)) {
      std::cerr << "tusdchecker: error: " << error << '\n';
      return kExitError;
    }
    layer = tinyusdz::next::pcp::LoadLayerFromMemory(
        args.input, reinterpret_cast<const uint8_t*>(input_bytes.data()),
        input_bytes.size(), &parser_warnings, &parser_errors, load_options);
  } else {
    layer = tinyusdz::next::pcp::LoadLayerFromFile(
        args.input, &parser_warnings, &parser_errors, load_options);
  }
  if (!layer) {
    std::cerr << "tusdchecker: failed to parse " << args.input;
    if (!parser_errors.empty())
      std::cerr << ":\n" << parser_errors;
    else
      std::cerr << '\n';
    return kExitError;
  }

  std::unique_ptr<Layer> composed_layer;
  std::vector<tinyusdz::next::CompositionError> composition_errors;
  if (args.composed) {
    tinyusdz::next::ResolverConfig resolver_config;
    resolver_config.enable_suffix_fallback = !args.strict_parse;
    tinyusdz::next::AssetResolver resolver(resolver_config);
    tinyusdz::next::Compositor compositor(&resolver);
    tinyusdz::next::CompositionOptions composition_options;
    composition_options.strict_aousd_conformance = args.strict_parse;
    composition_options.max_layer_memory = max_memory;
    compositor.SetOptions(composition_options);
    compositor.SetLayerLoader([&](const std::string& resolved_path,
                                  std::string* load_error) {
      std::string dependency_warnings;
      std::string dependency_errors;
      std::shared_ptr<Layer> loaded =
          tinyusdz::next::pcp::LoadLayerFromFile(
              resolved_path, &dependency_warnings, &dependency_errors,
              load_options);
      if (!dependency_warnings.empty()) parser_warnings += dependency_warnings;
      if (!loaded) {
        if (load_error) *load_error = dependency_errors;
        return std::unique_ptr<Layer>();
      }
      return std::make_unique<Layer>(loaded->Clone());
    });
    composed_layer = compositor.Compose(*layer,
                                        args.input == "-" ? "" : args.input);
    composition_errors = compositor.GetErrors();
  }

  Layer* validation_layer = composed_layer ? composed_layer.get() : layer.get();
  USDValidationResult result =
      tinyusdz::next::ValidateLayerAgainstAOUSDCore(*validation_layer,
                                                    args.groups);
  if (args.composed && args.groups.core && args.input != "-") {
    std::unordered_set<std::string> visited;
    visited.insert(std::filesystem::path(args.input).lexically_normal().string());
    AuditLayerTypesRecursive(*layer, args.input, load_options, &visited,
                             &result);
  }
  const uint8_t* raw = reinterpret_cast<const uint8_t*>(input_bytes.data());
  const bool is_package = input_bytes.size() >= 4 && raw[0] == 0x50 &&
                          raw[1] == 0x4b && raw[2] == 0x03 && raw[3] == 0x04;
  const bool is_crate = input_bytes.size() >= 8 &&
                        std::memcmp(raw, "PXR-USDC", 8) == 0;
  std::vector<PackageEntry> package_entries;
  if (args.groups.package && is_package) {
    ValidatePackage(raw, input_bytes.size(), *layer, args.groups.arkit, &result,
                    &package_entries);
  }
  if (args.groups.crate) {
    if (is_crate) {
      ValidateCrate(raw, input_bytes.size(), args.input, max_memory, &result);
    } else if (is_package) {
      if (package_entries.empty()) {
        USDValidationResult ignored;
        package_entries = ParsePackageEntries(raw, input_bytes.size(),
                                              /* arkit */ false, &ignored);
      }
      for (const PackageEntry& entry : package_entries) {
        if (entry.compression == 0 && LowerExtension(entry.name) == "usdc" &&
            entry.data_offset <= input_bytes.size() &&
            entry.size <= input_bytes.size() - entry.data_offset) {
          ValidateCrate(raw + entry.data_offset, entry.size,
                        args.input + "[" + entry.name + "]", max_memory,
                        &result);
        }
      }
    }
  }
  for (const auto& composition_error : composition_errors) {
    USDValidationIssue issue;
    issue.severity = USDValidationSeverity::Error;
    issue.rule_id = "core.composition.error";
    issue.location = composition_error.prim_path.empty()
                         ? std::string("/")
                         : composition_error.prim_path;
    issue.message = composition_error.message;
    result.issues.push_back(std::move(issue));
  }
  if (args.composed && !composed_layer && composition_errors.empty()) {
    USDValidationIssue issue;
    issue.severity = USDValidationSeverity::Error;
    issue.rule_id = "core.composition.error";
    issue.location = "/";
    issue.message = "composition failed without a detailed diagnostic";
    result.issues.push_back(std::move(issue));
  }
  if (args.require_all_groups) {
    const std::vector<std::string> requested =
        GetValidationGroupNames(args.groups);
    const std::vector<std::string> checked =
        GetValidationGroupNames(result.checked_groups);
    for (const std::string& name : requested) {
      if (std::find(checked.begin(), checked.end(), name) == checked.end()) {
        AddIssue(&result, USDValidationSeverity::Error,
                 "checker.coverage.skipped", "<checker>",
                 "requested validation group was inapplicable: " + name);
      }
    }
  }
  const bool warnings_fail =
      args.strict && (result.warning_count() > 0 || !parser_warnings.empty());
  const bool valid = result.ok() && !warnings_fail;

  std::ofstream file_output;
  std::ostream* output = &std::cout;
  if (args.output == "stderr") {
    output = &std::cerr;
  } else if (args.output != "stdout") {
    file_output.open(args.output, std::ios::out | std::ios::trunc);
    if (!file_output) {
      std::cerr << "tusdchecker: error: cannot open output file '"
                << args.output << "'\n";
      return kExitError;
    }
    output = &file_output;
  }

  if (args.json) {
    *output << JsonReport(args, result, parser_warnings, valid);
  } else {
    *output << "Input: " << args.input << '\n';
    if (!parser_warnings.empty()) {
      *output << "\nParser warnings:\n" << parser_warnings;
      if (parser_warnings.back() != '\n') *output << '\n';
    }
    *output << tinyusdz::next::FormatValidationResult(result);
    if (warnings_fail && result.ok()) {
      *output << "Strict result: FAILED - warnings are errors\n";
    }
  }
  if (!*output) {
    std::cerr << "tusdchecker: error: failed while writing report\n";
    return kExitError;
  }
  return valid ? kExitValid : kExitInvalid;
}
