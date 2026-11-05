/**
 * @file http_server.h
 * @brief Minimal dependency-free HTTP/1.1 server (loopback by default) for the platform dashboard.
 *
 * One thread per connection, "Connection: close", request bodies up to 1 MiB.
 * Intended for local tooling, not for exposure to untrusted networks.
 */
#ifndef AP_DASHBOARD_HTTP_SERVER_H_
#define AP_DASHBOARD_HTTP_SERVER_H_

#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <thread>

namespace apdash {

struct HttpRequest {
  std::string method;
  std::string path;
  std::map<std::string, std::string> query;
  std::string body;
};

struct HttpResponse {
  int status{200};
  std::string contentType{"application/json"};
  std::string body;
};

class HttpServer {
public:
  using Handler = std::function<HttpResponse(HttpRequest const &)>;
  explicit HttpServer(Handler handler) : handler_(std::move(handler)) {}
  ~HttpServer() { Stop(); }
  HttpServer(HttpServer const &) = delete;
  HttpServer &operator=(HttpServer const &) = delete;

  /// port 0 picks a free port.  Returns false on failure.
  bool Start(std::string const &bindAddress, unsigned short port);
  void Stop();
  unsigned short Port() const noexcept { return port_; }

private:
  void AcceptLoop();
  void Serve(int fd);

  Handler handler_;
  int listenFd_{-1};
  unsigned short port_{0};
  std::atomic<bool> running_{false};
  std::thread acceptThread_;
};

std::string UrlDecode(std::string const &s);

} // namespace apdash

#endif // AP_DASHBOARD_HTTP_SERVER_H_
