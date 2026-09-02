#pragma once
#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <vector>

namespace framebridge::protocol {
constexpr uint32_t kMagic = 0x30574246; constexpr uint16_t kVersion = 0; constexpr size_t kHeaderBytes = 36;
#pragma pack(push, 1)
struct Header { uint32_t magic; uint16_t version; uint16_t type; uint32_t flags; uint32_t payloadBytes; uint64_t sequence; uint64_t objectId; uint32_t checksum; };
#pragma pack(pop)
static_assert(sizeof(Header) == kHeaderBytes);
inline uint32_t Checksum(std::span<const uint8_t> bytes) { uint32_t h = 0x811c9dc5; for (uint8_t b : bytes) { h ^= b; h *= 0x01000193; } return h; }
inline std::vector<uint8_t> Encode(const Header& header, std::span<const uint8_t> payload) { if (header.magic != kMagic || header.version != kVersion || header.payloadBytes != payload.size() || header.payloadBytes > 1024 * 1024) throw std::runtime_error("invalid frame"); std::vector<uint8_t> bytes(kHeaderBytes + payload.size()); std::memcpy(bytes.data(), &header, kHeaderBytes); std::memcpy(bytes.data() + kHeaderBytes, payload.data(), payload.size()); return bytes; }
inline Header DecodeHeader(std::span<const uint8_t> bytes) { if (bytes.size() < kHeaderBytes) throw std::runtime_error("truncated header"); Header h{}; std::memcpy(&h, bytes.data(), sizeof(h)); if (h.magic != kMagic || h.version != kVersion || h.payloadBytes > 1024 * 1024 || bytes.size() != kHeaderBytes + h.payloadBytes || Checksum(bytes.subspan(kHeaderBytes)) != h.checksum) throw std::runtime_error("invalid frame"); return h; }
}
