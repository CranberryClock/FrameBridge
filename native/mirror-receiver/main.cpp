#include "mirror_session.h"
#include "render_gate.h"
#ifdef FRAMEBRIDGE_HAS_DAWN
#include "renderer.h"
#endif
#include <ixwebsocket/IXWebSocketServer.h>
#include <ixwebsocket/IXGetFreePort.h>
#include <ixwebsocket/IXNetSystem.h>
#include <nlohmann/json.hpp>
#include <windows.h>
#include <bcrypt.h>
#include <psapi.h>
#include <conio.h>
#include <atomic>
#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
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
#pragma comment(lib, "psapi.lib")

namespace {
using Clock=std::chrono::steady_clock;
using ClientPtr=std::shared_ptr<ix::WebSocket>;
using namespace framebridge::native_mirror;
std::atomic<bool> interrupted{false};
BOOL WINAPI Control(DWORD) { interrupted=true; return TRUE; }
std::string Environment(const char* name,const std::string& fallback) {
  char* v=nullptr; size_t n=0;
  if(_dupenv_s(&v,&n,name)!=0 || !v) return fallback;
  const std::string result=*v?v:fallback; free(v); return result;
}
std::string MakeToken() {
  std::array<unsigned char,24> bytes{};
  if(BCryptGenRandom(nullptr,bytes.data(),static_cast<ULONG>(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)!=0)
    throw std::runtime_error("token generation failed");
  std::ostringstream s; for(auto b:bytes) s<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(b);
  return s.str();
}
struct Options {
  bool protocolOnly=false, failInit=false, failRender=false, trace=false;
  std::string capture;
};
class Receiver {
  struct Client {
    ClientPtr socket;
    Clock::time_point deadline=Clock::now()+std::chrono::seconds(5);
    std::string origin;
    std::unique_ptr<MirrorSession> session;
    std::atomic<bool> active{true};
  };
 public:
  explicit Receiver(Options options) : options_(std::move(options)),
      origin_(Environment("FRAMEBRIDGE_ALLOWED_ORIGIN","http://127.0.0.1:5173")),
      token_(MakeToken()), server_(ix::getFreePort(),"127.0.0.1",4,4,3) {
    if(options_.failInit) throw std::runtime_error("controlled renderer initialization failure");
    if(!options_.protocolOnly) {
#ifdef FRAMEBRIDGE_HAS_DAWN
      renderer_=std::make_unique<framebridge::render::Renderer>();
      std::cout<<renderer_->Telemetry()<<"\n";
#else
      throw std::runtime_error("Dawn build required for default native mode");
#endif
    }
    if(!options_.capture.empty()) std::filesystem::create_directories(options_.capture);
    const auto delay=Environment("FRAMEBRIDGE_NATIVE_PROCESSING_DELAY_MS","0");
    size_t used=0; delay_=std::stoul(delay,&used);
    if(used!=delay.size() || delay_>10000) throw std::runtime_error("invalid processing delay");
    server_.disablePerMessageDeflate();
    server_.setOnConnectionCallback([this](std::weak_ptr<ix::WebSocket> weak,std::shared_ptr<ix::ConnectionState>) {
      if(auto socket=weak.lock()) {
        auto client=std::make_shared<Client>(); client->socket=socket;
        {
          std::lock_guard lock(mutex_);
          clients_[socket.get()]=client;
        }
        // Weak capture avoids a socket -> callback -> socket ownership cycle.
        socket->setOnMessageCallback([this,weak](const ix::WebSocketMessagePtr& message) {
          if(auto s=weak.lock()) Handle(s,message);
        });
      }
    });
  }
  ~Receiver() { Stop(); }
  void Start() {
    const auto result=server_.listen(); if(!result.first) throw std::runtime_error("loopback listen failed");
    server_.start(); started_=true;
    // Renderer/device/LUID/window all exist before capabilities or READY are possible.
    std::cout<<"FRAMEBRIDGE_NATIVE_MIRROR_READY host=127.0.0.1 port="<<server_.getPort()
             <<" token="<<token_<<" backend="<<Backend()<<" mode=mirror-spike\n"<<std::flush;
  }
  void Stop() {
    if(!started_) return;
    std::vector<ClientPtr> sockets;
    {
      std::lock_guard lock(mutex_);
      for(auto& [_,c]:clients_) { c->active=false; sockets.push_back(c->socket); }
      clients_.clear(); controller_.reset();
    }
    for(auto& s:sockets) s->close(1001,"receiver shutdown");
    server_.stop(); // IX joins its accept and client threads; no receiver thread is detached.
    started_=false;
  }
  bool Tick() {
    if(Clock::now()>=nextMemory_) {
      PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb=sizeof(memory);
      if(!GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))) throw std::runtime_error("memory sample failed");
      std::cout<<nlohmann::json({{"event","memory"},{"elapsed_seconds",std::chrono::duration<double>(Clock::now()-startedAt_).count()},
        {"working_set_bytes",memory.WorkingSetSize},{"private_bytes",memory.PrivateUsage},{"submitted",submitted_},{"acknowledged",acknowledged_}}).dump()<<"\n"<<std::flush;
      nextMemory_=Clock::now()+std::chrono::seconds(10);
    }
#ifdef FRAMEBRIDGE_HAS_DAWN
    if(renderer_ && !renderer_->Pump()) return false;
#endif
    std::vector<ClientPtr> expired;
    std::shared_ptr<Client> client;
    std::optional<CompleteFrame> frame;
    std::uint64_t sessionGeneration=0;
    std::vector<std::uint8_t> acknowledgement;
    {
      std::lock_guard lock(mutex_);
      for(auto& [_,c]:clients_) if(!c->session && c->active && Clock::now()>c->deadline) {
        c->active=false; expired.push_back(c->socket);
      }
      client=controller_.lock();
      if(client && client->active && client->session && Clock::now()>=nextProcess_) {
        sessionGeneration=client->session->generation();
        frame=client->session->ProcessOne();
        if(frame) acknowledgement=client->session->EncodeFrameAccepted(*frame);
      }
    }
    for(auto& s:expired) s->close(1008,"hello timeout");
    if(!frame) return true;
    try {
#ifdef FRAMEBRIDGE_HAS_DAWN
      if(renderer_) renderer_->SetSessionGeneration(sessionGeneration);
#endif
      SubmitThenAcknowledge(*frame,[&](const SceneState& state,uint64_t drops) {
        if(options_.failRender) throw std::runtime_error("controlled render failure");
#ifdef FRAMEBRIDGE_HAS_DAWN
        if(renderer_) {
          std::string capture;
          if(!options_.capture.empty() && (state.frame==60 || state.frame==120 || state.frame==180))
            capture=options_.capture+"/native-"+std::to_string(state.frame)+".png";
          renderer_->Submit(state,drops,capture);
        }
#else
        (void)state; (void)drops;
#endif
        ++submitted_;
        if(options_.trace) {
          std::cout<<nlohmann::json({{"event","submitted"},{"frame",state.frame},{"simulationTime",state.simulationTime},
            {"rotationX",state.rotationX},{"rotationY",state.rotationY},{"cameraZ",state.cameraZ},
            {"width",state.width},{"height",state.height},{"resizeGeneration",state.resizeGeneration},
            {"dropped",drops},{"sessionGeneration",client->session->generation()}}).dump()<<"\n"<<std::flush;
        }
      },[&] {
        if(client->active) {
          const auto result=client->socket->sendBinary(std::string(acknowledgement.begin(),acknowledgement.end()));
          if(!result.success) throw std::runtime_error("ack send failed");
          ++acknowledged_;
        }
      });
      nextProcess_=Clock::now()+std::chrono::milliseconds(delay_);
    } catch(...) {
      client->active=false; client->socket->close(1011,"render submission failed");
      throw;
    }
    return true;
  }
  void Report() {
#ifdef FRAMEBRIDGE_HAS_DAWN
    if(renderer_) { renderer_->Validate(); std::cout<<renderer_->Telemetry()<<"\n"; }
#endif
    std::cout<<nlohmann::json({{"event","shutdown"},{"backend",Backend()},{"submitted",submitted_},
      {"acknowledged",acknowledged_},{"sessions",generation_},{"max_queued_complete_frames",maxQueued_},{"clean_shutdown",true},{"owned_threads_joined",true}}).dump()<<"\n";
  }
 private:
  const char* Backend() const { return options_.protocolOnly?"test-harness":"native-dawn"; }
  void Handle(const ClientPtr& socket,const ix::WebSocketMessagePtr& message) {
    std::string response, reason;
    uint16_t closeCode=0;
    {
      std::lock_guard lock(mutex_);
      const auto it=clients_.find(socket.get()); if(it==clients_.end()) return;
      auto c=it->second;
      if(message->type==ix::WebSocketMessageType::Close || message->type==ix::WebSocketMessageType::Error) {
        c->active=false;
        if(controller_.lock()==c) controller_.reset();
        clients_.erase(it); return;
      }
      if(!c->active) return;
      if(message->type==ix::WebSocketMessageType::Open) {
        const auto h=message->openInfo.headers.find("Origin");
        c->origin=h==message->openInfo.headers.end()?"":h->second; return;
      }
      if(message->type!=ix::WebSocketMessageType::Message) return;
      try {
        if(!c->session) {
          if(message->binary || message->str.size()>8192 || Clock::now()>c->deadline ||
             c->origin!=origin_ || !controller_.expired()) throw std::runtime_error("hello");
          const auto h=nlohmann::json::parse(message->str);
          if(!h.is_object() || h.value("kind","")!="hello" || h.value("version",-1)!=0 ||
             h.value("byteOrder","")!="little" || h.value("origin","")!=origin_ ||
             h.value("token","")!=token_ || !h.contains("buildId") || !h["buildId"].is_string() ||
             !h.contains("three") || !h["three"].is_object() ||
             h["three"].value("version","")!="0.185.0" || h["three"].value("commit","")!="2431a09" ||
             h.value("requestedCapabilities",nlohmann::json::array())!=nlohmann::json::array({"explicit-mirror"}))
            throw std::runtime_error("hello shape");
          c->session=std::make_unique<MirrorSession>(++generation_); controller_=c;
          response=nlohmann::json({{"kind","capabilities"},{"version",0},{"sessionId","native-mirror"},
            {"sessionGeneration",std::to_string(generation_)},{"buildId","framebridge-tcw005r"},
            {"backend",Backend()},{"features",{"explicit-mirror"}},{"byteOrder","little"}}).dump();
        } else {
          if(!message->binary || message->str.size()>framebridge::protocol::kMaxPayload+framebridge::protocol::kHeaderBytes)
            throw std::runtime_error("binary size/type");
          const auto bytes=std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(message->str.data()),message->str.size());
          const auto decoded=framebridge::protocol::Decode(bytes); c->session->Accept(decoded);
          maxQueued_=std::max(maxQueued_,c->session->queuedFrames());
          if(decoded.type==framebridge::protocol::MessageType::EndSession) {
            c->active=false; if(controller_.lock()==c) controller_.reset(); closeCode=1000; reason="session ended";
          }
        }
      } catch(...) {
        closeCode=c->session?1002:1008; reason="protocol rejected"; c->active=false;
        if(controller_.lock()==c) controller_.reset();
      }
    }
    // No socket I/O while holding mutex_: IX callbacks may re-enter.
    if(closeCode) socket->close(closeCode,reason);
    else if(!response.empty()) socket->sendText(response);
  }
  Options options_;
  std::string origin_,token_;
  unsigned long delay_=0;
  ix::WebSocketServer server_;
  std::mutex mutex_;
  std::map<ix::WebSocket*,std::shared_ptr<Client>> clients_;
  std::weak_ptr<Client> controller_;
  uint64_t generation_=0,submitted_=0,acknowledged_=0;
  bool started_=false;
  Clock::time_point nextProcess_{};
  Clock::time_point startedAt_=Clock::now(),nextMemory_{};
  size_t maxQueued_=0;
