// Interface for MCP(ModelContextProtocol)
#pragma once

#include <string>

namespace tinyusdz {
namespace tydra {

class MCPServer
{
 public:
  MCPServer();
  bool init(int port, const std::string &host = "localhost");
  bool run();


 private:
  class Impl;
  Impl *impl_ = nullptr; // Pointer to implementation details
 
};

} // namespace tydra
} // namespace tinyusdz


