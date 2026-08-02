// SPDX-License-Identifier: Apache-2.0
#include "mcp/mcp_server.hh"

#include <string>

#include "mcp/mcp_host.hh"

#if defined(__unix__) || defined(__APPLE__)
#include <poll.h>
#include <unistd.h>
#else
#include <iostream>
#endif

namespace tusdql {

void MCPServer::startStdio() {
  if (stdio_started_) return;
  stdio_started_ = true;
  stdio_thread_ = std::thread([this]() {
#if defined(__unix__) || defined(__APPLE__)
    std::string buffer;
    char chunk[4096];
    while (running_) {
      pollfd pfd;
      pfd.fd = 0;
      pfd.events = POLLIN;
      pfd.revents = 0;
      const int result = ::poll(&pfd, 1, 200);
      if (result <= 0) continue;
      const ssize_t count = ::read(0, chunk, sizeof(chunk));
      if (count <= 0) {
        stdio_closed_.store(true);
        break;
      }
      buffer.append(chunk, static_cast<size_t>(count));
      size_t newline = 0;
      while ((newline = buffer.find('\n')) != std::string::npos) {
        const std::string line = buffer.substr(0, newline);
        buffer.erase(0, newline + 1);
        if (line.empty()) continue;
        const std::string response = dispatchLine(line);
        if (!response.empty()) {
          std::string output = response;
          output.push_back('\n');
          if (::write(1, output.data(), output.size()) < 0) {
            stdio_closed_.store(true);
            return;
          }
        }
      }
    }
#else
    std::string line;
    while (running_ && std::getline(std::cin, line)) {
      if (line.empty()) continue;
      const std::string response = dispatchLine(line);
      if (!response.empty()) {
        std::cout << response << "\n";
        std::cout.flush();
      }
    }
    stdio_closed_.store(true);
#endif
  });
}

}  // namespace tusdql
