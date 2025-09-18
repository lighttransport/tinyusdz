#include "mcp-server.hh"
#include "mcp-tools.hh"
#include "mcp-resources.hh"
#include "mcp-context.hh"
#include "uuid-gen.hh"

#if defined(TINYUSDZ_WITH_MCP_SERVER)

#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "external/jsonhpp/nlohmann/json.hpp"

#include "external/civetweb/civetweb.h"

#ifdef __clang__
#pragma clang diagnostic pop
#endif

#include <functional>
#include <map>
#include <string>
#include <unordered_set>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <iostream>
#include <sstream>

// TODO
//
// [ ] Roots(from Protocol revision 2025-06-18)
// [ ] Use mcp-session-id in resources and tools.
// [ ] Server notification
//   [ ] resources/list_changed
//   [ ] tools/list_changed

namespace tinyusdz {
namespace tydra {
namespace mcp {

// JSON-RPC request structure
struct JsonRpcRequest {
  std::string jsonrpc = "2.0";
  std::string method;
  nlohmann::json params;
  nlohmann::json id;
  
  bool is_notification() const { return id.is_null(); }
};

// JSON-RPC response structure
struct JsonRpcResponse {
  std::string jsonrpc = "2.0";
  nlohmann::json result;
  nlohmann::json error;
  nlohmann::json id;
  
  nlohmann::json to_json() const {
    nlohmann::json response;
    response["jsonrpc"] = jsonrpc;
    response["id"] = id;
    
    if (!error.is_null()) {
      response["error"] = error;
    } else {
      response["result"] = result;
    }
    
    return response;
  }
};

// JSON-RPC error codes
enum JsonRpcErrorCode {
  PARSE_ERROR = -32700,
  INVALID_REQUEST = -32600,
  METHOD_NOT_FOUND = -32601,
  INVALID_PARAMS = -32602,
  INTERNAL_ERROR = -32603
};

// Method handler function type
// <params, sess_id, err>
using MethodHandler = std::function<nlohmann::json(const nlohmann::json&, const std::string &, std::string &)>;

// SSE connection structure
struct SSEConnection {
  struct mg_connection* conn;
  std::string session_id;
  bool active;

  SSEConnection(struct mg_connection* c, const std::string& sid)
    : conn(c), session_id(sid), active(true) {}
};

// SSE event structure
struct SSEEvent {
  std::string session_id;
  std::string event_type;
  std::string data;

  SSEEvent(const std::string& sid, const std::string& type, const std::string& d)
    : session_id(sid), event_type(type), data(d) {}
};


namespace {

bool has_header(const struct mg_request_info *ri, const char *name) {
    for (int i = 0; i < ri->num_headers; i++) {
        DCOUT("header" + std::string(ri->http_headers[i].name));
        if (strcmp(ri->http_headers[i].name, name) == 0) {
          return true;
        }
    }
    return false;
}

std::string get_header_value(const struct mg_request_info *ri, const char *name) {
    for (int i = 0; i < ri->num_headers; i++) {
        if (strcmp(ri->http_headers[i].name, name) == 0) {
            return std::string(ri->http_headers[i].value);
        }
    }
    return {};
}


} // namespace

class MCPServer::Impl {
 public:
  // Constructor and destructor
  Impl() = default;
  ~Impl() {
    stop_all_transports();
  }

  // Initialize the server with transport configuration
  bool init(const TransportConfig& config);

  // Legacy HTTP-only init for backwards compatibility
  bool init(int port, const std::string &host = "localhost");

  // Run the server
  bool run();

  bool stop() {
    return stop_all_transports();
  }

  // Register a JSON-RPC method handler
  void register_method(const std::string& method, MethodHandler handler);

  std::string genSessionID() {
    return uuid_gen_.generate();
  }

  void addSessionID(const std::string &s) {
    sessions_.insert(s);
  }

