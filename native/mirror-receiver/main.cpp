#include "mirror_session.h"

#include <ixwebsocket/IXWebSocketServer.h>
#include <ixwebsocket/IXGetFreePort.h>
#include <ixwebsocket/IXNetSystem.h>
#include <nlohmann/json.hpp>
#include <bcrypt.h>

#include <atomic>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace {
using Clock = std::chrono::steady_clock;
using ClientPtr = std::shared_ptr<ix::WebSocket>;

std::string Environment(const char* name, const std::string& fallback) {
  char* value = nullptr;
  size_t length = 0;
  if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) return fallback;
  const std::string result = *value ? value : fallback;
  free(value);
  return result;
}

std::string MakeToken() {
  std::array<unsigned char, 24> bytes{};
  if (BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
    throw std::runtime_error("cryptographic token generation failed");
  }
  std::ostringstream out;
  for (const auto byte : bytes) out << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
  return out.str();
}

bool TokenShape(const std::string& token) {
  if (token.size() != 48) return false;
  return token.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
}

class NativeReceiver {
 public:
  explicit NativeReceiver(std::string origin)
      : origin_(std::move(origin)), token_(MakeToken()), delayMs_(ProcessingDelay()), server_(ix::getFreePort(), "127.0.0.1") {
    server_.disablePerMessageDeflate();
    server_.setOnConnectionCallback([this](std::weak_ptr<ix::WebSocket> weak, std::shared_ptr<ix::ConnectionState>) {
      if (auto socket = weak.lock()) {
        std::lock_guard lock(mutex_);
        clients_[socket.get()] = Client{socket, Clock::now() + std::chrono::seconds(5)};
        socket->setOnMessageCallback([this, socket](const ix::WebSocketMessagePtr& message) { Handle(socket, message); });
      }
    });
  }

  bool Start() {
    const auto result = server_.listen();
    if (!result.first) { std::cerr << "FAIL native receiver listen: " << result.second << '\n'; return false; }
    server_.start();
    monitor_ = std::thread([this] { Monitor(); });
    processor_ = std::thread([this] { ProcessLoop(); });
    return true;
  }

  void Stop() {
    stopping_ = true;
    server_.stop();
    if (monitor_.joinable()) monitor_.join();
    if (processor_.joinable()) processor_.join();
  }

  int Port() { return server_.getPort(); }
  const std::string& Token() const { return token_; }

 private:
  struct Client {
    ClientPtr socket;
    Clock::time_point helloDeadline;
    std::unique_ptr<framebridge::native_mirror::MirrorSession> session;
    std::string headerOrigin;
    bool authenticated = false;
  };

  static std::string Header(const ix::WebSocketHttpHeaders& headers, const char* name) {
    const auto it = headers.find(name);
    return it == headers.end() ? std::string{} : it->second;
  }

  static std::uint32_t ProcessingDelay() {
    const std::string value = Environment("FRAMEBRIDGE_NATIVE_PROCESSING_DELAY_MS", "");
    if (value.empty()) return 0;
    char* end = nullptr;
    const unsigned long parsed = std::strtoul(value.c_str(), &end, 10);
    return end != value.c_str() && *end == '\0' && parsed <= 10000 ? static_cast<std::uint32_t>(parsed) : 0;
  }

  void Reject(const ClientPtr& socket, uint16_t code, const char* reason) {
    socket->close(code, reason);
  }

