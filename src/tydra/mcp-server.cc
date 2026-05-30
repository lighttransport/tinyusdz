#include "mcp-server.hh"
#include "mcp-tools.hh"
#include "mcp-resources.hh"
#include "mcp-context.hh"
#include "uuid-gen.hh"
#include "js-script.hh"

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
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

// Defines the DCOUT debug-logging macro used below. Without this, DCOUT(...)
// is undefined and its `<<` arguments are parsed as a real expression.
#include "common-macros.inc"

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

bool MethodRequiresSession(const std::string &method) {
  return (method != "initialize") &&
         (method != "ping") &&
         (method != "notifications/initialized");
}


} // namespace

class MCPServer::Impl {
 public:
  // Constructor and destructor
  explicit Impl(const MCPServerOptions &options) : options_(options) {}
  ~Impl() {
    if (ctx_) {
      mg_stop(ctx_);
    }
  }

  // Initialize the server with the specified port and host
  bool init(int port, const std::string &host = "localhost");

  // Run the server
  bool run();

  bool stop() {
    // Nothing to do here.
    // mg_stop() is called in dtor.
    return true;
  }
  
  // Register a JSON-RPC method handler
  void register_method(const std::string& method, MethodHandler handler);

  std::string genSessionID() {
    return uuid_gen_.generate();
  }

  void addSessionID(const std::string &s) {
    std::lock_guard<std::mutex> lock(mu_);
    sessions_.insert(s);
  }

 private:
  struct mg_context *ctx_ = nullptr; // Pointer to the CivetWeb context
  std::map<std::string, MethodHandler> method_handlers_;
  
  // Static callback for MCP requests(POST + jsonrpc)
  static int mcp_handler(struct mg_connection *conn, void *user_data);
  
  // Process JSON-RPC request
  JsonRpcResponse process_request(const JsonRpcRequest& request, const std::string &sess_id);
  
  // Parse JSON-RPC request from string
  JsonRpcRequest parse_request(const std::string& json_str);
  
  // Create JSON-RPC error response
  JsonRpcResponse create_error_response(int code, const std::string& message, const nlohmann::json& id = nullptr);

  MCPServerOptions options_;
  Context mcp_ctx_;
  UUIDGenerator uuid_gen_;
  std::mutex mu_;

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
    if ((request_info->content_length > 0) &&
        (size_t(request_info->content_length) > server->options_.max_request_body_bytes)) {
      mg_printf(conn,
                "HTTP/1.1 413 Payload Too Large\r\n"
                "Content-Length: 0\r\n"
                "\r\n");
      return 413;
    }

    // Read request body
    std::string body;
    char buffer[1024];
    int bytes_read;
    
    while ((bytes_read = mg_read(conn, buffer, sizeof(buffer))) > 0) {
      if (body.size() + size_t(bytes_read) > server->options_.max_request_body_bytes) {
        mg_printf(conn,
                  "HTTP/1.1 413 Payload Too Large\r\n"
                  "Content-Length: 0\r\n"
                  "\r\n");
        return 413;
      }
      body.append(buffer, size_t(bytes_read));
    }
    if (server->options_.log_request_body) {
      DCOUT("body " << body);
    } else {
      DCOUT("body size " << body.size() << " bytes");
    }
    
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
              "Access-Control-Allow-Headers: Content-Type, mcp-session-id\r\n"
              "\r\n");
    return 200;
  }
  
  return 404; // Not found
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

  if (json_obj.contains("jsonrpc") && json_obj["jsonrpc"].is_string()) {
    request.jsonrpc = json_obj["jsonrpc"].get<std::string>();
  } else {
    request.jsonrpc = "";
  }
  if (json_obj.contains("method") && json_obj["method"].is_string()) {
    request.method = json_obj["method"].get<std::string>();
  } else {
    request.method = "";
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
  std::lock_guard<std::mutex> lock(mu_);

  // Validate JSON-RPC version
  if (request.jsonrpc != "2.0") {
    return create_error_response(INVALID_REQUEST, "Invalid JSON-RPC version", request.id);
  }

  if (options_.require_session && MethodRequiresSession(request.method)) {
    if (sess_id.empty() || (sessions_.find(sess_id) == sessions_.end())) {
      return create_error_response(INVALID_REQUEST, "Missing or invalid mcp-session-id", request.id);
    }
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

bool MCPServer::Impl::init(int port, const std::string &host) {
  (void)host;

  register_method("ping", [](const nlohmann::json& params, const std::string &sess_id, std::string &err) -> nlohmann::json {
    (void)sess_id;
    (void)err;
    (void)params;

    // The receiver MUST respond promptly with an empty response
    return nlohmann::json{
      {"result", nlohmann::json::object()}};
  });

  // Register MCP initialize method
  register_method("initialize", [](const nlohmann::json& params, const std::string &sess_id, std::string &err) -> nlohmann::json {
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

  // CivetWeb options
  std::string port_str = std::to_string(port);
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
  
  return true; // Server initialized successfully
}

bool MCPServer::Impl::run() {
  if (!ctx_) {
    return false;
  }
  
  // Server is already running after mg_start
  // This method can be used for additional setup or monitoring
  return true;
}

MCPServer::MCPServer() : impl_(nullptr) {}
MCPServer::~MCPServer() {
  delete impl_;
  impl_ = nullptr;
}
bool MCPServer::init(int port, const std::string &host, const MCPServerOptions &options) {
  if (!impl_) {
    impl_ = new tydra::mcp::MCPServer::Impl(options);
  }
  return impl_->init(port, host);
}

bool MCPServer::init(int port, const std::string &host) {
  return init(port, host, MCPServerOptions{});
}

bool MCPServer::run() {
  if (!impl_) {
    return false;
  }
  return impl_->run();
}

bool MCPServer::stop() {
  if (!impl_) {
    return false;
  }
  return impl_->stop();
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

bool MCPServer::init(int port, const std::string &host, const MCPServerOptions &options) {
  (void)port;
  (void)host;
  (void)options;
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


} // namespace mcp
} // namespace tydra
} // namespace tinyusdz

#endif
