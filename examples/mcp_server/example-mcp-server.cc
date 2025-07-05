#include <iostream>

#include "prim-types.hh"
#include "tydra/mcp-server.hh"
#include "tydra/command-and-history.hh"
#include "arg-parser.hh"

int main(int argc, char **argv) {

  using namespace tinyusdz;

  argparser::ArgParser parser;
  parser.add_option("--port", true, "Port number for the MCP server(default 8080)");
  parser.add_option("--host", true, "Hostname(default `localhost`)");

  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " [options]" << std::endl;
    parser.print_help();
    return -1;
  } 

  if (!parser.parse(argc, argv)) {
    std::cerr << "Error parsing arguments." << std::endl;
    parser.print_help();
    return -1;

  }
  parser.parse(argc, argv);


  Layer empty;
  
  tydra::EditHistory hist;
  hist.layer = std::move(empty);

  tydra::HistoryQueue queue;
  if (!queue.push(std::move(hist))) {
    return -1;
  }
  
  return 0;

}
