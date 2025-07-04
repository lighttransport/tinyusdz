#include <iostream>

#include "prim-types.hh"
#include "tydra/mcp-server.hh"
#include "tydra/command-and-history.hh"
#include "arg-parser.hh"

int main(int argc, char **argv) {

  using namespace tinyusdz;

  Layer empty;
  
  tydra::EditHistory hist;
  hist.layer = std::move(empty);

  tydra::HistoryQueue queue;
  if (!queue.push(std::move(hist))) {
    return -1;
  }
  
  return 0;

}
