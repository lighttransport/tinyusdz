// SPDX-License-Identifier: Apache-2.0
// tusdrender — WebSocket browser streaming server. Keeps the scene + BVH
// resident, renders the current camera to memory on demand (reusing
// RenderImage), encodes via the shared image writer (JPEG/QOI/PNG) and pushes
// the frame to connected browsers over a WebSocket. Browser mouse/keyboard map
// to orbit / pan / dolly / resize, mirroring tusdview's stream viewer. Compiles
// to nothing unless TUSDRENDER_WITH_STREAM is defined.
#ifdef TUSDRENDER_WITH_STREAM

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#include "external/civetweb/civetweb.h"
#include "external/jsonhpp/nlohmann/json.hpp"
#include "image-writer.hh"
#include "tusdr_context.hh"

namespace tusdr {

namespace {
using json = nlohmann::json;
constexpr double kPi = 3.14159265358979323846;

// Same minimal canvas viewer as tusdview's stream server: draws streamed binary
// frames and sends navigation back as JSON over the same WebSocket.
const char *kViewerHtml = R"HTML(<!doctype html><html><head><meta charset="utf-8">
<title>tusdrender stream</title>
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
window.addEventListener('keydown',e=>{ if('f'.includes(e.key)) send({t:'key',k:e.key}); });
let rt; window.addEventListener('resize',()=>{clearTimeout(rt);rt=setTimeout(()=>send({t:'resize',w:innerWidth,h:innerHeight}),150);});
</script></body></html>)HTML";

// A browser navigation command parsed from a WebSocket text frame.
struct Nav {
  enum Type { Orbit, Pan, Dolly, Key, Resize } type;
  float dx = 0, dy = 0, amount = 0;
  std::string str;
  int w = 0, h = 0;
};

}  // namespace

// Orbit-camera streaming server: one civetweb context, a /stream WebSocket and
// the embedded viewer at /. The render loop runs on the calling (main) thread so
// RenderImage + BVH access stay single-threaded; civetweb worker threads only
// enqueue input and register/unregister live connections.
class StreamRenderServer {
 public:
  explicit StreamRenderServer(RenderContext &ctx) : ctx_(ctx) {
    // Seed the orbit state from the scene bounds so the first frame is framed.
    const Vec3 ext = Sub(ctx_.bounds.hi, ctx_.bounds.lo);
    radius_ = std::max(1.0e-3f, Length(ext) * 0.5f);
    center_ = Mul(Add(ctx_.bounds.lo, ctx_.bounds.hi), 0.5f);
    az_ = 30.0;
    el_ = 20.0;
    dist_scale_ = 2.5;
    applyCamera();
  }

  bool start(int port) {
    const std::string ports = std::to_string(port);
    const char *options[] = {"listening_ports", ports.c_str(), "num_threads",
                             "4", nullptr};
    ctx_web_ = mg_start(nullptr, this, options);
    if (!ctx_web_) {
      std::cerr << "tusdrender stream: failed to start HTTP server on port "
                << port << "\n";
      return false;
    }
    mg_set_websocket_handler(ctx_web_, "/stream", wsConnect, wsReady, wsData,
                             wsClose, this);
    mg_set_request_handler(ctx_web_, "/", httpHandler, this);
    std::cerr << "tusdrender stream server listening on http://localhost:"
              << port << "/ (WebSocket /stream)\n";
    return true;
  }

  // Block on the render loop: render whenever the camera/resolution is dirty and
  // at least one client is connected. Returns when the server is stopped.
  //
  // civetweb starts a connection's read loop slightly after its ready handler
  // fires, so a frame written immediately on connect can race that startup and
  // be dropped. We therefore cache the last encoded frame and re-send it for a
  // short window after every render / new connection (resend_) — cheap (network
  // only, no re-trace) and robust for new/reconnecting clients.
  void runLoop() {
    for (;;) {
      std::vector<Nav> batch;
      bool render = false, resend = false;
      {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait_for(lk, std::chrono::milliseconds(150), [&] {
          return stop_ || (!conns_.empty() && (dirty_ || resend_ > 0));
        });
        if (stop_) return;
        batch.swap(input_);
        render = dirty_ && !conns_.empty();
        if (render) dirty_ = false;
        resend = !render && resend_ > 0 && !conns_.empty();
      }
      for (const Nav &n : batch) applyNav(n);
      if (render) {
        renderAndPush();
      } else if (resend) {
        pushLast();
        std::lock_guard<std::mutex> lk(mu_);
        if (resend_ > 0) --resend_;
      }
    }
  }

  void stop() {
    {
      std::lock_guard<std::mutex> lk(mu_);
      stop_ = true;
    }
    cv_.notify_all();
    if (ctx_web_) {
      mg_stop(ctx_web_);
      ctx_web_ = nullptr;
    }
  }

