// SPDX-License-Identifier: MIT
#pragma once
#include <chrono>
#include <string>

namespace vchar {
struct AutoriggerResult { bool ok{false}; bool timedOut{false}; int exitCode{-1}; std::string response; std::string error; };
AutoriggerResult RunAutorigger(const std::string& executable,
                              const std::string& asset,
                              const std::string& output,
                              std::chrono::milliseconds timeout);
}  // namespace vchar
