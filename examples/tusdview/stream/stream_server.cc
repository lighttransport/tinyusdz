// SPDX-License-Identifier: Apache-2.0
#include "stream/stream_server.hh"

#include <algorithm>
#include <cstring>

#include "external/civetweb/civetweb.h"
#include "external/jsonhpp/nlohmann/json.hpp"
#include "log.hh"

namespace tusdview {

namespace {
using json = nlohmann::json;

// Minimal browser viewer: draws streamed binary image frames onto a canvas and
// sends camera navigation (mouse/keyboard) back over the same WebSocket as JSON.
const char* kViewerHtml = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<title>tusdview stream</title>
<style>html,body{margin:0;height:100%;background:#111;overflow:hidden}
#c{display:block;width:100vw;height:100vh;cursor:grab;image-rendering:auto}
#s{position:fixed;left:8px;top:6px;color:#9cf;font:12px monospace;opacity:.8}</style>
</head><body><canvas id="c"></canvas><div id="s">connecting…</div>
<script>
const cv=document.getElementById('c'),cx=cv.getContext('2d'),st=document.getElementById('s');
const ws=new WebSocket((location.protocol==='https:'?'wss://':'ws://')+location.host+'/stream');
ws.binaryType='blob';
let frames=0,last=performance.now();
ws.onopen=()=>{st.textContent='connected'; send({t:'resize',w:innerWidth,h:innerHeight});};
ws.onclose=()=>st.textContent='disconnected';
ws.onmessage=ev=>{createImageBitmap(ev.data).then(b=>{
  if(cv.width!==b.width||cv.height!==b.height){cv.width=b.width;cv.height=b.height;}
  cx.drawImage(b,0,0); b.close(); frames++;
  const now=performance.now(); if(now-last>1000){st.textContent=frames+' fps  '+cv.width+'x'+cv.height;frames=0;last=now;}
});};
function send(o){ if(ws.readyState===1) ws.send(JSON.stringify(o)); }
let drag=false,btn=0,px=0,py=0;
cv.addEventListener('contextmenu',e=>e.preventDefault());
cv.addEventListener('mousedown',e=>{drag=true;btn=e.button;px=e.clientX;py=e.clientY;cv.style.cursor='grabbing';});
window.addEventListener('mouseup',()=>{drag=false;cv.style.cursor='grab';});
window.addEventListener('mousemove',e=>{ if(!drag)return; const dx=e.clientX-px,dy=e.clientY-py; px=e.clientX;py=e.clientY;
  if(btn===1||(btn===0&&e.shiftKey)) send({t:'pan',dx:dx,dy:dy});
  else if(btn===2) send({t:'dolly',amount:(dx-dy)*0.05});
  else send({t:'orbit',dx:dx,dy:dy}); });
cv.addEventListener('wheel',e=>{e.preventDefault(); send({t:'dolly',amount:(e.deltaY>0?-1:1)});},{passive:false});
window.addEventListener('keydown',e=>{ if('wfa01357'.includes(e.key)) send({t:'key',k:e.key}); });
let rt; window.addEventListener('resize',()=>{clearTimeout(rt);rt=setTimeout(()=>send({t:'resize',w:innerWidth,h:innerHeight}),150);});
</script></body></html>)HTML";

StreamServer* Self(void* u) { return static_cast<StreamServer*>(u); }
}  // namespace

StreamServer::~StreamServer() { stop(); }

int StreamServer::wsConnect(const mg_connection*, void*) { return 0; /* accept */ }

void StreamServer::wsReady(mg_connection* c, void* u) {
  StreamServer* s = Self(u);
  std::lock_guard<std::mutex> lk(s->connMu_);
  s->conns_.push_back(c);
}

void StreamServer::wsClose(const mg_connection* c, void* u) {
  StreamServer* s = Self(u);
  std::lock_guard<std::mutex> lk(s->connMu_);
  s->conns_.erase(std::remove(s->conns_.begin(), s->conns_.end(),
                              const_cast<mg_connection*>(c)),
                  s->conns_.end());
}

int StreamServer::wsData(mg_connection*, int bits, char* data, size_t len,
                         void* u) {
  const int opcode = bits & 0xf;
  if (opcode == MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE) return 0;
  if (opcode != MG_WEBSOCKET_OPCODE_TEXT || !data || len == 0) return 1;
  StreamServer* s = Self(u);
  json j = json::parse(std::string(data, len), nullptr, /*exceptions=*/false);
  if (!j.is_object() || !j.contains("t") || !j["t"].is_string()) return 1;
  const std::string t = j["t"].get<std::string>();
  StreamNav cmd;
  auto num = [&](const char* k) -> float {
    return (j.contains(k) && j[k].is_number()) ? j[k].get<float>() : 0.f;
  };
  if (t == "orbit") {
    cmd.type = StreamNav::Orbit; cmd.dx = num("dx"); cmd.dy = num("dy");
  } else if (t == "pan") {
    cmd.type = StreamNav::Pan; cmd.dx = num("dx"); cmd.dy = num("dy");
  } else if (t == "dolly") {
    cmd.type = StreamNav::Dolly; cmd.amount = num("amount");
  } else if (t == "key") {
    cmd.type = StreamNav::Key;
    if (j.contains("k") && j["k"].is_string()) cmd.str = j["k"].get<std::string>();
  } else if (t == "resize") {
    cmd.type = StreamNav::Resize;
    cmd.w = static_cast<int>(num("w")); cmd.h = static_cast<int>(num("h"));
  } else {
    return 1;
  }
  std::lock_guard<std::mutex> lk(s->inMu_);
  if (s->input_.size() < 4096) s->input_.push_back(std::move(cmd));  // cap backlog
  return 1;
}

int StreamServer::httpHandler(mg_connection* conn, void*) {
  mg_printf(conn,
            "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
            "Access-Control-Allow-Origin: *\r\nContent-Length: %d\r\n\r\n%s",
            static_cast<int>(std::strlen(kViewerHtml)), kViewerHtml);
  return 200;
}

bool StreamServer::start(int port) {
  const std::string ports = std::to_string(port);
  const char* options[] = {"listening_ports", ports.c_str(), "num_threads", "6",
                           nullptr};
  ctx_ = mg_start(nullptr, this, options);
  if (!ctx_) {
    LOGE("stream: failed to start HTTP server on port %d", port);
    return false;
  }
  mg_set_websocket_handler(ctx_, "/stream", wsConnect, wsReady, wsData, wsClose,
                           this);
  mg_set_request_handler(ctx_, "/", httpHandler, this);
  LOGI("stream server listening on http://localhost:%d/ (WebSocket /stream)", port);
  return true;
}

void StreamServer::stop() {
  if (ctx_) {
    mg_stop(ctx_);
    ctx_ = nullptr;
  }
  std::lock_guard<std::mutex> lk(connMu_);
  conns_.clear();
}

int StreamServer::clientCount() {
  std::lock_guard<std::mutex> lk(connMu_);
  return static_cast<int>(conns_.size());
}

void StreamServer::pushFrame(const uint8_t* data, size_t len) {
  if (!data || len == 0) return;
  std::lock_guard<std::mutex> lk(connMu_);
  for (mg_connection* c : conns_) {
    mg_websocket_write(c, MG_WEBSOCKET_OPCODE_BINARY,
                       reinterpret_cast<const char*>(data), len);
  }
}

std::vector<StreamNav> StreamServer::takeInput() {
  std::lock_guard<std::mutex> lk(inMu_);
  std::vector<StreamNav> out;
  out.swap(input_);
  return out;
}

}  // namespace tusdview