  void setCodec(const std::string &c) { codec_ = c; }

 private:
  // ---- civetweb callbacks (run on worker threads) ----
  static StreamRenderServer *self(void *u) {
    return static_cast<StreamRenderServer *>(u);
  }

  static int wsConnect(const mg_connection *, void *) { return 0; }

  static void wsReady(mg_connection *c, void *u) {
    StreamRenderServer *s = self(u);
    std::lock_guard<std::mutex> lk(s->mu_);
    s->conns_.push_back(c);
    if (s->last_frame_.empty())
      s->dirty_ = true;       // first client: render the initial frame
    s->resend_ = kResendN;    // (re)send the latest frame so the new client gets it
    s->cv_.notify_all();
  }

  static void wsClose(const mg_connection *c, void *u) {
    StreamRenderServer *s = self(u);
    std::lock_guard<std::mutex> lk(s->mu_);
    s->conns_.erase(std::remove(s->conns_.begin(), s->conns_.end(),
                                const_cast<mg_connection *>(c)),
                    s->conns_.end());
  }

  static int wsData(mg_connection *, int bits, char *data, size_t len,
                    void *u) {
    const int opcode = bits & 0xf;
    if (opcode == MG_WEBSOCKET_OPCODE_CONNECTION_CLOSE) return 0;
    if (opcode != MG_WEBSOCKET_OPCODE_TEXT || !data || len == 0) return 1;
    json j = json::parse(std::string(data, len), nullptr, /*exceptions=*/false);
    if (!j.is_object() || !j.contains("t") || !j["t"].is_string()) return 1;
    const std::string t = j["t"].get<std::string>();
    auto num = [&](const char *k) -> float {
      return (j.contains(k) && j[k].is_number()) ? j[k].get<float>() : 0.f;
    };
    Nav n;
    if (t == "orbit") {
      n.type = Nav::Orbit; n.dx = num("dx"); n.dy = num("dy");
    } else if (t == "pan") {
      n.type = Nav::Pan; n.dx = num("dx"); n.dy = num("dy");
    } else if (t == "dolly") {
      n.type = Nav::Dolly; n.amount = num("amount");
    } else if (t == "key") {
      n.type = Nav::Key;
      if (j.contains("k") && j["k"].is_string())
        n.str = j["k"].get<std::string>();
    } else if (t == "resize") {
      n.type = Nav::Resize; n.w = int(num("w")); n.h = int(num("h"));
    } else {
      return 1;
    }
    StreamRenderServer *s = self(u);
    std::lock_guard<std::mutex> lk(s->mu_);
    if (s->input_.size() < 4096) s->input_.push_back(std::move(n));
    s->dirty_ = true;
    s->cv_.notify_all();
    return 1;
  }

  static int httpHandler(mg_connection *conn, void *) {
    mg_printf(conn,
              "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
              "Access-Control-Allow-Origin: *\r\nContent-Length: %d\r\n\r\n%s",
              int(std::strlen(kViewerHtml)), kViewerHtml);
    return 200;
  }

  // ---- camera / render (run on the loop thread) ----
  // Position the camera on the orbit sphere around center_ (+ pan offset),
  // mirroring the JS `tusdrender.orbit` math.
  void applyCamera() {
    const double az = az_ * kPi / 180.0;
    const double el = std::max(-89.0, std::min(89.0, el_)) * kPi / 180.0;
    const double dist = double(radius_) * dist_scale_;
    const Vec3 up = AxisVec(ctx_.up_axis);
    const Vec3 ref = (std::fabs(up.y) < 0.9f) ? Vec3{0, 1, 0} : Vec3{1, 0, 0};
    const Vec3 e0 = Normalize(Cross(up, ref));
    const Vec3 e1 = Normalize(Cross(up, e0));
    const float ce = float(std::cos(el)), se = float(std::sin(el));
    const Vec3 horiz = Add(Mul(e0, float(std::cos(az)) * ce),
                           Mul(e1, float(std::sin(az)) * ce));
    const Vec3 dir = Add(horiz, Mul(up, se));
    const Vec3 target = Add(center_, pan_);
    const Vec3 eye = Add(target, Mul(dir, float(dist)));
    CameraFrame &c = ctx_.camera;
    c.origin = eye;
    c.forward = Normalize(Sub(target, eye));
    c.right = Normalize(Cross(c.forward, up));
    if (Length(c.right) < 1.0e-6f) c.right = Vec3{1, 0, 0};
    c.up = Normalize(Cross(c.right, c.forward));
    c.ortho = false;
    c.znear = std::max(1.0e-4f, float(dist) * 1.0e-3f);
    c.zfar = float(dist) + radius_ * 4.0f;
  }

