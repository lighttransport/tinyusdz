// SPDX-License-Identifier: Apache-2.0
// stdio transport for the MCP server: newline-delimited JSON-RPC on stdin/stdout.
// Logging stays on stderr (see log.hh) so stdout carries only protocol messages.
#include <string>

#include "log.hh"
#include "mcp/mcp_server.hh"

#if defined(__unix__) || defined(__APPLE__)
#include <poll.h>
#include <unistd.h>
#else
#include <iostream>
#endif

namespace tusdview {

void MCPServer::startStdio() {
  if (stdioStarted_) return;
  stdioStarted_ = true;
  stdioThread_ = std::thread([this]() {
#if defined(__unix__) || defined(__APPLE__)
    // Raw fd I/O with poll() so the thread exits promptly on shutdown
    // (running_=false) or stdin EOF, instead of blocking forever in getline.
    std::string buf;
    char chunk[4096];
    while (running_) {
      struct pollfd pfd;
      pfd.fd = 0;  // stdin
      pfd.events = POLLIN;
      pfd.revents = 0;
      const int pr = ::poll(&pfd, 1, 200);
      if (pr <= 0) continue;  // timeout/error -> re-check running_
      const ssize_t n = ::read(0, chunk, sizeof(chunk));
      if (n <= 0) break;  // EOF / error
      buf.append(chunk, static_cast<size_t>(n));
      size_t nl;
      while ((nl = buf.find('\n')) != std::string::npos) {
        std::string line = buf.substr(0, nl);
        buf.erase(0, nl + 1);
        if (line.empty()) continue;
        const std::string resp = dispatchLine(line);
        if (!resp.empty()) {
          std::string out = resp;
          out.push_back('\n');
          if (::write(1, out.data(), out.size()) < 0) return;  // stdout closed
        }
      }
    }
#else
    // Portable fallback: blocking line reads (shutdown relies on stdin EOF).
    std::string line;
    while (running_ && std::getline(std::cin, line)) {
      if (line.empty()) continue;
      const std::string resp = dispatchLine(line);
      if (!resp.empty()) {
        std::cout << resp << "\n";
        std::cout.flush();
      }
    }
#endif
  });
  LOGI("MCP stdio transport active (JSON-RPC on stdin/stdout)");
}

}  // namespace tusdview