  // SSE-specific methods
  void set_sse_event_handler(std::function<void(const std::string&, const std::string&)> handler);
  void send_sse_event(const std::string& session_id, const std::string& event_type, const std::string& data);
  void broadcast_sse_event(const std::string& event_type, const std::string& data);

 private:
  // Transport configuration
  TransportConfig config_;

  // HTTP transport (existing)
  struct mg_context *ctx_ = nullptr;

  // SSE transport
  std::vector<SSEConnection> sse_connections_;
  std::mutex sse_connections_mutex_;
  std::queue<SSEEvent> sse_event_queue_;
  std::mutex sse_event_queue_mutex_;
  std::condition_variable sse_event_cv_;
  std::thread sse_worker_thread_;
  std::atomic<bool> sse_worker_running_{false};
  std::function<void(const std::string&, const std::string&)> sse_event_handler_;

  // Stdio transport
  std::thread stdio_thread_;
  std::atomic<bool> stdio_running_{false};
  std::atomic<bool> should_stop_{false};

  std::map<std::string, MethodHandler> method_handlers_;

  // Common methods
  bool stop_all_transports();

  // HTTP transport methods (existing)
  static int mcp_handler(struct mg_connection *conn, void *user_data);

  // SSE transport methods
  static int sse_handler(struct mg_connection *conn, void *user_data);
  void sse_worker();
  void cleanup_sse_connections();

  // Stdio transport methods
  void stdio_worker();

  // Transport-specific init methods
  void register_common_methods();
  bool init_http_transport();
  bool init_stdio_transport();

  // JSON-RPC processing methods
  JsonRpcResponse process_request(const JsonRpcRequest& request, const std::string &sess_id);
  JsonRpcRequest parse_request(const std::string& json_str);
  JsonRpcResponse create_error_response(int code, const std::string& message, const nlohmann::json& id = nullptr);

  // Shared data
  Context mcp_ctx_;
  UUIDGenerator uuid_gen_;
  std::unordered_set<std::string> sessions_;
};

int MCPServer::Impl::mcp_handler(struct mg_connection *conn, void *user_data) {
  MCPServer::Impl* server = static_cast<MCPServer::Impl*>(user_data);
  
  const struct mg_request_info *request_info = mg_get_request_info(conn);

  DCOUT("req_method " << request_info->request_method);

  std::string mcp_sess_id;
  // TODO: Support Mcp-Session-Id?
  if (has_header(request_info, "mcp-session-id")) {
    mcp_sess_id = get_header_value(request_info, "mcp-session-id");
    DCOUT("mcp-session-id " << mcp_sess_id);
  }
  
  // Handle POST requests for JSON-RPC
  if (strcmp(request_info->request_method, "POST") == 0) {
    // Read request body
    std::string body;
    char buffer[1024];
    int bytes_read;
    
    while ((bytes_read = mg_read(conn, buffer, sizeof(buffer))) > 0) {
      body.append(buffer, size_t(bytes_read));
    }
    DCOUT("body " << body);
    
    // Parse and process JSON-RPC request
    JsonRpcRequest rpc_request = server->parse_request(body);
    JsonRpcResponse rpc_response = server->process_request(rpc_request, mcp_sess_id);

    if (rpc_request.is_notification()) {
        // Return 202 
      mg_printf(conn,
                "HTTP/1.1 202 Accepted\r\n"
                "Content-Length: 0\r\n"
                "\r\n");
    } else {
    
      // Send JSON-RPC response
      std::string response_json = rpc_response.to_json().dump();

      if (rpc_request.method == "initialize") {

        std::string sess_id = server->genSessionID();
        server->addSessionID(sess_id);

        mg_printf(conn,
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Type: application/json\r\n"
                  "mcp-session-id: %s\r\n"
                  "Content-Length: %d\r\n"
                  "\r\n"
                  "%s",
                  sess_id.c_str(),
                  static_cast<int>(response_json.length()),
                  response_json.c_str());
      }
      
      else {
        mg_printf(conn,
                  "HTTP/1.1 200 OK\r\n"
                  "Content-Type: application/json\r\n"
                  "Content-Length: %d\r\n"
                  "\r\n"
                  "%s",
                  static_cast<int>(response_json.length()),
                  response_json.c_str());
      }
    }
    
    return 200; // Request handled
  }
  
  // Handle OPTIONS for CORS
  if (strcmp(request_info->request_method, "OPTIONS") == 0) {
    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Access-Control-Allow-Origin: *\r\n"
              "Access-Control-Allow-Methods: POST, OPTIONS\r\n"
              "Access-Control-Allow-Headers: Content-Type\r\n"
              "\r\n");
    return 200;
  }
  
