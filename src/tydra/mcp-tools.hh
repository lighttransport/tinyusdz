#pragma once

#include <vector>
#include <string>

namespace tinyusdz {
namespace tydra {
namespace mcp {

struct MCPTool
{
  
  bool GetVersion();
  bool ReadUSDFromFile(const std::string &filepath);
 
};

// for 'tools/list'
bool GetMCPToolsList(
  std::vector<std::string> &toolslist);


} // namespace mcp
} // namespace tydra
} // namespace tinyusdz
