// SPDX-License-Identifier: Apache-2.0
// tusdview - interface the MCP server calls to read/manipulate the viewer.
//
// Every method runs on the MAIN thread (the MCP server marshals tool calls into
// the render loop), so implementations may freely touch the Stage / DrawScene /
// camera / GUI. Each returns the tool's JSON result payload; set `err` non-empty
// to signal a tool error (surfaced as a JSON-RPC error to the client).
#pragma once

#include <string>

#include "external/jsonhpp/nlohmann/json.hpp"

namespace tusdview {

class McpHost {
 public:
  virtual ~McpHost() = default;

  virtual nlohmann::json mcpLoadUsd(const nlohmann::json& args, std::string& err) = 0;
  virtual nlohmann::json mcpSceneInfo(const nlohmann::json& args, std::string& err) = 0;
  virtual nlohmann::json mcpGetFocusedPrim(const nlohmann::json& args, std::string& err) = 0;
  virtual nlohmann::json mcpSetFocus(const nlohmann::json& args, std::string& err) = 0;
  virtual nlohmann::json mcpViewport(const nlohmann::json& args, std::string& err) = 0;
  virtual nlohmann::json mcpListPrims(const nlohmann::json& args, std::string& err) = 0;
  virtual nlohmann::json mcpLoadPayloads(const nlohmann::json& args, std::string& err) = 0;
  virtual nlohmann::json mcpTimeline(const nlohmann::json& args, std::string& err) = 0;

  // Forward an unrecognized tool name to the tinyusdz library tool dispatcher
  // (tydra::mcp::CallTool), run against a snapshot of the loaded Stage.
  virtual nlohmann::json mcpCallLibraryTool(const std::string& name,
                                            const nlohmann::json& args,
                                            std::string& err) = 0;
};

}  // namespace tusdview
