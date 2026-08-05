// SPDX-License-Identifier: Apache-2.0
// tusdquicklook - small embedded MCP server.
//
// JSON-RPC 2.0 over newline-delimited stdio or a minimal POST HTTP endpoint.
// Stateful tools are marshalled into the app's main loop by drain().
#pragma once

#include <atomic>
#include <deque>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "external/jsonhpp/nlohmann/json.hpp"

struct mg_context;
struct mg_connection;

namespace tusdql {

class McpHost;

class MCPServer {
 public:
  explicit MCPServer(McpHost* host) : host_(host) {}
  ~MCPServer();

  void startStdio();
  bool startHttp(int port);
  void drain();
  void stop();
  std::string dispatchLine(const std::string& text);
  bool stdioClosed() const { return stdio_closed_.load(); }

 private:
  struct Command {
    std::string tool;
    nlohmann::json args;
    std::promise<nlohmann::json> result;
    std::string err;
  };

  nlohmann::json buildToolsList() const;
  nlohmann::json runTool(const std::string& name, const nlohmann::json& args,
                         std::string& err);
  void stopHttp();
  static int HttpHandler(mg_connection* conn, void* user);

  McpHost* host_ = nullptr;
  std::mutex queue_mu_;
  std::deque<std::shared_ptr<Command>> queue_;
  std::atomic<bool> running_{true};
  std::atomic<bool> stdio_closed_{false};
  std::thread stdio_thread_;
  bool stdio_started_ = false;
  mg_context* http_ctx_ = nullptr;
};

}  // namespace tusdql
