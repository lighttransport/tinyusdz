// SPDX-License-Identifier: Apache-2.0
#include "stream/stream_server.hh"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>

#include "external/civetweb/civetweb.h"
#include "external/jsonhpp/nlohmann/json.hpp"
#include "log.hh"

namespace tusdview {

namespace {
using json = nlohmann::json;

// Browser viewer: a header bar + left/right HTML control panes around a center
// canvas. The canvas draws streamed binary image frames (the full tusdview
// window, including ImGui) preserving aspect (object-fit:contain) and forwards
// raw mouse/keyboard events (in image-pixel space) so ImGui widgets are
// clickable and the camera navigates when ImGui isn't capturing the mouse.
const char* kViewerHtml = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<title>tusdview stream</title>
<style>
*{box-sizing:border-box}
html,body{margin:0;height:100%;background:#15171c;color:#cdd3df;font:13px system-ui,sans-serif}
body{display:grid;grid-template-rows:42px 1fr;grid-template-columns:210px 1fr 210px;
 grid-template-areas:"head head head" "left center right";height:100vh}
#head{grid-area:head;display:flex;align-items:center;gap:10px;padding:0 12px;background:#1d2026;border-bottom:1px solid #2c313b}
#head b{color:#7fb0ff;font-size:14px}
#head input{flex:0 1 360px;background:#0f1115;border:1px solid #2c313b;color:#cdd3df;padding:4px 6px;border-radius:4px}
#stat{margin-left:auto;color:#7d8595;font:12px monospace}
.pane{background:#1a1d23;overflow:auto;padding:10px}
#left{grid-area:left;border-right:1px solid #2c313b}
#right{grid-area:right;border-left:1px solid #2c313b}
.pane h3{margin:4px 0 8px;font-size:11px;text-transform:uppercase;letter-spacing:.5px;color:#7d8595}
button,select{width:100%;margin:3px 0;background:#262b34;border:1px solid #353c48;color:#cdd3df;padding:6px;border-radius:4px;cursor:pointer}
button:hover{background:#2f3540}
.row{display:flex;gap:6px}.row button{width:auto;flex:1}
#drop{border:1px dashed #3a4150;border-radius:6px;padding:14px;text-align:center;color:#7d8595;margin:6px 0}
#drop.hot{border-color:#7fb0ff;color:#7fb0ff}
#center{grid-area:center;position:relative;background:#0c0d10;overflow:hidden}
#c{position:absolute;inset:0;width:100%;height:100%;object-fit:contain;cursor:crosshair;background:#0c0d10}
small{color:#6b7280}
</style></head><body>
<div id="head"><b>tusdview</b>
 <input id="path" placeholder="server-side USD path, then Enter (e.g. /data/scene.usda)">
 <span id="stat">connecting…</span></div>

<div id="left" class="pane">
 <h3>Scene</h3>
 <button id="openbtn">Open path</button>
 <div id="drop">Drop .usd/.usdz here<br><small>or click to upload</small>
  <input id="file" type="file" accept=".usd,.usda,.usdc,.usdz" style="display:none"></div>
 <h3>Render</h3>
 <label><small>Stream codec</small></label>
 <select id="codec"><option value="jpeg">JPEG (fast)</option>
  <option value="png">PNG (lossless)</option><option value="qoi">QOI</option></select>
</div>

<div id="center"><canvas id="c"></canvas></div>

<div id="right" class="pane">
 <h3>View</h3>
 <button data-k="f">Fit to scene (F)</button>
 <button data-k="w">Wireframe (W)</button>
 <h3>Presets</h3>
 <div class="row"><button data-k="1">Front</button><button data-k="3">Right</button></div>
 <div class="row"><button data-k="7">Top</button><button data-k="0">Iso</button></div>
 <h3>Help</h3>
 <small>Left-drag orbit · Shift+drag or middle-drag pan · right-drag / wheel dolly. ImGui widgets in the view are clickable.</small>
</div>

<script>
const cv=document.getElementById('c'),cx=cv.getContext('2d'),stat=document.getElementById('stat');
const ws=new WebSocket((location.protocol==='https:'?'wss://':'ws://')+location.host+'/stream');
ws.binaryType='blob';
let frames=0,last=performance.now();
ws.onopen=()=>{stat.textContent='connected';};
ws.onclose=()=>stat.textContent='disconnected';
ws.onerror=()=>stat.textContent='error';
ws.onmessage=ev=>{createImageBitmap(ev.data).then(b=>{
  if(cv.width!==b.width||cv.height!==b.height){cv.width=b.width;cv.height=b.height;}
  cx.drawImage(b,0,0); b.close(); frames++;
  const now=performance.now(); if(now-last>1000){stat.textContent=frames+' fps · '+cv.width+'×'+cv.height;frames=0;last=now;}
});};
function send(o){ if(ws.readyState===1) ws.send(JSON.stringify(o)); }

// Map a DOM event to image-pixel coords inside the contain-fitted canvas.
function coords(e){
  const r=cv.getBoundingClientRect(), iw=cv.width||1, ih=cv.height||1;
  const s=Math.min(r.width/iw, r.height/ih), dw=iw*s, dh=ih*s;
  const ox=r.left+(r.width-dw)/2, oy=r.top+(r.height-dh)/2;
  let x=(e.clientX-ox)/s, y=(e.clientY-oy)/s;
  x=Math.max(0,Math.min(iw,x)); y=Math.max(0,Math.min(ih,y));
  return {x,y};
}
const mods=e=>({shift:e.shiftKey,ctrl:e.ctrlKey,alt:e.altKey});
let pendMove=null;
function flushMove(){ if(pendMove){send(pendMove);pendMove=null;} requestAnimationFrame(flushMove); }
requestAnimationFrame(flushMove);
cv.addEventListener('contextmenu',e=>e.preventDefault());
cv.addEventListener('mousemove',e=>{const p=coords(e);pendMove={t:'m',x:p.x,y:p.y,...mods(e)};});
cv.addEventListener('mousedown',e=>{const p=coords(e);send({t:'d',b:e.button,x:p.x,y:p.y,...mods(e)});});
window.addEventListener('mouseup',e=>{const p=coords(e);send({t:'u',b:e.button,x:p.x,y:p.y});});
cv.addEventListener('wheel',e=>{e.preventDefault();const p=coords(e);send({t:'w',wheel:(e.deltaY>0?-1:1),x:p.x,y:p.y});},{passive:false});
window.addEventListener('keydown',e=>{ if(e.target.tagName==='INPUT')return;
  if(e.key.length===1||['Delete','Backspace','Escape'].includes(e.key)) send({t:'k',k:e.key}); });

// Controls.
document.querySelectorAll('[data-k]').forEach(b=>b.onclick=()=>send({t:'k',k:b.dataset.k}));
document.getElementById('codec').onchange=e=>send({t:'codec',v:e.target.value});
const path=document.getElementById('path');
function openPath(){ if(path.value.trim()) send({t:'load',path:path.value.trim()}); }
document.getElementById('openbtn').onclick=openPath;
path.addEventListener('keydown',e=>{ if(e.key==='Enter') openPath(); });

// Upload (drag-drop or click).
const drop=document.getElementById('drop'),file=document.getElementById('file');
drop.onclick=()=>file.click();
file.onchange=()=>{ if(file.files[0]) upload(file.files[0]); };
['dragenter','dragover'].forEach(ev=>drop.addEventListener(ev,e=>{e.preventDefault();drop.classList.add('hot');}));
['dragleave','drop'].forEach(ev=>drop.addEventListener(ev,e=>{e.preventDefault();drop.classList.remove('hot');}));
drop.addEventListener('drop',e=>{ if(e.dataTransfer.files[0]) upload(e.dataTransfer.files[0]); });
async function upload(f){
  stat.textContent='uploading '+f.name+'…';
  try{ const r=await fetch('/upload?name='+encodeURIComponent(f.name),{method:'POST',body:f});
       const j=await r.json(); stat.textContent=j.ok?('loaded '+j.path):('upload failed: '+(j.error||'')); }
  catch(err){ stat.textContent='upload error'; }
}
</script></body></html>)HTML";

StreamServer* Self(void* u) { return static_cast<StreamServer*>(u); }

// Sanitize an uploaded filename to a bare basename (no path traversal).
std::string SafeBasename(const std::string& name) {
  size_t slash = name.find_last_of("/\\");
  std::string base = (slash == std::string::npos) ? name : name.substr(slash + 1);
  if (base.empty() || base == "." || base == "..") base = "upload.usdz";
  return base;
}
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
  auto num = [&](const char* k) -> float {
    return (j.contains(k) && j[k].is_number()) ? j[k].get<float>() : 0.f;
  };
  auto boolean = [&](const char* k) -> bool {
    return j.contains(k) && j[k].is_boolean() && j[k].get<bool>();
  };
  auto str = [&](const char* k) -> std::string {
    return (j.contains(k) && j[k].is_string()) ? j[k].get<std::string>() : "";
  };
  StreamNav cmd;
  if (t == "m") {
    cmd.type = StreamNav::MouseMove; cmd.x = num("x"); cmd.y = num("y");
    cmd.shift = boolean("shift"); cmd.ctrl = boolean("ctrl"); cmd.alt = boolean("alt");
  } else if (t == "d" || t == "u") {
    cmd.type = StreamNav::MouseButton; cmd.down = (t == "d");
    cmd.button = static_cast<int>(num("b"));
    cmd.x = num("x"); cmd.y = num("y");
    cmd.shift = boolean("shift"); cmd.ctrl = boolean("ctrl"); cmd.alt = boolean("alt");
  } else if (t == "w") {
    cmd.type = StreamNav::Wheel; cmd.wheel = num("wheel");
    cmd.x = num("x"); cmd.y = num("y");
  } else if (t == "k") {
    cmd.type = StreamNav::Key; cmd.str = str("k"); cmd.down = true;
  } else if (t == "load") {
    cmd.type = StreamNav::Load; cmd.str = str("path");
    if (cmd.str.empty()) return 1;
  } else if (t == "codec") {
    cmd.type = StreamNav::Codec; cmd.str = str("v");
  } else if (t == "resize") {
    cmd.type = StreamNav::Resize;
    cmd.w = static_cast<int>(num("w")); cmd.h = static_cast<int>(num("h"));
  } else {
    return 1;
  }
  s->enqueue(cmd);
  return 1;
}

int StreamServer::httpHandler(mg_connection* conn, void*) {
  mg_printf(conn,
            "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
            "Access-Control-Allow-Origin: *\r\nContent-Length: %d\r\n\r\n%s",
            static_cast<int>(std::strlen(kViewerHtml)), kViewerHtml);
  return 200;
}

// POST /upload?name=foo.usdz  — body is the raw file. Saves it under the system
// temp dir and queues a Load so the main thread opens it.
int StreamServer::uploadHandler(mg_connection* conn, void* u) {
  StreamServer* s = Self(u);
  const mg_request_info* ri = mg_get_request_info(conn);
  auto reply = [&](bool ok, const std::string& msg) {
    json r = ok ? json{{"ok", true}, {"path", msg}}
                : json{{"ok", false}, {"error", msg}};
    const std::string body = r.dump();
    mg_printf(conn,
              "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
              "Access-Control-Allow-Origin: *\r\nContent-Length: %d\r\n\r\n%s",
              static_cast<int>(body.size()), body.c_str());
    return 200;
  };
  if (!ri || std::strcmp(ri->request_method, "POST") != 0)
    return reply(false, "POST required");

  char nameBuf[512] = "upload.usdz";
  if (ri->query_string)
    mg_get_var(ri->query_string, std::strlen(ri->query_string), "name", nameBuf,
               sizeof(nameBuf));

  std::error_code ec;
  std::filesystem::path dir =
      std::filesystem::temp_directory_path(ec) / "tusdview_uploads";
  std::filesystem::create_directories(dir, ec);
  std::filesystem::path out = dir / SafeBasename(nameBuf);

  std::ofstream ofs(out, std::ios::binary | std::ios::trunc);
  if (!ofs) return reply(false, "cannot write temp file");
  char buf[65536];
  int n;
  long long total = 0;
  while ((n = mg_read(conn, buf, sizeof(buf))) > 0) {
    ofs.write(buf, n);
    total += n;
  }
  ofs.close();
  if (total <= 0) return reply(false, "empty upload");

  LOGI("stream: received upload %s (%lld bytes)", out.string().c_str(), total);
  StreamNav cmd;
  cmd.type = StreamNav::Load;
  cmd.str = out.string();
  s->enqueue(cmd);
  return reply(true, out.string());
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
  mg_set_request_handler(ctx_, "/upload", uploadHandler, this);
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

void StreamServer::enqueue(const StreamNav& cmd) {
  std::lock_guard<std::mutex> lk(inMu_);
  if (input_.size() < 8192) input_.push_back(cmd);  // cap backlog
}

std::vector<StreamNav> StreamServer::takeInput() {
  std::lock_guard<std::mutex> lk(inMu_);
  std::vector<StreamNav> out;
  out.swap(input_);
  return out;
}

}  // namespace tusdview