#ifdef FRAMEBRIDGE_HAS_DAWN
  std::unique_ptr<framebridge::render::Renderer> renderer_;
#endif
};
bool StopInput() {
  HANDLE input=GetStdHandle(STD_INPUT_HANDLE);
  if(GetFileType(input)==FILE_TYPE_PIPE) {
    DWORD available=0;
    if(!PeekNamedPipe(input,nullptr,0,nullptr,&available,nullptr)) return true;
    if(available) { char c; DWORD read=0; ReadFile(input,&c,1,&read,nullptr); return true; }
  } else if(_kbhit()) { return _getch()==13; }
  return false;
}
}
int main(int argc,char** argv) {
  try {
    Options options;
    for(int i=1;i<argc;++i) {
      const std::string a=argv[i];
      if(a=="--test-protocol-only") options.protocolOnly=true;
      else if(a=="--test-init-failure") options.failInit=true;
      else if(a=="--test-render-failure") options.failRender=true;
      else if(a=="--trace") options.trace=true;
      else if(a=="--capture-dir" && i+1<argc) options.capture=argv[++i];
      else throw std::runtime_error("unknown argument");
    }
    if(!ix::initNetSystem()) throw std::runtime_error("network init");
    SetConsoleCtrlHandler(Control,TRUE);
    {
      Receiver receiver(options); receiver.Start();
      while(!interrupted && !StopInput() && receiver.Tick()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
      receiver.Stop(); receiver.Report();
    }
    ix::uninitNetSystem(); return 0;
  } catch(const std::exception& e) {
    std::cerr<<"FAIL native receiver: "<<e.what()<<"\n"; ix::uninitNetSystem(); return 4;
  } catch(...) { std::cerr<<"FAIL native receiver: unknown exception\n"; ix::uninitNetSystem(); return 4; }
}
