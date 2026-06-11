// SPDX-License-Identifier: Apache-2.0
#include "mcp/mcp_server.hh"

#include <chrono>
#include <exception>

#include "log.hh"
#include "mcp/mcp_host.hh"
#include "tydra/js-script.hh"    // complete JSEngineState (held in Context by value)
#include "tydra/mcp-context.hh"  // tinyusdz::tydra::mcp::Context
#include "tydra/mcp-tools.hh"    // GetToolsList (library tool schemas)

namespace tusdview {

using nlohmann::json;

namespace {

enum JsonRpcError {
  PARSE_ERROR = -32700,
  INVALID_REQUEST = -32600,
  METHOD_NOT_FOUND = -32601,
  INVALID_PARAMS = -32602,
  INTERNAL_ERROR = -32603,
};

std::string makeResult(const json& id, const json& result) {
  return json{{"jsonrpc", "2.0"}, {"id", id}, {"result", result}}.dump();
}
std::string makeError(const json& id, int code, const std::string& msg) {
  return json{{"jsonrpc", "2.0"},
              {"id", id},
              {"error", {{"code", code}, {"message", msg}}}}
      .dump();
}

// Tool schema helper.
json tool(const char* name, const char* desc, const json& props,
          const json& required = json::array()) {
  json schema = {{"type", "object"}, {"properties", props}};
  if (!required.empty()) schema["required"] = required;
  return {{"name", name}, {"description", desc}, {"inputSchema", schema}};
}
json numProp(const char* desc) { return {{"type", "number"}, {"description", desc}}; }
json strProp(const char* desc) { return {{"type", "string"}, {"description", desc}}; }

}  // namespace

MCPServer::~MCPServer() { stop(); }

json MCPServer::buildToolsList() const {
  json tools = json::array();
  tools.push_back(tool("load_usd", "Load a USD file (.usd/.usda/.usdc/.usdz) into "
                                   "the viewer (async; poll get_scene_info).",
                       {{"path", strProp("Path to the USD file")}},
                       json::array({"path"})));
  tools.push_back(tool("get_scene_info",
                       "Get the loaded scene: filepath, mesh/triangle counts, up "
                       "axis, and the world-space bounding box.",
                       json::object()));
  tools.push_back(tool("get_scene_bbox", "Get the scene world-space bounding box "
                                         "(alias of get_scene_info).",
                       json::object()));
  tools.push_back(tool("get_focused_prim",
                       "Info about the currently focused/selected prim: path, "
                       "vertex/triangle counts, bbox, and material.",
                       json::object()));
  tools.push_back(tool("set_focus", "Select/focus a prim by its absolute path; "
                                    "returns the same info as get_focused_prim.",
                       {{"path", strProp("Absolute prim path, e.g. /root/Mesh")}},
                       json::array({"path"})));
  tools.push_back(tool(
      "viewport",
      "Manipulate the camera. op=orbit|pan {dx,dy pixels} | dolly {amount} | fit "
      "{frame the scene} | home | isometric | front | back | right | left | top | bottom "
      "| bookmark_save {slot} | bookmark_load {slot} | set {absolute target[3],yaw,pitch,distance}.",
      {{"op", strProp("orbit | pan | dolly | fit | home | isometric | front | back | right | left | top | bottom | bookmark_save | bookmark_load | set")},
       {"dx", numProp("pixels (orbit/pan)")},
       {"dy", numProp("pixels (orbit/pan)")},
       {"amount", numProp("dolly amount (+ zooms in)")},
       {"slot", {{"type", "integer"}, {"description", "bookmark slot 1..3"}}},
       {"target", {{"type", "array"}, {"items", {{"type", "number"}}}}},
       {"yaw", numProp("azimuth radians (set)")},
       {"pitch", numProp("elevation radians (set)")},
       {"distance", numProp("distance to target (set)")}},
      json::array({"op"})));
  tools.push_back(tool("list_prims", "List renderable mesh prim paths in the scene.",
                       {{"max", {{"type", "integer"}, {"description", "cap (default 1000)"}}}}));
  tools.push_back(tool(
      "load_payloads",
      "Load deferred USD payloads (lazy loading). Omit 'paths' to load all "
      "deferred payloads; otherwise pass prim paths from get_scene_info's "
      "deferred_payloads. Async; poll get_scene_info.",
      {{"paths", {{"type", "array"},
                  {"items", {{"type", "string"}}},
                  {"description", "Deferred-payload prim paths (omit = all)"}}}}));

  // Append the tinyusdz library's USD tools (stage/prim/attr query, composition,
  // search, run_script, ...). GetToolsList emits static schemas (no stage), so it
  // is safe to call here on the transport thread.
  try {
    tinyusdz::tydra::mcp::Context ctx;
    json libList;
    if (tinyusdz::tydra::mcp::GetToolsList(ctx, libList) && libList.contains("tools") &&
        libList["tools"].is_array()) {
      for (auto& t : libList["tools"]) tools.push_back(t);
    }
  } catch (const std::exception& e) {
    LOGD("MCP: GetToolsList failed: %s", e.what());
  }
  return tools;
}

std::string MCPServer::dispatchLine(const std::string& text) {
  json j = json::parse(text, nullptr, /*allow_exceptions=*/false);
  if (j.is_discarded() || !j.is_object()) {
    return makeError(nullptr, PARSE_ERROR, "Parse error");
  }
  const json id = j.contains("id") ? j["id"] : json(nullptr);
  const bool isNotification = !j.contains("id") || j["id"].is_null();
  const std::string method = j.value("method", std::string());
  const json params =
      (j.contains("params") && j["params"].is_object()) ? j["params"] : json::object();

  if (j.contains("jsonrpc") && j["jsonrpc"].is_string() &&
      j["jsonrpc"].get<std::string>() != "2.0") {
    return isNotification ? std::string() : makeError(id, INVALID_REQUEST,
                                                      "Unsupported jsonrpc version");
  }
  if (method.empty()) {
    return isNotification ? std::string() : makeError(id, INVALID_REQUEST, "Missing method");
  }

  if (method == "initialize") {
    json result = {{"protocolVersion", "2025-03-26"},
                   {"serverInfo", {{"name", "tusdview-mcp"}, {"version", "1.0.0"}}},
                   {"capabilities", {{"tools", json::object()}}}};
    return makeResult(id, result);
  }
  if (method == "notifications/initialized") return "";  // notification
  if (method == "ping") return makeResult(id, json::object());
  if (method == "tools/list") return makeResult(id, json{{"tools", buildToolsList()}});

  if (method == "tools/call") {
    if (isNotification) return "";
    if (!params.contains("name") || !params["name"].is_string()) {
      return makeError(id, INVALID_PARAMS, "tools/call requires string 'name'");
    }
    const std::string name = params["name"].get<std::string>();
    const json arguments =
        (params.contains("arguments") && params["arguments"].is_object())
            ? params["arguments"]
            : json::object();
    std::string err;
    json payload = runTool(name, arguments, err);
    if (!err.empty()) return makeError(id, INVALID_PARAMS, err);
    json result = {
        {"content", json::array({json{{"type", "text"}, {"text", payload.dump(2)}}})},
        {"structuredContent", payload}};
    return makeResult(id, result);
  }

  return isNotification ? std::string()
                        : makeError(id, METHOD_NOT_FOUND, "Method not found: " + method);
}

json MCPServer::runTool(const std::string& name, const json& args, std::string& err) {
  auto cmd = std::make_shared<Command>();
  cmd->tool = name;
  cmd->args = args;
  auto fut = cmd->result.get_future();
  {
    std::lock_guard<std::mutex> lk(queueMu_);
    if (!running_) {
      err = "server shutting down";
      return json::object();
    }
    queue_.push_back(cmd);
  }
  if (fut.wait_for(std::chrono::seconds(30)) != std::future_status::ready) {
    err = "tool call timed out";
    return json::object();
  }
  json payload = fut.get();
  if (!cmd->err.empty()) err = cmd->err;
  return payload;
}

void MCPServer::drain() {
  for (;;) {
    std::shared_ptr<Command> cmd;
    {
      std::lock_guard<std::mutex> lk(queueMu_);
      if (queue_.empty()) break;
      cmd = queue_.front();
      queue_.pop_front();
    }
    json payload;
    std::string err;
    const std::string& t = cmd->tool;
    try {
      if (!host_) {
        err = "no host";
      } else if (t == "load_usd") {
        payload = host_->mcpLoadUsd(cmd->args, err);
      } else if (t == "get_scene_info" || t == "get_scene_bbox") {
        payload = host_->mcpSceneInfo(cmd->args, err);
      } else if (t == "get_focused_prim") {
        payload = host_->mcpGetFocusedPrim(cmd->args, err);
      } else if (t == "set_focus") {
        payload = host_->mcpSetFocus(cmd->args, err);
      } else if (t == "viewport") {
        payload = host_->mcpViewport(cmd->args, err);
      } else if (t == "list_prims") {
        payload = host_->mcpListPrims(cmd->args, err);
      } else if (t == "load_payloads") {
        payload = host_->mcpLoadPayloads(cmd->args, err);
      } else {
        // Not a viewer tool -> forward to the tinyusdz library tool dispatcher.
        payload = host_->mcpCallLibraryTool(t, cmd->args, err);
      }
    } catch (const std::exception& e) {
      err = std::string("tool exception: ") + e.what();
    }
    cmd->err = err;
    cmd->result.set_value(std::move(payload));
  }
}

void MCPServer::stop() {
  if (!running_.exchange(false)) return;  // already stopped

  // Wake any transport thread blocked on a queued tool call BEFORE joining the
  // transports (the civetweb worker that pushed it is blocked on its future, and
  // mg_stop() would otherwise deadlock waiting for that worker to return).
  {
    std::lock_guard<std::mutex> lk(queueMu_);
    for (auto& cmd : queue_) {
      cmd->err = "server shutting down";
      cmd->result.set_value(nlohmann::json::object());
    }
    queue_.clear();
  }
  stopHttp();  // mg_stop joins civetweb worker threads
  if (stdioThread_.joinable()) stdioThread_.join();
  LOGD("MCP server stopped");
}

}  // namespace tusdview
