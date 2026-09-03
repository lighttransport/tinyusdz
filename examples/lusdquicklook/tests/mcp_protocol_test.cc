// SPDX-License-Identifier: Apache-2.0
// Transport-independent MCP protocol checks. Viewer-state calls are covered by
// the Xvfb smoke; this test keeps initialize/tools/list usable without a GUI.
#include <string>

#include "mcp/mcp_server.hh"

namespace {

bool Contains(const std::string& text, const char* needle) {
  return text.find(needle) != std::string::npos;
}

}  // namespace

int main() {
  lusdql::MCPServer server(nullptr);
  const std::string initialize =
      server.dispatchLine(
          R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{}})");
  if (!Contains(initialize, "lusdquicklook-mcp") ||
      !Contains(initialize, "protocolVersion")) {
    return 1;
  }

  const std::string tools = server.dispatchLine(
      R"({"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}})");
  for (const char* name : {"load_usd", "get_scene_info", "viewport",
                           "screenshot", "render_settings", "quit"}) {
    if (!Contains(tools, name)) return 2;
  }

  if (!server.dispatchLine(
           R"({"jsonrpc":"2.0","method":"notifications/initialized"})")
           .empty()) {
    return 3;
  }
  if (!Contains(server.dispatchLine("not-json"), "Parse error")) return 4;
  if (!Contains(server.dispatchLine(
                    R"({"jsonrpc":"2.0","id":3,"method":"nope"})"),
                "Method not found")) {
    return 5;
  }
  return 0;
}
