// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Light Transport Entertainment Inc.
//
// tusdchecker - dependency-free AOUSD Core and USD schema validator.

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "next/pcp/layer-registry.hh"
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
};

ValidationOptions AllAvailableGroups() {
  ValidationOptions groups = tinyusdz::next::MakeValidateAllOptions();
  // The next reader does not expose Crate tables to the validator yet.
  groups.crate = false;
  return groups;
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
        "                        lux,physics (default: all)\n"
        "      --core-only       Run AOUSD Core rules only\n"
        "      --all             Run all available rule groups (default)\n"
        "  -t, --strict          Treat validation and parser warnings as "
        "failure\n"
        "      --strict-parse    Reject non-conforming/unsupported format "
        "data\n"
        "      --json            Emit stable machine-readable JSON\n"
        "  -o, --out FILE        Write report to FILE, stdout, or stderr\n"
        "      --max-memory-mb N Bound input/parser memory (default: 1024)\n"
        "  -h, --help            Show this help\n"
        "      --version         Show validator/spec version\n"
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
    } else if (arg == "--json") {
      args->json = true;
    } else if (arg == "-t" || arg == "--strict") {
      args->strict = true;
    } else if (arg == "--strict-parse") {
      args->strict_parse = true;
    } else if (arg == "--core-only") {
      args->groups = ValidationOptions();
    } else if (arg == "--all") {
      args->groups = AllAvailableGroups();
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
     << ",\n  \"checkedGroups\":[";
  const std::vector<std::string> groups =
      GetValidationGroupNames(result.checked_groups);
  for (size_t i = 0; i < groups.size(); ++i) {
    if (i) os << ',';
    WriteJsonString(os, groups[i]);
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
  std::shared_ptr<Layer> layer;
  if (args.input == "-") {
    std::string bytes;
    if (!ReadStdin(max_memory, &bytes, &error)) {
      std::cerr << "tusdchecker: error: " << error << '\n';
      return kExitError;
    }
    layer = tinyusdz::next::pcp::LoadLayerFromMemoryOwned(
        "stdin.usda", std::move(bytes), &parser_warnings, &parser_errors,
        load_options);
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

  const USDValidationResult result =
      tinyusdz::next::ValidateLayerAgainstAOUSDCore(*layer, args.groups);
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
