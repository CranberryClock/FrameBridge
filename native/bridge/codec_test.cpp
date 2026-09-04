#include "codec.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
using namespace framebridge::protocol;
std::vector<uint8_t> Hex(const std::string& hex) {
  if (hex == "-") return {};
  if(hex.size()%2) throw std::runtime_error("odd hex");
  std::vector<uint8_t> out;
  for(size_t i=0;i<hex.size();i+=2) {
    if(hex.substr(i,2).find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos) throw std::runtime_error("invalid hex");
    out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i,2),nullptr,16)));
  }
  return out;
}
std::vector<uint8_t> Read(const std::filesystem::path& path) {
  std::ifstream file(path); std::string hex;
  if(!(file>>hex)) throw std::runtime_error("missing fixture "+path.string());
  return Hex(hex);
}
int main(int argc,char** argv) {
  try {
    const std::filesystem::path root = argc>1 ? argv[1] : "packages/protocol/fixtures";
    std::ifstream specs(root/"canonical.tsv"); if(!specs) throw std::runtime_error("missing canonical specification");
    std::string name,payloadHex; uint16_t type=0; uint32_t flags=0; uint64_t seq=0,id=0; int valid=0,invalid=0,encoderInvalid=0;
    while(specs>>name>>type>>flags>>seq>>id>>payloadHex) {
      const auto payload=Hex(payloadHex), fixture=Read(root/name);
      // Encoder inputs come from the structured specification, never a decoded header/payload.
      if(Encode({static_cast<MessageType>(type),flags,seq,id,payload})!=fixture) throw std::runtime_error("encode parity "+name);
      const auto decoded=Decode(fixture);
      if(uint16_t(decoded.type)!=type||decoded.flags!=flags||decoded.sequence!=seq||decoded.objectId!=id||!std::equal(decoded.payload.begin(),decoded.payload.end(),payload.begin(),payload.end())) throw std::runtime_error("decode parity "+name);
      valid++;
    }
    if(valid!=13) throw std::runtime_error("supported fixture coverage");
    for(const auto& entry:std::filesystem::directory_iterator(root)) {
      if(entry.path().filename().string().starts_with("malformed-") && entry.path().extension()==".hex") {
        const auto bytes=Read(entry.path()); bool rejected=false;
        try { (void)Decode(bytes); } catch(const std::runtime_error&) { rejected=true; }
        if(!rejected) throw std::runtime_error("accepted malformed "+entry.path().string()); invalid++;
      }
    }
    auto reject=[&](Message m){bool rejected=false;try{(void)Encode(m);}catch(const std::runtime_error&){rejected=true;}if(!rejected)throw std::runtime_error("encoder accepted invalid");encoderInvalid++;};
    const std::vector<uint8_t> empty,shortBuffer(7),buffer(8),large(kMaxPayload+1);
    reject({static_cast<MessageType>(99),0,1,0,empty});
    reject({MessageType::Ping,1,1,0,empty}); reject({MessageType::Ping,0,0,0,empty});
    reject({MessageType::CreateBuffer,0,1,0,buffer}); reject({MessageType::CreateBuffer,0,1,7,shortBuffer});
    reject({MessageType::Ping,0,1,0,large});
    // Both JSON fixtures are read from the same directory; runtime JSON validation is TypeScript-only.
    for(const char* file:{"hello.json","capabilities.json"}) {std::ifstream json(root/file);std::string text((std::istreambuf_iterator<char>(json)),{});if(text.empty())throw std::runtime_error("missing JSON fixture");}
    std::cout<<"CPP_CODEC_OK valid="<<valid<<" malformed="<<invalid<<" encoder_rejections="<<encoderInvalid<<" json_files=2\n";
    return 0;
  } catch(const std::exception& e) {std::cerr<<"CPP_CODEC_FAIL "<<e.what()<<"\n";return 1;}
}
