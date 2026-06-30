// SPDX-License-Identifier: Apache-2.0
// tusdview - WebSocket image-streaming server. Streams encoded frames (JPEG/QOI/
// PNG) of the full window (incl. ImGui) to a browser over /stream, serves a small
// HTML/JS viewer at /, and receives camera navigation input from the browser.
// Independent civetweb context (own port) so it coexists with the MCP server.
#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

struct mg_context;
struct mg_connection;

namespace tusdview {

// One navigation command from a browser client. Mirrors the MCP viewport/input
// ops; applied on the main thread via App::applyNavCommand().
struct StreamNav {
  enum Type { Orbit, Pan, Dolly, Key, Preset, Frame, Resize } type{Orbit};
  float dx{0.f}, dy{0.f}, amount{0.f};
  std::string str;   // key (for Key) or preset name (for Preset)
  int w{0}, h{0};    // for Resize
};

class StreamServer {
 public:
  ~StreamServer();

  // Start the WebSocket/HTTP server on `port`. Returns false on bind failure.
  bool start(int port);
  void stop();
  bool running() const { return ctx_ != nullptr; }

  int clientCount();
  // Broadcast one encoded image frame (binary) to all connected clients.
  void pushFrame(const uint8_t* data, size_t len);
  // Drain queued browser nav commands (call on the main thread each frame).
  std::vector<StreamNav> takeInput();

  // civetweb callbacks (cbdata = this).
  static int wsConnect(const mg_connection*, void*);
  static void wsReady(mg_connection*, void*);
  static int wsData(mg_connection*, int bits, char* data, size_t len, void*);
  static void wsClose(const mg_connection*, void*);
  static int httpHandler(mg_connection*, void*);  // serves the "/" viewer page

 private:
  mg_context* ctx_{nullptr};
  std::mutex connMu_;
  std::vector<mg_connection*> conns_;
  std::mutex inMu_;
  std::vector<StreamNav> input_;
};

}  // namespace tusdview
