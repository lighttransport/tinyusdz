// Interface for MCP(ModelContextProtocol)
#pragma once

#include <string>
#include <map>
#include <functional>

namespace tinyusdz {

namespace tydra {
namespace mcp {

// Transport types supported by the MCP server
enum class TransportType {
  HTTP_POST,      // Standard HTTP POST for JSON-RPC
  HTTP_SSE,       // Server-Sent Events for streaming
  STDIO           // Standard input/output for command-line usage
};

// Configuration for different transport types
struct TransportConfig {
  TransportType type = TransportType::HTTP_POST;
  int port = 8080;
  std::string host = "localhost";
  std::string sse_endpoint = "/mcp/sse";
  bool enable_cors = true;
};

class MCPServer
{
 public:
  MCPServer();
  ~MCPServer();

  // Initialize server with specific transport configuration
  bool init(const TransportConfig& config);

  // Legacy HTTP-only init for backwards compatibility
  bool init(int port, const std::string &host = "localhost");

  // Run the server (blocks for stdio, returns immediately for HTTP)
  bool run();

  // Stop the server
  bool stop();

  // Set custom event handler for SSE connections
  void set_sse_event_handler(std::function<void(const std::string&, const std::string&)> handler);

 private:
  class Impl;
  Impl *impl_ = nullptr; // Pointer to implementation details
};

} // namespace mcp
} // namespace tydra
} // namespace tinyusdz


