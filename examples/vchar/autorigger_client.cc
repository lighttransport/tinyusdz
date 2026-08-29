// SPDX-License-Identifier: MIT
#include "autorigger_client.hh"

#include "external/jsonhpp/nlohmann/json.hpp"

#if !defined(_WIN32)
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

#include <cerrno>
#include <cstring>

namespace vchar {
#if !defined(_WIN32)
namespace {
constexpr size_t kProtocolLimit = 1024u * 1024u;
bool WriteAll(int fd, const std::string& value) {
  size_t offset = 0;
  while (offset < value.size()) {
    const ssize_t n = write(fd, value.data() + offset, value.size() - offset);
    if (n > 0) offset += static_cast<size_t>(n);
    else if (n < 0 && errno == EINTR) continue;
    else return false;
  }
  return true;
}
bool ReadLine(int fd, std::string* pending, std::string* line,
              std::chrono::steady_clock::time_point deadline) {
  for (;;) {
    const size_t newline = pending->find('\n');
    if (newline != std::string::npos) {
      *line = pending->substr(0, newline); pending->erase(0, newline + 1); return true;
    }
    if (pending->size() > kProtocolLimit) return false;
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) return false;
    const int wait = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(deadline-now).count());
    pollfd descriptor{fd, POLLIN, 0};
    const int ready = poll(&descriptor, 1, wait);
    if (ready < 0 && errno == EINTR) continue;
    if (ready <= 0 || !(descriptor.revents & (POLLIN|POLLHUP))) return false;
    char buffer[4096]; const ssize_t n = read(fd, buffer, sizeof(buffer));
    if (n > 0) pending->append(buffer, static_cast<size_t>(n)); else return false;
  }
}
}  // namespace
#endif

AutoriggerResult RunWorkerRequest(const std::string& executable,
                                  const std::string& requestText,
                                  std::chrono::milliseconds timeout) {
  AutoriggerResult result;
#if defined(_WIN32)
  result.error = "autorigger launcher is not implemented on Windows";
  return result;
#else
  nlohmann::json request = nlohmann::json::parse(requestText, nullptr, false);
  if (executable.empty() || request.is_discarded() || !request.is_object() ||
      timeout.count() <= 0) { result.error = "invalid worker request"; return result; }
  int inputPipe[2], outputPipe[2], errorPipe[2];
  if (pipe(inputPipe) || pipe(outputPipe) || pipe(errorPipe)) { result.error = std::strerror(errno); return result; }
  const pid_t child = fork();
  if (child < 0) { result.error = std::strerror(errno); return result; }
  if (child == 0) {
    setpgid(0,0); dup2(inputPipe[0],STDIN_FILENO); dup2(outputPipe[1],STDOUT_FILENO); dup2(errorPipe[1],STDERR_FILENO);
    close(inputPipe[0]); close(inputPipe[1]); close(outputPipe[0]); close(outputPipe[1]); close(errorPipe[0]); close(errorPipe[1]);
    execl(executable.c_str(), executable.c_str(), "worker", static_cast<char*>(nullptr)); _exit(127);
  }
  setpgid(child,child); close(inputPipe[0]); close(outputPipe[1]); close(errorPipe[1]);
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  std::string pending, line; nlohmann::json response;
  const bool initialized = WriteAll(inputPipe[1], "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"rig.initialize\"}\n") &&
      ReadLine(outputPipe[0], &pending, &line, deadline);
  const bool called = initialized && WriteAll(inputPipe[1], request.dump()+"\n") &&
      ReadLine(outputPipe[0], &pending, &line, deadline);
  if (called) { response=nlohmann::json::parse(line,nullptr,false); if(!response.is_discarded()) { result.response=response.dump(); result.ok=response.contains("result"); if(!result.ok) result.error=response.dump(); } }
  close(inputPipe[1]); close(outputPipe[0]); int status=0;
  while(waitpid(child,&status,WNOHANG)==0) { if(std::chrono::steady_clock::now()>=deadline){result.timedOut=true;kill(-child,SIGTERM);waitpid(child,&status,0);break;} usleep(1000); }
  char errors[4096];const ssize_t bytes=read(errorPipe[0],errors,sizeof(errors)-1);close(errorPipe[0]);if(bytes>0&&!result.ok){errors[bytes]=0;result.error=errors;}
  result.exitCode=WIFEXITED(status)?WEXITSTATUS(status):-1;if(!called&&result.error.empty())result.error="worker request timed out or returned invalid JSON";
  return result;
#endif
}

