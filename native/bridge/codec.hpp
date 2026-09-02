#pragma once
#include <cstdint>
#include <algorithm>
#include <span>
#include <stdexcept>
#include <vector>
namespace framebridge::protocol {
constexpr uint32_t kMagic = 0x30574246; constexpr uint16_t kVersion = 0; constexpr size_t kHeaderBytes = 36; constexpr uint32_t kMaxPayload = 1024 * 1024; constexpr uint32_t kFlags = 0;
enum class MessageType : uint16_t { BeginSession=1, EndSession=2, Ping=3, Error=4, SetRtxMode=5, CreateBuffer=16, DestroyResource=17, BeginFrame=32, Draw=33, EndFrame=34, Resize=35, FrameAccepted=48 };
struct Message { MessageType type; uint32_t flags; uint64_t sequence; uint64_t objectId; std::span<const uint8_t> payload; };
inline uint32_t Checksum(std::span<const uint8_t> bytes) { uint32_t h=0x811c9dc5; for (uint8_t b:bytes) { h^=b; h*=0x01000193; } return h; }
inline void Put16(std::vector<uint8_t>& b,size_t o,uint16_t v){b[o]=uint8_t(v);b[o+1]=uint8_t(v>>8);} inline void Put32(std::vector<uint8_t>& b,size_t o,uint32_t v){for(int i=0;i<4;i++)b[o+i]=uint8_t(v>>(8*i));} inline void Put64(std::vector<uint8_t>& b,size_t o,uint64_t v){for(int i=0;i<8;i++)b[o+i]=uint8_t(v>>(8*i));}
inline uint16_t Get16(std::span<const uint8_t> b,size_t o){return uint16_t(b[o])|uint16_t(b[o+1])<<8;} inline uint32_t Get32(std::span<const uint8_t> b,size_t o){uint32_t v=0;for(int i=0;i<4;i++)v|=uint32_t(b[o+i])<<(8*i);return v;} inline uint64_t Get64(std::span<const uint8_t> b,size_t o){uint64_t v=0;for(int i=0;i<8;i++)v|=uint64_t(b[o+i])<<(8*i);return v;}
inline size_t FixedPayload(uint16_t t){switch(t){case 1:case 2:case 3:case 17:case 34:return 0;case 4:return 4;case 5:return 1;case 16:return 8;case 32:return 48;case 33:return 16;case 35:return 16;case 48:return 40;default:return size_t(-1);}}
inline bool Known(uint16_t t){return FixedPayload(t)!=size_t(-1)||t==16;}
inline std::vector<uint8_t> Encode(Message m){ if(!Known(uint16_t(m.type))||m.flags!=kFlags||m.sequence<1||m.payload.size()>kMaxPayload||(m.type==MessageType::CreateBuffer||m.type==MessageType::DestroyResource)&&m.objectId==0)throw std::runtime_error("invalid message"); auto fixed=FixedPayload(uint16_t(m.type)); if(fixed!=size_t(-1)&&fixed!=m.payload.size())throw std::runtime_error("invalid fixed payload"); std::vector<uint8_t>b(kHeaderBytes+m.payload.size()); Put32(b,0,kMagic);Put16(b,4,kVersion);Put16(b,6,uint16_t(m.type));Put32(b,8,m.flags);Put32(b,12,uint32_t(m.payload.size()));Put64(b,16,m.sequence);Put64(b,24,m.objectId);Put32(b,32,Checksum(m.payload));std::copy(m.payload.begin(),m.payload.end(),b.begin()+kHeaderBytes);return b; }
inline Message Decode(std::span<const uint8_t> b){if(b.size()<kHeaderBytes)throw std::runtime_error("truncated header");auto t=Get16(b,6);auto n=Get32(b,12);if(Get32(b,0)!=kMagic||Get16(b,4)!=kVersion||!Known(t)||Get32(b,8)!=kFlags||Get64(b,16)<1||n>kMaxPayload||b.size()!=kHeaderBytes+n)throw std::runtime_error("invalid header");auto fixed=FixedPayload(t);if(fixed!=size_t(-1)&&fixed!=n)throw std::runtime_error("invalid fixed payload");auto payload=b.subspan(kHeaderBytes);if(Get32(b,32)!=Checksum(payload))throw std::runtime_error("checksum mismatch");if((t==16||t==17)&&Get64(b,24)==0)throw std::runtime_error("illegal object id");return {static_cast<MessageType>(t),Get32(b,8),Get64(b,16),Get64(b,24),payload};}
}