  return 404; // Not found
}

int MCPServer::Impl::sse_handler(struct mg_connection *conn, void *user_data) {
  MCPServer::Impl* server = static_cast<MCPServer::Impl*>(user_data);

  const struct mg_request_info *request_info = mg_get_request_info(conn);

  if (strcmp(request_info->request_method, "GET") == 0) {
    // Set SSE headers
    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: text/event-stream\r\n"
              "Cache-Control: no-cache\r\n"
              "Connection: keep-alive\r\n");

    if (server->config_.enable_cors) {
      mg_printf(conn,
                "Access-Control-Allow-Origin: *\r\n"
                "Access-Control-Allow-Headers: Cache-Control\r\n");
    }

    mg_printf(conn, "\r\n");

    // Generate session ID for this SSE connection
    std::string session_id = server->genSessionID();
    server->addSessionID(session_id);

    // Send initial connection event
    mg_printf(conn, "event: connection\r\n");
    mg_printf(conn, "data: {\"sessionId\":\"%s\"}\r\n\r\n", session_id.c_str());

    // Add this connection to the SSE connections list
    {
      std::lock_guard<std::mutex> lock(server->sse_connections_mutex_);
      server->sse_connections_.emplace_back(conn, session_id);
    }

    // Keep connection alive until client disconnects
    // This handler will be called periodically to check if connection is still active
    return 200;
  }

