// SPDX-License-Identifier: Apache-2.0
// lusdview - WebSocket image-streaming server. Streams encoded frames (JPEG/QOI/
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

namespace lusdview {

// One input event from a browser client. Raw mouse/keyboard events are injected
// into ImGui (so its widgets are clickable) and, when ImGui isn't capturing the
// mouse, drive the camera. Applied on the main thread via App::applyNavCommand().
struct StreamNav {
  enum Type {
    MouseMove,    // x,y (image-space cursor)
    MouseButton,  // button (DOM 0=L,1=M,2=R), down, x,y, modifiers
    Wheel,        // wheel delta, x,y
    Key,          // str = key name, down
    Load,         // str = USD path (server-side path or saved upload)
    Resize,       // w,h
    Codec         // str = "jpeg" | "qoi" | "png"
  } type{MouseMove};
  float x{0.f}, y{0.f};   // image-space cursor
  int button{0};          // DOM button: 0=left, 1=middle, 2=right
  bool down{false};       // MouseButton / Key press(true) vs release(false)
  bool shift{false}, ctrl{false}, alt{false};  // modifier state
  float wheel{0.f};       // Wheel delta (>0 = scroll up / zoom in)
  std::string str;        // Key name / Load path / Codec name
  int w{0}, h{0};         // Resize
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
  // Drain queued browser input events (call on the main thread each frame).
  std::vector<StreamNav> takeInput();
  // Enqueue an input event (used by the HTTP upload handler to trigger a load).
  void enqueue(const StreamNav& cmd);

  // civetweb callbacks (cbdata = this).
  static int wsConnect(const mg_connection*, void*);
  static void wsReady(mg_connection*, void*);
  static int wsData(mg_connection*, int bits, char* data, size_t len, void*);
  static void wsClose(const mg_connection*, void*);
  static int httpHandler(mg_connection*, void*);    // serves the "/" viewer page
  static int uploadHandler(mg_connection*, void*);  // POST /upload -> save + load

 private:
  mg_context* ctx_{nullptr};
  std::mutex connMu_;
  std::vector<mg_connection*> conns_;
  std::mutex inMu_;
  std::vector<StreamNav> input_;
};

}  // namespace lusdview