  void Handle(const ClientPtr& socket, const ix::WebSocketMessagePtr& message) {
    std::lock_guard lock(mutex_);
    const auto it = clients_.find(socket.get());
    if (it == clients_.end()) return;
    Client& client = it->second;
    if (message->type == ix::WebSocketMessageType::Open) {
      client.headerOrigin = Header(message->openInfo.headers, "Origin");
      return;
    }
    if (message->type == ix::WebSocketMessageType::Close || message->type == ix::WebSocketMessageType::Error) {
      if (client.authenticated) controlling_ = false;
      clients_.erase(it);
      return;
    }
    if (message->type != ix::WebSocketMessageType::Message) return;
    if (Clock::now() > client.helloDeadline && !client.authenticated) { Reject(socket, 1008, "hello timeout"); return; }
    try {
      if (!client.authenticated) {
        if (message->binary || controlling_) throw std::runtime_error("hello rejected");
        if (message->str.size() > 8192 || client.headerOrigin != origin_) throw std::runtime_error("authentication rejected");
        const auto hello = nlohmann::json::parse(message->str);
        if (!hello.is_object() || hello.value("kind", "") != "hello" || hello.value("version", -1) != 0 ||
            hello.value("byteOrder", "") != "little" || hello.value("token", "") != token_ ||
            hello.value("origin", "") != origin_ || !TokenShape(hello.value("token", ""))) {
          throw std::runtime_error("authentication rejected");
        }
        const auto three = hello.value("three", nlohmann::json::object());
        const auto capabilities = hello.value("requestedCapabilities", nlohmann::json::array());
        if (!three.is_object() || !three.contains("version") || !three["version"].is_string() ||
            !three.contains("commit") || !three["commit"].is_string() || !hello.contains("buildId") ||
            !hello["buildId"].is_string() || !capabilities.is_array() || capabilities.size() != 1 ||
            capabilities[0] != "explicit-mirror") throw std::runtime_error("invalid hello identity");
        if (controlling_) throw std::runtime_error("competing controller");
        controlling_ = true; client.authenticated = true; generation_++;
        client.session = std::make_unique<framebridge::native_mirror::MirrorSession>(generation_);
        nlohmann::json caps = {{"kind", "capabilities"}, {"version", 0}, {"sessionId", "native-mirror"},
                               {"sessionGeneration", std::to_string(generation_)}, {"buildId", "framebridge-tcw005"},
                               {"backend", "native-dawn"}, {"features", {"explicit-mirror"}}, {"byteOrder", "little"}};
        socket->sendText(caps.dump());
        return;
      }
      if (!message->binary) throw std::runtime_error("post-auth text");
      const std::vector<std::uint8_t> bytes(message->str.begin(), message->str.end());
      const auto decoded = framebridge::protocol::Decode(bytes);
      client.session->Accept(decoded);
      if (decoded.type == framebridge::protocol::MessageType::EndFrame) {
        // The render worker consumes complete states and emits acknowledgements.
      }
      if (decoded.type == framebridge::protocol::MessageType::EndSession) { controlling_ = false; socket->close(); }
    } catch (const std::exception&) {
      Reject(socket, client.authenticated ? 1002 : 1008, "protocol rejected");
    }
  }

  void Monitor() {
    while (!stopping_) {
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      std::lock_guard lock(mutex_);
      const auto now = Clock::now();
      for (auto& [_, client] : clients_) if (!client.authenticated && now > client.helloDeadline) client.socket->close(1008, "hello timeout");
    }
  }

  void ProcessLoop() {
    while (!stopping_) {
      bool processed = false;
      {
        std::lock_guard lock(mutex_);
        for (auto& [_, client] : clients_) {
          if (!client.authenticated || !client.session) continue;
          if (auto frame = client.session->ProcessOne()) {
            const auto acknowledgement = client.session->EncodeFrameAccepted(*frame);
            client.socket->sendBinary(std::string(acknowledgement.begin(), acknowledgement.end()));
            processed = true;
            if (delayMs_ > 0) break;
          }
        }
      }
      if (delayMs_ > 0 && processed) std::this_thread::sleep_for(std::chrono::milliseconds(delayMs_));
      else std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  std::string origin_;
  std::string token_;
  std::uint32_t delayMs_ = 0;
  ix::WebSocketServer server_;
  std::map<ix::WebSocket*, Client> clients_;
  std::mutex mutex_;
  std::thread monitor_;
  std::thread processor_;
  std::atomic<bool> stopping_{false};
  bool controlling_ = false;
  std::uint64_t generation_ = 0;
};
}  // namespace

int main() {
  try {
    if (!ix::initNetSystem()) return 4;
    NativeReceiver receiver(Environment("FRAMEBRIDGE_ALLOWED_ORIGIN", "http://127.0.0.1:5173"));
    if (!receiver.Start()) { ix::uninitNetSystem(); return 2; }
    std::cout << "FRAMEBRIDGE_NATIVE_MIRROR_READY host=127.0.0.1 port=" << receiver.Port()
              << " token=" << receiver.Token() << " backend=native-dawn mode=mirror-spike\n" << std::flush;
    std::string line;
    std::getline(std::cin, line);
    receiver.Stop();
    ix::uninitNetSystem();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL native receiver startup: " << error.what() << '\n';
    return 4;
  } catch (...) {
    std::cerr << "FAIL native receiver startup: unknown exception\n";
    return 4;
  }
}
