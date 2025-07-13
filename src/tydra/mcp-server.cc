#include "mcp-server.hh"
#include "mcp-tools.hh"

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

// TODO
//
// Server notification
//
// [ ] tools/list_changed

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
using MethodHandler = std::function<nlohmann::json(const nlohmann::json&, std::string &)>;

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

  bool stop() {
    // Nothing to do here.
    // mg_stop() is called in dtor.
    return true;
  }
  
  // Register a JSON-RPC method handler
  void register_method(const std::string& method, MethodHandler handler);

 private:
  struct mg_context *ctx_ = nullptr; // Pointer to the CivetWeb context
  std::map<std::string, MethodHandler> method_handlers_;
  
  // Static callback for MCP requests(POST + jsonrpc)
  static int mcp_handler(struct mg_connection *conn, void *user_data);
  
  // Process JSON-RPC request
  JsonRpcResponse process_request(const JsonRpcRequest& request);
  
  // Parse JSON-RPC request from string
  JsonRpcRequest parse_request(const std::string& json_str);
  
  // Create JSON-RPC error response
  JsonRpcResponse create_error_response(int code, const std::string& message, const nlohmann::json& id = nullptr);
};

int MCPServer::Impl::mcp_handler(struct mg_connection *conn, void *user_data) {
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

    if (rpc_request.is_notification()) {
      // Just acknowledge. No response.
    } else {
    
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
  std::string err;
  nlohmann::json result = handler_it->second(request.params, err);
  
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
  // TODO
  (void)host;

  // Register MCP initialize method
  register_method("initialize", [](const nlohmann::json& params, std::string &err) -> nlohmann::json {
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

  register_method("tools/list",[](const nlohmann::json &params, std::string &err) -> nlohmann::json {

    (void)err;
    (void)params;

    nlohmann::json j;
    mcp::GetToolsList(j);
    
    return j;

  });

  register_method("tools/call",[](const nlohmann::json &params, std::string &err) -> nlohmann::json {

    (void)err;
    
    if (!params.contains("name")) {
      err = "`name` is missing in params.";
      return {};
    }

    std::string tool_name = params["name"];
    nlohmann::json empty{};
    nlohmann::json result;
    
    bool ret = mcp::CallTool(tool_name, params.contains("arguments") ? params["arguments"] : empty, result);

    if (!ret) {
      err = "Unknown tool: " + tool_name;
      return {};
    }

    return result;

  });

  register_method("notifications/initialized", [](const nlohmann::json& params, std::string &err) -> nlohmann::json {
    // no response required
    (void)params;
    (void)err;
    
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

MCPServer::MCPServer() : impl_(new tydra::mcp::MCPServer::Impl()) {}
bool MCPServer::init(int port, const std::string &host) {
  return impl_->init(port, host);
}

bool MCPServer::run() {
  return impl_->run();
}

bool MCPServer::stop() {
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

