// SPDX-License-Identifier: Apache-2.0
#include "mcp/mcp_server.hh"

#include <cstring>
#include <string>

#include "external/civetweb/civetweb.h"

namespace tusdql {

int MCPServer::HttpHandler(mg_connection* connection, void* user) {
  auto* server = static_cast<MCPServer*>(user);
  const mg_request_info* info = mg_get_request_info(connection);
  const char* method = info && info->request_method ? info->request_method : "";
  if (std::strcmp(method, "POST") == 0) {
    std::string body;
    char buffer[2048];
    int count = 0;
    while ((count = mg_read(connection, buffer, sizeof(buffer))) > 0) {
      body.append(buffer, static_cast<size_t>(count));
    }
    const std::string response = server->dispatchLine(body);
    if (response.empty()) {
      mg_printf(connection, "HTTP/1.1 202 Accepted\r\nContent-Length: 0\r\n\r\n");
      return 202;
    }
    mg_printf(connection,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: application/json\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "Content-Length: %d\r\n\r\n%s",
              static_cast<int>(response.size()), response.c_str());
    return 200;
  }
  if (std::strcmp(method, "OPTIONS") == 0) {
    mg_printf(connection,
              "HTTP/1.1 200 OK\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
              "Access-Control-Allow-Headers: Content-Type, mcp-session-id\r\n"
              "Content-Length: 0\r\n\r\n");
    return 200;
  }
  const char* message = "tusdquicklook MCP endpoint\n";
  mg_printf(connection,
            "HTTP/1.1 405 Method Not Allowed\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: %d\r\n\r\n%s",
            static_cast<int>(std::strlen(message)), message);
  return 405;
}

bool MCPServer::startHttp(int port) {
  const std::string ports = std::to_string(port);
  const char* options[] = {"listening_ports", ports.c_str(), "num_threads", "2",
                           nullptr};
  http_ctx_ = mg_start(nullptr, this, options);
  if (!http_ctx_) return false;
  mg_set_request_handler(http_ctx_, "/", HttpHandler, this);
  mg_set_request_handler(http_ctx_, "/mcp", HttpHandler, this);
  return true;
}

void MCPServer::stopHttp() {
  if (http_ctx_) {
    mg_stop(http_ctx_);
    http_ctx_ = nullptr;
  }
}

}  // namespace tusdql
