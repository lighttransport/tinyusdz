// Interface for MCP(ModelContextProtocol)
#pragma once

#include <string>
#include <map>

namespace tinyusdz {

namespace tydra {
namespace mcp {

class MCPServer
{
 public:
  MCPServer();
  bool init(int port, const std::string &host = "localhost");
  bool run();
  bool stop();

 private:
  class Impl;
  Impl *impl_ = nullptr; // Pointer to implementation details
 
};

} // namespace mcp
} // namespace tydra
} // namespace tinyusdz


