#include <iostream>

#include "prim-types.hh"
#include "tydra/mcp-server.hh"
#include "tydra/command-and-history.hh"
#include "arg-parser.hh"

int main(int argc, char **argv) {

  using namespace tinyusdz;

  argparser::ArgParser parser;
  parser.add_option("--port", true, "Port number for the MCP server");
  parser.add_option("--host", true, "Hostname(default `localhost`)");

  if (!parser.parse(argc, argv)) {
    std::cerr << "Error parsing arguments." << std::endl;
    parser.print_help();
    return -1;

  }

  std::cout << "is_setr " << parser.is_set("--port") << "\n";

  double portval;
  if (!parser.get("--port", portval)) {
    std::cerr << "--port is missing or invalid\n";
    return -1;
  }

  int port = int(portval);
  
  std::string hostname;
  if (!parser.get("--host", hostname)) {
    std::cerr << "--host is missing or invalid\n";
    return -1;
  }

  std::cout << "port " << port << "\n";
  std::cout << "hostname " << hostname << "\n";

  tydra::MCPServer server;
  if (!server.init(port, hostname)) {
    std::cerr << "Failed to init MCP server.\n";
    return -1;
  }


  Layer empty;
  
  tydra::EditHistory hist;
  hist.layer = std::move(empty);

  tydra::HistoryQueue queue;
  if (!queue.push(std::move(hist))) {
    return -1;
  }
  
  return 0;

}
