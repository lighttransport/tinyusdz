// SPDX-License-Identifier: Apache-2.0
// HTTP transport for the MCP server (civetweb raw C API). One JSON-RPC response
// per POST (minimal "Streamable HTTP"; no SSE / server-initiated messages).
#include <cstring>
#include <string>

#include "external/civetweb/civetweb.h"
#include "log.hh"
#include "mcp/mcp_server.hh"

namespace tusdview {

int MCPServer::HttpHandler(mg_connection* conn, void* user) {
  auto* self = static_cast<MCPServer*>(user);
  const mg_request_info* ri = mg_get_request_info(conn);
  const char* method = ri && ri->request_method ? ri->request_method : "";

  if (std::strcmp(method, "POST") == 0) {
    std::string body;
    char buf[2048];
    int n;
    while ((n = mg_read(conn, buf, sizeof(buf))) > 0) {
      body.append(buf, static_cast<size_t>(n));
    }
    const std::string resp = self->dispatchLine(body);  // marshals to main thread
    if (resp.empty()) {
      mg_printf(conn, "HTTP/1.1 202 Accepted\r\nContent-Length: 0\r\n\r\n");
      return 202;
    }
    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: application/json\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "Content-Length: %d\r\n\r\n%s",
              static_cast<int>(resp.size()), resp.c_str());
    return 200;
  }
  if (std::strcmp(method, "OPTIONS") == 0) {
    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
              "Access-Control-Allow-Headers: Content-Type, mcp-session-id\r\n"
              "Content-Length: 0\r\n\r\n");
    return 200;
  }
  const char* info = "tusdview MCP server. POST JSON-RPC 2.0 to this endpoint.\n";
  mg_printf(conn,
            "HTTP/1.1 405 Method Not Allowed\r\n"
            "Content-Type: text/plain\r\n"
            "Content-Length: %d\r\n\r\n%s",
            static_cast<int>(std::strlen(info)), info);
  return 405;
}

bool MCPServer::startHttp(int port) {
  const std::string ports = std::to_string(port);
  const char* options[] = {"listening_ports", ports.c_str(), "num_threads", "4",
                           nullptr};
  httpCtx_ = mg_start(nullptr, this, options);
  if (!httpCtx_) {
    LOGE("MCP: failed to start HTTP server on port %d", port);
    return false;
  }
  mg_set_request_handler(httpCtx_, "/", HttpHandler, this);
  mg_set_request_handler(httpCtx_, "/mcp", HttpHandler, this);
  LOGI("MCP HTTP server listening on http://localhost:%d/mcp", port);
  return true;
}

void MCPServer::stopHttp() {
  if (httpCtx_) {
    mg_stop(httpCtx_);
    httpCtx_ = nullptr;
  }
}

}  // namespace tusdview
