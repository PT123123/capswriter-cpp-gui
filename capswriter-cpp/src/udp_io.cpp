#include "udp_io.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstring>

#include "log.h"

namespace capswriter {

namespace {

struct WinsockGuard {
  bool ok = false;
  WinsockGuard() {
    WSADATA data{};
    ok = WSAStartup(MAKEWORD(2, 2), &data) == 0;
  }
  ~WinsockGuard() {
    if (ok) WSACleanup();
  }
};

WinsockGuard& Winsock() {
  static WinsockGuard guard;
  return guard;
}

}  // namespace

UdpBroadcaster::UdpBroadcaster(const std::vector<UdpTarget>& targets)
    : targets_(targets) {
  ready_ = Winsock().ok && !targets_.empty();
  if (!Winsock().ok) LOG_WARN("WSAStartup failed; UDP broadcast disabled");
}

UdpBroadcaster::~UdpBroadcaster() = default;

void UdpBroadcaster::Broadcast(const std::string& text) {
  if (!ready_ || text.empty()) return;

  for (const UdpTarget& target : targets_) {
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
      LOG_WARN("UDP socket creation failed (error %d)", WSAGetLastError());
      continue;
    }

    // Broadcast addresses need SO_BROADCAST.
    BOOL broadcast = TRUE;
    setsockopt(sock, SOL_SOCKET, SO_BROADCAST,
               reinterpret_cast<const char*>(&broadcast), sizeof(broadcast));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(target.port));
    inet_pton(AF_INET, target.addr.c_str(), &addr.sin_addr);

    const int sent = sendto(sock, text.c_str(),
                            static_cast<int>(text.size()), 0,
                            reinterpret_cast<const sockaddr*>(&addr),
                            sizeof(addr));
    if (sent < 0) {
      LOG_WARN("UDP send to %s:%d failed (error %d)", target.addr.c_str(),
               target.port, WSAGetLastError());
    } else {
      LOG_INFO("UDP broadcast to %s:%d (%d bytes)", target.addr.c_str(),
               target.port, sent);
    }
    closesocket(sock);
  }
}

UdpController::UdpController(const std::string& addr, int port,
                             CommandHandler handler)
    : addr_(addr), port_(port), handler_(std::move(handler)) {}

UdpController::~UdpController() { Stop(); }

bool UdpController::Start() {
  if (running_.load()) return true;
  if (!Winsock().ok) {
    LOG_WARN("UDP control disabled: winsock unavailable");
    return false;
  }
  running_.store(true);
  thread_ = std::thread(&UdpController::ListenLoop, this);
  LOG_INFO("UDP control listening on %s:%d", addr_.c_str(), port_);
  return true;
}

void UdpController::Stop() {
  if (!running_.load() && !thread_.joinable()) return;
  running_.store(false);
  // Closing the socket wakes up the blocking recvfrom.
  if (sock_) {
    closesocket(reinterpret_cast<SOCKET>(sock_));
    sock_ = nullptr;
  }
  if (thread_.joinable()) thread_.join();
}

void UdpController::ListenLoop() {
  SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (sock == INVALID_SOCKET) {
    LOG_ERROR("UDP control socket creation failed (error %d)",
              WSAGetLastError());
    running_.store(false);
    return;
  }
  sock_ = reinterpret_cast<void*>(sock);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<u_short>(port_));
  inet_pton(AF_INET, addr_.c_str(), &addr.sin_addr);

  if (bind(sock, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) ==
      SOCKET_ERROR) {
    LOG_ERROR("UDP control bind to %s:%d failed (error %d)", addr_.c_str(),
              port_, WSAGetLastError());
    closesocket(sock);
    sock_ = nullptr;
    running_.store(false);
    return;
  }

  char buf[1024];
  while (running_.load()) {
    sockaddr_in from{};
    int from_len = sizeof(from);
    const int n = recvfrom(sock, buf, sizeof(buf) - 1, 0,
                           reinterpret_cast<sockaddr*>(&from), &from_len);
    if (n <= 0) break;
    buf[n] = '\0';

    // Uppercase and trim, mirroring the Python command protocol.
    std::string command(buf);
    std::string cleaned;
    for (char c : command) {
      if (c != '\r' && c != '\n' && c != ' ' && c != '\t') {
        cleaned += static_cast<char>(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
      }
    }

    if (cleaned == "START") {
      char ip[INET_ADDRSTRLEN] = {};
      InetNtopA(AF_INET, &from.sin_addr, ip, sizeof(ip));
      LOG_INFO("UDP control: START (from %s:%u)", ip, ntohs(from.sin_port));
      if (handler_) handler_(true);
    } else if (cleaned == "STOP") {
      char ip[INET_ADDRSTRLEN] = {};
      InetNtopA(AF_INET, &from.sin_addr, ip, sizeof(ip));
      LOG_INFO("UDP control: STOP (from %s:%u)", ip, ntohs(from.sin_port));
      if (handler_) handler_(false);
    } else {
      LOG_WARN("UDP control: unknown command '%s'", cleaned.c_str());
    }
  }

  if (sock_ == reinterpret_cast<void*>(sock)) {
    closesocket(sock);
    sock_ = nullptr;
  }
  running_.store(false);
}

}  // namespace capswriter
