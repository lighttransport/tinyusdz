#include "mcp-server.hh"

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

namespace tinyusdz {
namespace tydra {

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
using MethodHandler = std::function<nlohmann::json(const nlohmann::json&)>;

class MCPServer::Impl {
 public:
  // Constructor and destructor
  Impl() = default;
  ~Impl() {
    if (ctx_) {
      mg_stop(ctx_);
    }
  }

  // Initialize the server with the specified port and host
  bool init(int port, const std::string &host = "localhost");

  // Run the server
  bool run();
  
  // Register a JSON-RPC method handler
  void register_method(const std::string& method, MethodHandler handler);

 private:
  struct mg_context *ctx_ = nullptr; // Pointer to the CivetWeb context
  std::map<std::string, MethodHandler> method_handlers_;
  
  // Static callback for HTTP requests
  static int http_handler(struct mg_connection *conn, void *user_data);
  
  // Process JSON-RPC request
  JsonRpcResponse process_request(const JsonRpcRequest& request);
  
  // Parse JSON-RPC request from string
  JsonRpcRequest parse_request(const std::string& json_str);
  
  // Create JSON-RPC error response
  JsonRpcResponse create_error_response(int code, const std::string& message, const nlohmann::json& id = nullptr);
};

// Static HTTP handler implementation
int MCPServer::Impl::http_handler(struct mg_connection *conn, void *user_data) {
  MCPServer::Impl* server = static_cast<MCPServer::Impl*>(user_data);
  
  const struct mg_request_info *request_info = mg_get_request_info(conn);
  
  // Handle POST requests for JSON-RPC
  if (strcmp(request_info->request_method, "POST") == 0) {
    // Read request body
    std::string body;
    char buffer[1024];
    int bytes_read;
    
    while ((bytes_read = mg_read(conn, buffer, sizeof(buffer))) > 0) {
      body.append(buffer, size_t(bytes_read));
    }
    
    // Parse and process JSON-RPC request
    JsonRpcRequest rpc_request = server->parse_request(body);
    JsonRpcResponse rpc_response = server->process_request(rpc_request);
    
    // Send JSON-RPC response
    std::string response_json = rpc_response.to_json().dump();
    
    mg_printf(conn,
              "HTTP/1.1 200 OK\r\n"
              "Content-Type: application/json\r\n"
              "Content-Length: %d\r\n"
              "\r\n"
              "%s",
              static_cast<int>(response_json.length()),
              response_json.c_str());
    
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

JsonRpcRequest MCPServer::Impl::parse_request(const std::string& json_str) {
  JsonRpcRequest request;
  
    nlohmann::json json_obj = nlohmann::json::parse(json_str);
    
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

JsonRpcResponse MCPServer::Impl::process_request(const JsonRpcRequest& request) {
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
    nlohmann::json result = handler_it->second(request.params);
    
    JsonRpcResponse response;
    response.id = request.id;
    response.result = result;
    
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
  // TODO
  (void)host;

  // CivetWeb options
  std::string port_str = std::to_string(port);
  std::vector<const char *> options;

  options.push_back("listening_ports");
  options.push_back(port_str.c_str());
  options.push_back("num_threads");
  options.push_back("4");

  // Initialize the server
  ctx_ = mg_start(NULL, this, options.data());
  if (!ctx_) {
    return false; // Failed to start server
  }
  
  // Register HTTP handler for JSON-RPC endpoint
  mg_set_request_handler(ctx_, "/jsonrpc", http_handler, this);
  
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

MCPServer::MCPServer() : impl_(new tydra::MCPServer::Impl()) {}
bool MCPServer::init(int port, const std::string &host) {
  return impl_->init(port, host);
}

} // namespace tydra
} // namespace tinyusdz


#else

namespace tinyusdz {
namespace tydra {

MCPServer::MCPServer() {}
bool MCPServer::init(int port, const std::string &host) {
  (void)port;
  (void)host;
  return false;
}

} // namespace tydra
} // namespace tinyusdz

#endif