AutoriggerResult RunAutorigger(const std::string& executable,
                              const std::string& asset,
                              const std::string& output,
                              std::chrono::milliseconds timeout) {
  AutoriggerResult result;
#if defined(_WIN32)
  result.error = "autorigger launcher is not implemented on Windows";
  return result;
#else
  if (executable.empty() || asset.empty() || output.empty() || timeout.count() <= 0) {
    result.error = "invalid autorigger launch parameters"; return result;
  }
  int inputPipe[2], outputPipe[2], errorPipe[2];
  if (pipe(inputPipe) || pipe(outputPipe) || pipe(errorPipe)) { result.error = std::strerror(errno); return result; }
  const pid_t child = fork();
  if (child < 0) { result.error = std::strerror(errno); return result; }
  if (child == 0) {
    setpgid(0,0); dup2(inputPipe[0],STDIN_FILENO); dup2(outputPipe[1],STDOUT_FILENO); dup2(errorPipe[1],STDERR_FILENO);
    close(inputPipe[0]); close(inputPipe[1]); close(outputPipe[0]); close(outputPipe[1]); close(errorPipe[0]); close(errorPipe[1]);
    execl(executable.c_str(), executable.c_str(), "worker", static_cast<char*>(nullptr)); _exit(127);
  }
  setpgid(child,child); close(inputPipe[0]); close(outputPipe[1]); close(errorPipe[1]);
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  std::string pending, line;
  auto request = [&](const nlohmann::json& value, nlohmann::json* response) {
    if (!WriteAll(inputPipe[1], value.dump()+"\n") || !ReadLine(outputPipe[0],&pending,&line,deadline)) return false;
    *response = nlohmann::json::parse(line, nullptr, false);
    return !response->is_discarded();
  };
  nlohmann::json response;
  bool protocol = request({{"jsonrpc","2.0"},{"id",1},{"method","rig.initialize"}},&response) && response.contains("result");
  if (protocol) protocol = request({{"jsonrpc","2.0"},{"id",2},{"method","rig.submit"},
      {"params",{{"asset_path",asset},{"output_layer",output},{"scope","facial"}}}},&response) && response.contains("result");
  std::string job = protocol ? response["result"].value("job_id",std::string()) : std::string();
  while (protocol && !job.empty()) {
    const std::string state=response["result"].value("state",std::string());
    if (state=="completed") { result.ok=true; result.response=response.dump(); break; }
    if (state=="failed" || state=="cancelled") { result.error=response.dump(); break; }
    protocol=request({{"jsonrpc","2.0"},{"id",3},{"method","rig.status"},{"params",{{"job_id",job}}}},&response);
  }
  close(inputPipe[1]); close(outputPipe[0]);
  int status=0;
  while (waitpid(child,&status,WNOHANG)==0) {
    if (std::chrono::steady_clock::now()>=deadline) { result.timedOut=true; kill(-child,SIGTERM); waitpid(child,&status,0); break; }
    usleep(1000);
  }
  char errors[4096]; const ssize_t errorBytes=read(errorPipe[0],errors,sizeof(errors)-1); close(errorPipe[0]);
  if(errorBytes>0){errors[errorBytes]=0;if(!result.ok)result.error=errors;}
  result.exitCode=WIFEXITED(status)?WEXITSTATUS(status):-1;
  if(!protocol && result.error.empty())result.error="invalid autorigger protocol response";
  return result;
#endif
}
}  // namespace vchar
