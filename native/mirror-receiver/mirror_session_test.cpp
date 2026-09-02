#include "mirror_session.h"

#include <cstring>
#include <iostream>
#include <stdexcept>

using namespace framebridge;
using namespace framebridge::native_mirror;

void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
std::vector<std::uint8_t> Scene(std::uint64_t frame, std::uint32_t w = 640, std::uint32_t h = 360, std::uint64_t rg = 1) {
  std::vector<std::uint8_t> p(48); protocol::Put64(p,0,frame); double t=static_cast<double>(frame)/60.0; float x=.01f*static_cast<float>(frame),y=.013f*static_cast<float>(frame),z=3.0f;
  std::memcpy(p.data()+8,&t,8); std::memcpy(p.data()+16,&x,4); std::memcpy(p.data()+20,&y,4); std::memcpy(p.data()+24,&z,4); protocol::Put32(p,28,w); protocol::Put32(p,32,h); protocol::Put64(p,36,rg); return p;
}
std::vector<std::uint8_t> Empty(){return {};}

int main() {
  try {
    MirrorSession q(2); std::uint64_t seq=0; auto begin=protocol::Encode({protocol::MessageType::BeginSession,0,++seq,0,Empty()}); q.Accept(protocol::Decode(begin));
    std::vector<std::uint8_t> resize(16); protocol::Put32(resize,0,640); protocol::Put32(resize,4,360); protocol::Put64(resize,8,1); q.Accept(protocol::Decode(protocol::Encode({protocol::MessageType::Resize,0,++seq,0,resize})));
    for (std::uint64_t frame=1; frame<=4; ++frame) { q.Accept(protocol::Decode(protocol::Encode({protocol::MessageType::BeginFrame,0,++seq,0,Scene(frame)}))); q.Accept(protocol::Decode(protocol::Encode({protocol::MessageType::EndFrame,0,++seq,0,Empty()}))); }
    Check(q.queuedFrames()==2 && q.droppedFrames()==2,"queue/drop policy"); auto first=q.ProcessOne(); Check(first && first->state.frame==3,"oldest-drop frame"); auto ack=q.EncodeFrameAccepted(*first); Check(protocol::Decode(ack).type==protocol::MessageType::FrameAccepted,"ack type");
    bool rejected=false; try { q.Accept(protocol::Decode(protocol::Encode({protocol::MessageType::Draw,0,++seq,0,std::vector<std::uint8_t>(16)}))); } catch (...) { rejected=true; } Check(rejected,"reserved Draw accepted");
    std::cout<<"PASS native_session queue=2 drops=2 ack=PASS reserved_rejection=PASS\n"; return 0;
  } catch (const std::exception& e) { std::cerr<<"FAIL "<<e.what()<<'\n'; return 1; }
}
