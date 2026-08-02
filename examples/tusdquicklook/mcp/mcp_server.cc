// SPDX-License-Identifier: Apache-2.0
#include "mcp/mcp_server.hh"

#include <chrono>
#include <exception>

#include "mcp/mcp_host.hh"

namespace tusdql {

using nlohmann::json;

namespace {

enum JsonRpcError {
  kParseError = -32700,
  kInvalidRequest = -32600,
  kMethodNotFound = -32601,
  kInvalidParams = -32602,
};

std::string Result(const json& id, const json& value) {
  return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", value}}.dump();
}

std::string Error(const json& id, int code, const std::string& message) {
  return json{{"jsonrpc", "2.0"},
              {"id", id},
              {"error", {{"code", code}, {"message", message}}}}
      .dump();
}

json Tool(const char* name, const char* description, const json& properties,
          const json& required = json::array()) {
  json schema = {{"type", "object"}, {"properties", properties}};
  if (!required.empty()) schema["required"] = required;
  return { {"name", name}, {"description", description},
           {"inputSchema", schema} };
}

json NumberProperty(const char* description) {
  return {{"type", "number"}, {"description", description}};
}

json StringProperty(const char* description) {
  return {{"type", "string"}, {"description", description}};
}

}  // namespace

MCPServer::~MCPServer() { stop(); }

json MCPServer::buildToolsList() const {
  json tools = json::array();
  tools.push_back(Tool(
      "load_usd", "Start loading a USD/USDZ file; poll get_scene_info.",
      {{"path", StringProperty("path to a USD, USDA, USDC, or USDZ file")}},
      json::array({"path"})));
  tools.push_back(Tool(
      "get_scene_info",
      "Return load progress, scene statistics, bounds, degradation, and backend.",
      json::object()));
  tools.push_back(Tool(
      "list_prims", "List renderable mesh entries in the quicklook scene.",
      {{"max", {{"type", "integer"}, {"description", "maximum entries"}}}}));
  tools.push_back(Tool(
      "viewport",
      "Move the orbit camera: fit, home, isometric, front, back, right, left, "
      "top, bottom, orbit, pan, dolly, or set.",
      {{"op", StringProperty("camera operation")},
       {"dx", NumberProperty("orbit/pan horizontal pixels")},
       {"dy", NumberProperty("orbit/pan vertical pixels")},
       {"amount", NumberProperty("dolly amount")},
       {"target", {{"type", "array"}, {"items", {{"type", "number"}}}}},
       {"yaw", NumberProperty("absolute yaw in radians")},
       {"pitch", NumberProperty("absolute pitch in radians")},
       {"distance", NumberProperty("absolute orbit distance")}},
      json::array({"op"})));
  tools.push_back(Tool(
      "screenshot",
      "Render the current quicklook window to a PNG. Optional width/height "
      "make a temporary offscreen capture.",
      {{"path", StringProperty("output PNG path")},
       {"width", {{"type", "integer"}, {"description", "capture width"}}},
       {"height", {{"type", "integer"}, {"description", "capture height"}}}},
      json::array({"path"})));
  tools.push_back(Tool(
      "render_settings",
      "Query or set shading mode, shadows, image-based lighting, and exposure.",
      {{"mode", StringProperty("shaded|albedo|normal|uv|roughness|metallic|depth")},
       {"shadows", {{"type", "boolean"}}},
       {"ibl", {{"type", "boolean"}}},
       {"exposure", NumberProperty("exposure stops")}}));
  tools.push_back(Tool("quit", "Stop the interactive quicklook process.",
                       json::object()));
  return json{{"tools", tools}};
}

std::string MCPServer::dispatchLine(const std::string& text) {
  json request = json::parse(text, nullptr, false);
  if (request.is_discarded() || !request.is_object()) {
    return Error(nullptr, kParseError, "Parse error");
  }
  const json id = request.contains("id") ? request["id"] : json(nullptr);
  const bool notification = !request.contains("id") || request["id"].is_null();
  const std::string method = request.value("method", std::string());
  const json params = request.contains("params") && request["params"].is_object()
                          ? request["params"]
                          : json::object();
  if (request.contains("jsonrpc") && request["jsonrpc"].is_string() &&
      request["jsonrpc"].get<std::string>() != "2.0") {
    return notification ? std::string()
                        : Error(id, kInvalidRequest, "Unsupported jsonrpc version");
  }
  if (method.empty()) {
    return notification ? std::string()
                        : Error(id, kInvalidRequest, "Missing method");
  }

  if (method == "initialize") {
    return Result(id, json{{"protocolVersion", "2025-03-26"},
                           {"serverInfo", {{"name", "tusdquicklook-mcp"},
                                             {"version", "1.0.0"}}},
                           {"capabilities", {{"tools", json::object()}}}});
  }
  if (method == "notifications/initialized") return "";
  if (method == "ping") return Result(id, json::object());
  if (method == "tools/list") return Result(id, buildToolsList());
  if (method == "tools/call") {
    if (notification) return "";
    if (!params.contains("name") || !params["name"].is_string()) {
      return Error(id, kInvalidParams, "tools/call requires string 'name'");
    }
    const std::string name = params["name"].get<std::string>();
    const json args = params.contains("arguments") && params["arguments"].is_object()
                          ? params["arguments"]
                          : json::object();
    std::string err;
    const json payload = runTool(name, args, err);
    if (!err.empty()) return Error(id, kInvalidParams, err);
    return Result(id, json{{"content", json::array({
                                      json{{"type", "text"},
                                           {"text", payload.dump(2)}}})},
                           {"structuredContent", payload}});
  }
  return notification ? std::string()
                      : Error(id, kMethodNotFound, "Method not found: " + method);
}

json MCPServer::runTool(const std::string& name, const json& args,
                        std::string& err) {
  auto command = std::make_shared<Command>();
  command->tool = name;
  command->args = args;
  auto future = command->result.get_future();
  {
    std::lock_guard<std::mutex> lock(queue_mu_);
    if (!running_) {
      err = "server shutting down";
      return json::object();
    }
    queue_.push_back(command);
  }
  if (future.wait_for(std::chrono::seconds(30)) != std::future_status::ready) {
    err = "tool call timed out";
    return json::object();
  }
  json value = future.get();
  if (!command->err.empty()) err = command->err;
  return value;
}

void MCPServer::drain() {
  for (;;) {
    std::shared_ptr<Command> command;
    {
      std::lock_guard<std::mutex> lock(queue_mu_);
      if (queue_.empty()) return;
      command = queue_.front();
      queue_.pop_front();
    }
    json value;
    std::string err;
    try {
      if (!host_) {
        err = "no MCP host";
      } else if (command->tool == "load_usd") {
        value = host_->mcpLoadUsd(command->args, err);
      } else if (command->tool == "get_scene_info") {
        value = host_->mcpSceneInfo(command->args, err);
      } else if (command->tool == "list_prims") {
        value = host_->mcpListPrims(command->args, err);
      } else if (command->tool == "viewport") {
        value = host_->mcpViewport(command->args, err);
      } else if (command->tool == "screenshot") {
        value = host_->mcpScreenshot(command->args, err);
      } else if (command->tool == "render_settings") {
        value = host_->mcpRenderSettings(command->args, err);
      } else if (command->tool == "quit") {
        value = host_->mcpQuit(command->args, err);
      } else {
        err = "unknown tool: " + command->tool;
      }
    } catch (const std::exception& e) {
      err = std::string("tool exception: ") + e.what();
    }
    command->err = err;
    command->result.set_value(std::move(value));
  }
}

void MCPServer::stop() {
  if (!running_.exchange(false)) return;
  {
    std::lock_guard<std::mutex> lock(queue_mu_);
    for (const auto& command : queue_) {
      command->err = "server shutting down";
      command->result.set_value(json::object());
    }
    queue_.clear();
  }
  stopHttp();
  if (stdio_thread_.joinable()) stdio_thread_.join();
}

}  // namespace tusdql
