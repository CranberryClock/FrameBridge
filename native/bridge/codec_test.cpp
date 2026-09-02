#include "codec.hpp"
#include <cassert>
#include <fstream>
#include <iostream>
#include <string>
using namespace framebridge::protocol;
static std::vector<uint8_t> ReadHex(const std::string& path) { std::ifstream in(path); if (!in) throw std::runtime_error("fixture missing: " + path); std::string hex; in >> hex; if (hex.size() % 2) throw std::runtime_error("odd fixture"); std::vector<uint8_t> out; for (size_t i=0;i<hex.size();i+=2) out.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i,2),nullptr,16))); return out; }
int main() {
  const std::string root = "packages/protocol/fixtures/"; const std::vector<std::pair<std::string,uint64_t>> valid = {{"valid-begin-session.hex",0},{"valid-end-session.hex",0},{"valid-ping.hex",0},{"valid-set-rtx-mode.hex",0},{"valid-create-buffer.hex",7},{"valid-destroy-resource.hex",7},{"valid-begin-frame.hex",0},{"valid-draw.hex",0},{"valid-end-frame.hex",0},{"valid-resize.hex",0},{"valid-frame-accepted.hex",0}};
  for (const auto& [name,id] : valid) { auto fixture=ReadHex(root+name); auto decoded=Decode(fixture); auto payload=std::vector<uint8_t>(decoded.payload.begin(),decoded.payload.end()); auto rebuilt=Encode({decoded.type,0,1,id,payload}); if (rebuilt != fixture) return 20; }
  for (const auto& name : {"malformed-wrong-magic.hex","malformed-wrong-version.hex","malformed-unknown-type.hex","malformed-unsupported-flags.hex","malformed-zero-sequence.hex","malformed-truncated-header.hex","malformed-truncated-payload.hex","malformed-oversized-payload.hex","malformed-incorrect-checksum.hex","malformed-illegal-object-zero.hex","malformed-invalid-fixed-payload.hex"}) { try { Decode(ReadHex(root+name)); return 10; } catch (const std::runtime_error&) {} }
  std::cout << "TCW-PROTO-001_CPP_PASS independent_encoder_decoder valid_fixtures=" << valid.size() << "\n";
  std::cout << "TCW-PROTO-002_CPP_PASS malformed=wrong_magic,unknown_type,zero_sequence,fixed_payload,checksum\n"; return 0;
}
