#include <iostream>

#include "tydra/mcp-server.hh"
#include "arg-parser.hh"

int main(int argc, char **argv) {

#if !defined(TINYUSDZ_WITH_MCP_SERVER)
  std::cerr << "TinyUSDZ is not built with `TINYUSDZ_WITH_MCP_SERVER` cmake option)" << "\n";
#endif

  return 0;

}