  return 404;
}

JsonRpcRequest MCPServer::Impl::parse_request(const std::string& json_str) {
  JsonRpcRequest request;
  
  nlohmann::json json_obj;
  if (!nlohmann::json::accept(json_str)) {
    // Return invalid request on parse error
    request.method = "";
    request.jsonrpc = "";
    return request;
  }
  
  json_obj = nlohmann::json::parse(json_str);
  
  if (json_obj.contains("jsonrpc")) {
    request.jsonrpc = json_obj["jsonrpc"];
  }
  if (json_obj.contains("method")) {
    request.method = json_obj["method"];
  }
  if (json_obj.contains("params")) {
    request.params = json_obj["params"];
  }
  if (json_obj.contains("id")) {
    request.id = json_obj["id"];
  }
  
  return request;
}

JsonRpcResponse MCPServer::Impl::process_request(const JsonRpcRequest& request, const std::string &sess_id) {
  // Validate JSON-RPC version
  if (request.jsonrpc != "2.0") {
    return create_error_response(INVALID_REQUEST, "Invalid JSON-RPC version", request.id);
  }
  
  // Check if method exists
  auto handler_it = method_handlers_.find(request.method);
  if (handler_it == method_handlers_.end()) {
    return create_error_response(METHOD_NOT_FOUND, "Method not found", request.id);
  }
  
  // Execute method handler
  std::string err;
  nlohmann::json result = handler_it->second(request.params, sess_id, err);
  
  JsonRpcResponse response;

  if (err.size()) {
    response = create_error_response(INVALID_PARAMS, err, request.id);
  } else {
    response.id = request.id;
    response.result = result;
  }
  
  return response;
}

JsonRpcResponse MCPServer::Impl::create_error_response(int code, const std::string& message, const nlohmann::json& id) {
  JsonRpcResponse response;
  response.id = id;
  response.error = nlohmann::json{
    {"code", code},
    {"message", message}
  };
  
  return response;
}

void MCPServer::Impl::register_method(const std::string& method, MethodHandler handler) {
  method_handlers_[method] = handler;
}

bool MCPServer::Impl::stop_all_transports() {
  should_stop_.store(true);

  // Stop HTTP/SSE server
  if (ctx_) {
    mg_stop(ctx_);
    ctx_ = nullptr;
  }

  // Stop SSE worker thread
  if (sse_worker_running_.load()) {
    sse_worker_running_.store(false);
    sse_event_cv_.notify_all();
    if (sse_worker_thread_.joinable()) {
      sse_worker_thread_.join();
    }
  }

  // Stop stdio worker thread
  if (stdio_running_.load()) {
    stdio_running_.store(false);
    if (stdio_thread_.joinable()) {
      stdio_thread_.join();
    }
  }

  return true;
}

void MCPServer::Impl::set_sse_event_handler(std::function<void(const std::string&, const std::string&)> handler) {
  sse_event_handler_ = handler;
}

void MCPServer::Impl::send_sse_event(const std::string& session_id, const std::string& event_type, const std::string& data) {
  {
    std::lock_guard<std::mutex> lock(sse_event_queue_mutex_);
    sse_event_queue_.emplace(session_id, event_type, data);
  }
  sse_event_cv_.notify_one();
}

void MCPServer::Impl::broadcast_sse_event(const std::string& event_type, const std::string& data) {
  {
    std::lock_guard<std::mutex> lock(sse_event_queue_mutex_);
    sse_event_queue_.emplace("", event_type, data);  // Empty session_id means broadcast
  }
  sse_event_cv_.notify_one();
}

void MCPServer::Impl::sse_worker() {
  while (sse_worker_running_.load()) {
    std::unique_lock<std::mutex> lock(sse_event_queue_mutex_);
    sse_event_cv_.wait(lock, [this] { return !sse_event_queue_.empty() || !sse_worker_running_.load(); });

    if (!sse_worker_running_.load()) break;

    while (!sse_event_queue_.empty()) {
      SSEEvent event = sse_event_queue_.front();
      sse_event_queue_.pop();
      lock.unlock();

      // Send event to appropriate connections
      {
        std::lock_guard<std::mutex> conn_lock(sse_connections_mutex_);
        auto it = sse_connections_.begin();
        while (it != sse_connections_.end()) {
          bool should_send = event.session_id.empty() || it->session_id == event.session_id;

          if (should_send && it->active) {
            int result = mg_printf(it->conn, "event: %s\r\ndata: %s\r\n\r\n",
                                  event.event_type.c_str(), event.data.c_str());
            if (result <= 0) {
              it->active = false;
            }
          }

          if (!it->active) {
            it = sse_connections_.erase(it);
          } else {
            ++it;
          }
        }
      }

      lock.lock();
    }
  }
}

void MCPServer::Impl::cleanup_sse_connections() {
  std::lock_guard<std::mutex> lock(sse_connections_mutex_);
  auto it = sse_connections_.begin();
  while (it != sse_connections_.end()) {
    if (!it->active) {
      it = sse_connections_.erase(it);
    } else {
      ++it;
    }
  }
}

void MCPServer::Impl::stdio_worker() {
  std::string line;
  while (stdio_running_.load() && !should_stop_.load()) {
    if (!std::getline(std::cin, line)) {
      break;  // EOF or error
    }

    if (line.empty()) continue;

    // Parse and process JSON-RPC request
    JsonRpcRequest rpc_request = parse_request(line);
    if (rpc_request.jsonrpc.empty()) {
      // Invalid JSON, send error
      JsonRpcResponse error_response = create_error_response(PARSE_ERROR, "Parse error", nullptr);
      std::cout << error_response.to_json().dump() << std::endl;
      continue;
    }

    JsonRpcResponse rpc_response = process_request(rpc_request, "stdio-session");

    if (!rpc_request.is_notification()) {
      std::cout << rpc_response.to_json().dump() << std::endl;
    }
  }
}

bool MCPServer::Impl::init(const TransportConfig& config) {
  config_ = config;
  should_stop_.store(false);

  // Register common MCP methods (same as before)
  register_common_methods();

  switch (config_.type) {
    case TransportType::HTTP_POST:
    case TransportType::HTTP_SSE:
      return init_http_transport();

    case TransportType::STDIO:
      return init_stdio_transport();

    default:
      return false;
  }
}

bool MCPServer::Impl::init(int port, const std::string &host) {
  // Legacy init - defaults to HTTP_POST transport
  TransportConfig config;
  config.type = TransportType::HTTP_POST;
  config.port = port;
  config.host = host;
  return init(config);
}

void MCPServer::Impl::register_common_methods() {
  register_method("ping", [](const nlohmann::json& params, const std::string &sess_id, std::string &err) -> nlohmann::json {
    (void)sess_id;
    (void)err;
    (void)params;

    // The receiver MUST respond promptly with an empty response
    return nlohmann::json{
      {"result", nlohmann::json::object()}};
  });

  // Register MCP initialize method
  register_method("initialize", [](const nlohmann::json& params, const std::string sess_id, std::string &err) -> nlohmann::json {
    (void)sess_id;
    (void)err;
    // Extract client info if provided
    std::string client_name = "unknown";
    std::string client_version = "unknown";
    
    if (params.contains("clientInfo")) {
      auto client_info = params["clientInfo"];
      if (client_info.contains("name")) {
        client_name = client_info["name"];
      }
      if (client_info.contains("version")) {
        client_version = client_info["version"];
      }
    }
    
    // Return server capabilities
    return nlohmann::json{
      {"protocolVersion", "2025-03-26"},
      {"serverInfo", {
        {"name", "tinyusdz-mcp-server"},
        {"version", "1.0.0"}
      }},
      {"capabilities", {
        {"tools", nlohmann::json::object()},
        {"resources", nlohmann::json::object()}
      }}
    };
  });

  register_method("notifications/cancelled",[](const nlohmann::json &params, const std::string &sess_id, std::string &err) -> nlohmann::json {
    (void)sess_id;

    if (!params.contains("requestId")) {
      err = "`requestId` is missing in params.";
      return {};
    }

    // TODO: Parse optional 'reason' string.
    err += "notifications/cancelled is TODO\n";

    return {};
  });

  register_method("resources/list",[this](const nlohmann::json &params, const std::string &sess_id, std::string &err) -> nlohmann::json {

    (void)err;
    (void)params;
    (void)sess_id;

    nlohmann::json j;
    mcp::GetResourcesList(mcp_ctx_, j);
    
    return j;

  });

  register_method("resources/read",[this](const nlohmann::json &params, const std::string &sess_id, std::string &err) -> nlohmann::json {

    (void)err;
    (void)params;
    (void)sess_id;

    if (!params.contains("uri")) {
      err = "`name` is missing in params.";
      return {};
    }

    std::string uri = params["uri"];
    nlohmann::json result;
    if (!mcp::ReadResource(mcp_ctx_, uri, result)) {
      err += "Failed to read resource: " + uri;
      return {};
    }
    
    return result;

  });

  register_method("tools/list",[this](const nlohmann::json &params, const std::string &sess_id, std::string &err) -> nlohmann::json {

    (void)err;
    (void)params;
    (void)sess_id;

    nlohmann::json j;
    mcp::GetToolsList(mcp_ctx_, j);
    
    return j;

  });

  register_method("tools/call",[this](const nlohmann::json &params, const std::string &sess_id, std::string &err) -> nlohmann::json {

    (void)sess_id;
    
    if (!params.contains("name")) {
      err = "`name` is missing in params.";
      return {};
    }

    std::string tool_name = params["name"];
    nlohmann::json empty{};
    nlohmann::json result;
    
    std::string err_;
    bool ret = mcp::CallTool(mcp_ctx_, tool_name, params.contains("arguments") ? params["arguments"] : empty, result, err_);

    if (!ret) {
      err = "Unknown tool: " + tool_name;
      return {};
    }

    return result;

  });

  register_method("notifications/initialized", [](const nlohmann::json& params, const std::string &sess_id, std::string &err) -> nlohmann::json {
    // no response required
    (void)params;
    (void)err;
    (void)sess_id;

    // Return server capabilities
    return nlohmann::json::object();
  });
}

bool MCPServer::Impl::init_http_transport() {
  // CivetWeb options
  std::string port_str = std::to_string(config_.port);
  std::vector<const char *> options;

  options.push_back("listening_ports");
  options.push_back(port_str.c_str());
  options.push_back("num_threads");
  options.push_back("4");
  options.push_back(nullptr);

  // Initialize the server
  ctx_ = mg_start(NULL, this, options.data());
  if (!ctx_) {
    return false; // Failed to start server
  }

  // Register HTTP handler for MCP endpoint
  mg_set_request_handler(ctx_, "/mcp", mcp_handler, this);

  // If SSE transport is enabled, also register SSE handler
  if (config_.type == TransportType::HTTP_SSE) {
    mg_set_request_handler(ctx_, config_.sse_endpoint.c_str(), sse_handler, this);

    // Start SSE worker thread
    sse_worker_running_.store(true);
    sse_worker_thread_ = std::thread(&MCPServer::Impl::sse_worker, this);
  }

  return true; // Server initialized successfully
}

bool MCPServer::Impl::init_stdio_transport() {
  // Stdio transport doesn't need server setup, just prepare for worker thread
  return true;
}

bool MCPServer::Impl::run() {
  switch (config_.type) {
    case TransportType::HTTP_POST:
    case TransportType::HTTP_SSE:
      if (!ctx_) {
        return false;
      }
      // HTTP Server is already running after mg_start
      // This method can be used for additional setup or monitoring
      return true;

    case TransportType::STDIO:
      // Start stdio worker thread and wait for it to complete
      stdio_running_.store(true);
      stdio_thread_ = std::thread(&MCPServer::Impl::stdio_worker, this);

      // Block until stdio worker completes
      if (stdio_thread_.joinable()) {
        stdio_thread_.join();
      }
      return true;

    default:
      return false;
  }
}

MCPServer::MCPServer() : impl_(new tydra::mcp::MCPServer::Impl()) {}

MCPServer::~MCPServer() {
  delete impl_;
}

bool MCPServer::init(const TransportConfig& config) {
  return impl_->init(config);
}

bool MCPServer::init(int port, const std::string &host) {
  return impl_->init(port, host);
}

bool MCPServer::run() {
  return impl_->run();
}

bool MCPServer::stop() {
  return impl_->stop();
}

void MCPServer::set_sse_event_handler(std::function<void(const std::string&, const std::string&)> handler) {
  impl_->set_sse_event_handler(handler);
}

} // namespace mcp
} // namespace tydra
} // namespace tinyusdz


#else

namespace tinyusdz {
namespace tydra {
namespace mcp {

MCPServer::MCPServer() {}

MCPServer::~MCPServer() {}

bool MCPServer::init(const TransportConfig& config) {
  (void)config;
  return false;
}

bool MCPServer::init(int port, const std::string &host) {
  (void)port;
  (void)host;
  return false;
}

bool MCPServer::run() {
  return false;
}

bool MCPServer::stop() {
  return false;
}

void MCPServer::set_sse_event_handler(std::function<void(const std::string&, const std::string&)> handler) {
  (void)handler;
}


} // namespace mcp
} // namespace tydra
} // namespace tinyusdz

#endif