  void applyNav(const Nav &n) {
    switch (n.type) {
      case Nav::Orbit:
        az_ += n.dx * 0.4;
        el_ = std::max(-89.0, std::min(89.0, el_ + n.dy * 0.4));
        applyCamera();
        break;
      case Nav::Pan: {
        const float s = radius_ * 0.0025f;
        pan_ = Add(pan_, Add(Mul(ctx_.camera.right, -n.dx * s),
                             Mul(ctx_.camera.up, n.dy * s)));
        applyCamera();
        break;
      }
      case Nav::Dolly:
        dist_scale_ *= std::exp(-double(n.amount) * 0.1);
        dist_scale_ = std::max(1.0e-3, std::min(1.0e4, dist_scale_));
        applyCamera();
        break;
      case Nav::Key:
        if (n.str == "f") {  // reset framing
          az_ = 30.0; el_ = 20.0; dist_scale_ = 2.5; pan_ = Vec3{0, 0, 0};
          applyCamera();
        }
        break;
      case Nav::Resize:
        if (n.w > 0 && n.h > 0) {
          ctx_.width = std::max(1, n.w);
          ctx_.height = std::max(1, n.h);
        }
        break;
    }
  }

  void renderAndPush() {
    ctx_.opt.width = ctx_.width;
    tinyusdz::Image img = RenderImage(
        ctx_.scene, &ctx_.direct, ctx_.tris, ctx_.flat_mats, ctx_.lights,
        ctx_.ibl.valid ? &ctx_.ibl : nullptr, ctx_.camera, ctx_.opt,
        ctx_.height, ctx_.textures.empty() ? nullptr : &ctx_.textures,
        ctx_.tri_uvs.empty() ? nullptr : &ctx_.tri_uvs,
        ctx_.use_tlas ? ctx_.tlas : nullptr,
        ctx_.use_tlas ? &ctx_.blas : nullptr,
        ctx_.use_tlas ? &ctx_.instances : nullptr,
        ctx_.tri_colors.empty() ? nullptr : &ctx_.tri_colors,
        ctx_.tri_normals.empty() ? nullptr : &ctx_.tri_normals,
        ctx_.volumes.empty() ? nullptr : &ctx_.volumes);

    tinyusdz::image::WriteOption wopt;
    if (codec_ == "qoi") {
      wopt.format = tinyusdz::image::WriteImageFormat::QOI;
    } else if (codec_ == "png") {
      wopt.format = tinyusdz::image::WriteImageFormat::PNG;
    } else {
      wopt.format = tinyusdz::image::WriteImageFormat::JPEG;
      wopt.jpeg_quality = 80;
    }
    auto enc = tinyusdz::image::WriteImageToMemory(img, wopt);
    if (!enc) {
      std::cerr << "stream: encode failed: " << enc.error() << "\n";
      return;
    }
    {
      std::lock_guard<std::mutex> lk(mu_);
      last_frame_ = std::move(enc.value());
      resend_ = kResendN;  // re-send for a short window (beats the connect race)
    }
    pushLast();
  }

  // Write the cached frame to every live connection. Cross-thread writes must be
  // bracketed with mg_lock_connection/mg_unlock_connection.
  void pushLast() {
    std::lock_guard<std::mutex> lk(mu_);
    if (last_frame_.empty()) return;
    for (mg_connection *c : conns_) {
      mg_lock_connection(c);
      mg_websocket_write(c, MG_WEBSOCKET_OPCODE_BINARY,
                         reinterpret_cast<const char *>(last_frame_.data()),
                         last_frame_.size());
      mg_unlock_connection(c);
    }
  }

  RenderContext &ctx_;
  mg_context *ctx_web_ = nullptr;
  std::string codec_ = "jpeg";

  static constexpr int kResendN = 6;  // ticks to re-send a frame after a change

  std::mutex mu_;
  std::condition_variable cv_;
  std::vector<mg_connection *> conns_;
  std::vector<Nav> input_;
  std::vector<uint8_t> last_frame_;  // latest encoded frame (cached for resend)
  int resend_ = 0;                   // remaining re-send ticks
  bool dirty_ = true;
  bool stop_ = false;

  // Orbit state.
  Vec3 center_{0, 0, 0};
  Vec3 pan_{0, 0, 0};
  float radius_ = 1.0f;
  double az_ = 30.0, el_ = 20.0, dist_scale_ = 2.5;
};

int RunStreamServer(const Options &opt) {
  RenderContext ctx;
  if (!BuildRenderContext(opt, ctx)) return EXIT_FAILURE;
  if (opt.stats) PrintRTStats(ctx);

  StreamRenderServer server(ctx);
  server.setCodec(opt.stream_codec);
  if (!server.start(opt.stream_http)) return EXIT_FAILURE;
  std::cerr << "Press Ctrl-C to stop.\n";
  server.runLoop();
  server.stop();
  return EXIT_SUCCESS;
}

}  // namespace tusdr
#endif  // TUSDRENDER_WITH_STREAM
