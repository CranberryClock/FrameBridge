#include "codec.hpp"
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

int main() {
  std::ifstream file("packages/protocol/fixtures/valid-begin-frame.hex"); if (!file) return 2;
  std::string hex; file >> hex; if (hex.size() % 2 != 0) return 3;
  std::vector<uint8_t> fixture; fixture.reserve(hex.size() / 2); for (size_t i = 0; i < hex.size(); i += 2) fixture.push_back(static_cast<uint8_t>(std::stoul(hex.substr(i, 2), nullptr, 16)));
  const auto header = framebridge::protocol::DecodeHeader(fixture); if (header.type != 32 || header.sequence != 1 || header.payloadBytes != 48) return 4;
  const auto encoded = framebridge::protocol::Encode(header, std::span<const uint8_t>(fixture.data() + framebridge::protocol::kHeaderBytes, header.payloadBytes)); if (encoded != fixture) return 5;
  auto bad = fixture; bad[0] = 0; try { framebridge::protocol::DecodeHeader(bad); return 6; } catch (const std::runtime_error&) {}
  std::cout << "TCW-PROTO-001_CPP_PASS shared_fixture=valid-begin-frame.hex\nTCW-PROTO-002_CPP_PASS malformed_magic=reject\n"; return 0;
}
