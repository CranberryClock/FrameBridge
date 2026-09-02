#include "render_gate.h"
#include "scene_math.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cstring>
using namespace framebridge;
using namespace framebridge::native_mirror;
int count=0;
void Check(bool condition,const char* name) { ++count; if(!condition) throw std::runtime_error(name); }
void Near(double a,double b,double tolerance) { Check(std::isfinite(a)&&std::abs(a-b)<=tolerance,"parity tolerance"); }
int main(int argc,char**) {
  try {
    if(argc>1) Check(false,"controlled active Release check");
    std::ifstream file("tests/fixtures/tcw005/three-parity.json"); nlohmann::json oracle; file>>oracle;
    Check(oracle["three"]=="185","Three pin");
    for(const auto& row:oracle["frames"]) {
      const std::string hex=row["scene_bytes"]; std::vector<uint8_t> payload;
      for(size_t i=0;i<hex.size();i+=2) payload.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i,2),nullptr,16)));
      MirrorSession session(1); uint64_t sequence=0;
      auto send=[&](protocol::MessageType t,std::span<const uint8_t> p) { auto bytes=protocol::Encode({t,0,++sequence,0,p}); session.Accept(protocol::Decode(bytes)); };
      send(protocol::MessageType::BeginSession,{});
      std::vector<uint8_t> resize(16); protocol::Put32(resize,0,row["width"]); protocol::Put32(resize,4,row["height"]); protocol::Put64(resize,8,protocol::Get64(payload,36));
      send(protocol::MessageType::Resize,resize); send(protocol::MessageType::BeginFrame,payload); send(protocol::MessageType::EndFrame,{});
      auto frame=session.ProcessOne(); Check(frame.has_value(),"frame delivery");
      bool rendered=false,acked=false;
      SubmitThenAcknowledge(*frame,[&](const SceneState& s,uint64_t) {
        rendered=true; Check(s.frame==row["frame"] && s.width==row["width"] && s.height==row["height"],"unchanged scene");
        double t;float x,y,z;std::memcpy(&t,payload.data()+8,8);std::memcpy(&x,payload.data()+16,4);std::memcpy(&y,payload.data()+20,4);std::memcpy(&z,payload.data()+24,4);
        Check(s.simulationTime==t&&s.rotationX==x&&s.rotationY==y&&s.cameraZ==z&&s.resizeGeneration==protocol::Get64(payload,36),"unchanged decoded floats/resize");
        auto matrices=render::SceneMatrices(s);
        for(const auto& [name,m]:{std::pair{"model",matrices.model},{"view",matrices.view},{"projection",matrices.projection},{"mvp",matrices.mvp}})
          for(size_t k=0;k<16;++k) Near(m[k],row[name][k],1e-12);
        for(size_t i=0;i<8;++i) for(size_t k=0;k<3;++k) Near(matrices.corners[i][k],row["corners"][i][k],1e-12);
        for(int i=0;i<8;++i) {
          std::array<float,4> p={(i&1)?.5f:-.5f,(i&2)?.5f:-.5f,(i&4)?.5f:-.5f,1},clip{};
          for(int r=0;r<4;++r) for(int k=0;k<4;++k) clip[r]+=static_cast<float>(matrices.mvp[k*4+r])*p[k];
          for(int r=0;r<3;++r) Near(clip[r]/clip[3],row["corners"][i][r],2e-6);
        }
      },[&]{Check(rendered,"ACK before submit"); acked=true;});
      Check(acked,"missing successful ACK");
      acked=false; bool failed=false;
      try {SubmitThenAcknowledge(*frame,[](const SceneState& s,uint64_t){if(s.frame>0) throw std::runtime_error("render failed");},[&]{acked=true;});} catch(const std::exception&) {failed=true;}
      Check(failed&&!acked,"render failure acknowledged");
    }
    std::cout<<"PASS render_gate_and_parity checks="<<count<<" cases=6 frames=60,120,180 double_tolerance=1e-12 gpu_float_tolerance=2e-6\n"; return 0;
  } catch(const std::exception& e) {std::cerr<<"FAIL checks="<<count<<" "<<e.what()<<"\n";return 1;}
}
