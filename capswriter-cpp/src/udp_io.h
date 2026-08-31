#pragma once

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "config.h"

namespace capswriter {

// Sends each recognized text to the configured UDP targets
// (mirrors broadcast_output_udp).
class UdpBroadcaster {
 public:
  explicit UdpBroadcaster(const std::vector<UdpTarget>& targets);
  ~UdpBroadcaster();

  void Broadcast(const std::string& text);

 private:
  std::vector<UdpTarget> targets_;
  bool ready_ = false;
};

// Listens on a UDP port for START / STOP commands that control the
// dictation recording (mirrors UDPController).
class UdpController {
 public:
  // Called with true for START and false for STOP.
  using CommandHandler = std::function<void(bool start)>;

  UdpController(const std::string& addr, int port, CommandHandler handler);
  ~UdpController();

  bool Start();
  void Stop();

 private:
  void ListenLoop();

  std::string addr_;
  int port_;
  CommandHandler handler_;
  std::atomic<bool> running_{false};
  std::thread thread_;
  // Socket handle (SOCKET) stored as void* to keep winsock out of the header.
  void* sock_ = nullptr;
};

}  // namespace capswriter
