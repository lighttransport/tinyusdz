// SPDX-License-Identifier: Apache-2.0
// Main-thread host surface for tusdquicklook's embedded MCP server.
#pragma once

#include <string>

#include "external/jsonhpp/nlohmann/json.hpp"

namespace tusdql {

class McpHost {
 public:
  virtual ~McpHost() = default;

  // All methods are called by App::Run() on the UI thread. Transport threads
  // never touch the scene, renderer, camera, or lightui state directly.
  virtual nlohmann::json mcpLoadUsd(const nlohmann::json& args,
                                    std::string& err) = 0;
  virtual nlohmann::json mcpSceneInfo(const nlohmann::json& args,
                                      std::string& err) = 0;
  virtual nlohmann::json mcpListPrims(const nlohmann::json& args,
                                      std::string& err) = 0;
  virtual nlohmann::json mcpViewport(const nlohmann::json& args,
                                     std::string& err) = 0;
  virtual nlohmann::json mcpScreenshot(const nlohmann::json& args,
                                       std::string& err) = 0;
  virtual nlohmann::json mcpRenderSettings(const nlohmann::json& args,
                                           std::string& err) = 0;
  virtual nlohmann::json mcpQuit(const nlohmann::json& args,
                                 std::string& err) = 0;
};

}  // namespace tusdql
