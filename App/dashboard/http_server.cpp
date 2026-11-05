#include "http_server.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstring>

namespace apdash {
namespace {
constexpr std::size_t kMaxHeader = 16 * 1024;
constexpr std::size_t kMaxBody = 1024 * 1024;

char const *Reason(int s) {
  switch (s) {
  case 200: return "OK";
  case 204: return "No Content";
  case 400: return "Bad Request";
  case 403: return "Forbidden";
  case 404: return "Not Found";
  case 405: return "Method Not Allowed";
  case 413: return "Payload Too Large";
  default: return "Error";
  }
}

bool SendAll(int fd, std::string const &data) {
  std::size_t off = 0;
  while (off < data.size()) {
    ssize_t n = ::send(fd, data.data() + off, data.size() - off, MSG_NOSIGNAL);
    if (n <= 0) return false;
    off += static_cast<std::size_t>(n);
  }
  return true;
}

void SendResponse(int fd, HttpResponse const &r) {
  std::string out = "HTTP/1.1 " + std::to_string(r.status) + " " + Reason(r.status) + "\r\n";
  out += "Content-Type: " + r.contentType + "\r\n";
  out += "Content-Length: " + std::to_string(r.body.size()) + "\r\n";
  out += "Cache-Control: no-store\r\nConnection: close\r\n\r\n";
  out += r.body;
  SendAll(fd, out);
}
} // namespace

std::string UrlDecode(std::string const &s) {
  std::string out;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size() + 0 && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
        std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
      out += static_cast<char>(std::stoi(s.substr(i + 1, 2), nullptr, 16));
      i += 2;
    } else if (s[i] == '+') {
      out += ' ';
    } else {
      out += s[i];
    }
  }
  return out;
}

bool HttpServer::Start(std::string const &bindAddress, unsigned short port) {
  listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
  if (listenFd_ < 0) return false;
  int one = 1;
  ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (::inet_pton(AF_INET, bindAddress.c_str(), &addr.sin_addr) != 1 ||
      ::bind(listenFd_, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0 ||
      ::listen(listenFd_, 32) != 0) {
    ::close(listenFd_);
    listenFd_ = -1;
    return false;
  }
  socklen_t len = sizeof addr;
  ::getsockname(listenFd_, reinterpret_cast<sockaddr *>(&addr), &len);
  port_ = ntohs(addr.sin_port);
  running_ = true;
  acceptThread_ = std::thread([this] { AcceptLoop(); });
  return true;
}

void HttpServer::Stop() {
  if (!running_.exchange(false)) return;
  ::shutdown(listenFd_, SHUT_RDWR);
  ::close(listenFd_);
  if (acceptThread_.joinable()) acceptThread_.join();
}

void HttpServer::AcceptLoop() {
  while (running_) {
    int fd = ::accept(listenFd_, nullptr, nullptr);
    if (fd < 0) {
      if (!running_) break;
      continue;
    }
    timeval tv{5, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    std::thread([this, fd] {
      Serve(fd);
      ::close(fd);
    }).detach();
  }
}

void HttpServer::Serve(int fd) {
  std::string buf;
  char tmp[4096];
  std::size_t headerEnd = std::string::npos;
  while (headerEnd == std::string::npos) {
    ssize_t n = ::recv(fd, tmp, sizeof tmp, 0);
    if (n <= 0) return;
    buf.append(tmp, static_cast<std::size_t>(n));
    headerEnd = buf.find("\r\n\r\n");
    if (headerEnd == std::string::npos && buf.size() > kMaxHeader) {
      SendResponse(fd, {400, "text/plain", "header too large"});
      return;
    }
  }
  HttpRequest req;
  std::string head = buf.substr(0, headerEnd);
  auto lineEnd = head.find("\r\n");
  std::string first = head.substr(0, lineEnd);
  auto sp1 = first.find(' ');
  auto sp2 = first.rfind(' ');
  if (sp1 == std::string::npos || sp2 == sp1) {
    SendResponse(fd, {400, "text/plain", "bad request line"});
    return;
  }
  req.method = first.substr(0, sp1);
  std::string target = first.substr(sp1 + 1, sp2 - sp1 - 1);
  auto q = target.find('?');
  req.path = UrlDecode(target.substr(0, q));
  if (q != std::string::npos) {
    std::string qs = target.substr(q + 1);
    std::size_t pos = 0;
    while (pos <= qs.size()) {
      auto amp = qs.find('&', pos);
      std::string kv = qs.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
      auto eq = kv.find('=');
      if (!kv.empty())
        req.query[UrlDecode(kv.substr(0, eq))] = eq == std::string::npos ? "" : UrlDecode(kv.substr(eq + 1));
      if (amp == std::string::npos) break;
      pos = amp + 1;
    }
  }
  std::size_t contentLength = 0;
  std::string lower = head;
  std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  auto cl = lower.find("content-length:");
  if (cl != std::string::npos) {
    try {
      contentLength = std::stoul(lower.substr(cl + 15));
    } catch (...) {
      SendResponse(fd, {400, "text/plain", "bad content-length"});
      return;
    }
  }
  if (contentLength > kMaxBody) {
    SendResponse(fd, {413, "text/plain", "body too large"});
    return;
  }
  req.body = buf.substr(headerEnd + 4);
  while (req.body.size() < contentLength) {
    ssize_t n = ::recv(fd, tmp, sizeof tmp, 0);
    if (n <= 0) return;
    req.body.append(tmp, static_cast<std::size_t>(n));
  }
  req.body.resize(contentLength);
  HttpResponse resp;
  try {
    resp = handler_(req);
  } catch (std::exception const &e) {
    resp = {500, "application/json", std::string("{\"error\":\"") + e.what() + "\"}"};
  } catch (...) {
    resp = {500, "application/json", "{\"error\":\"internal\"}"};
  }
  SendResponse(fd, resp);
}

} // namespace apdash
