#include "mirror_session.h"

#include <cstring>
#include <cmath>
#include <stdexcept>

namespace framebridge::native_mirror {
using framebridge::protocol::MessageType;

void MirrorSession::Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

SceneState MirrorSession::DecodeScene(std::span<const std::uint8_t> p) {
  Require(p.size() == 48, "invalid scene payload size");
  SceneState s;
  s.frame = framebridge::protocol::Get64(p, 0);
  std::memcpy(&s.simulationTime, p.data() + 8, sizeof(s.simulationTime));
  std::memcpy(&s.rotationX, p.data() + 16, sizeof(s.rotationX));
  std::memcpy(&s.rotationY, p.data() + 20, sizeof(s.rotationY));
  std::memcpy(&s.cameraZ, p.data() + 24, sizeof(s.cameraZ));
  s.width = framebridge::protocol::Get32(p, 28);
  s.height = framebridge::protocol::Get32(p, 32);
  s.resizeGeneration = framebridge::protocol::Get64(p, 36);
  Require(framebridge::protocol::Get32(p, 44) == 0, "nonzero scene reserved bytes");
  Require(s.frame > 0 && s.resizeGeneration > 0 && s.width > 0 && s.height > 0 &&
              s.width <= 8192 && s.height <= 8192,
          "invalid scene state");
  Require(s.simulationTime >= 0.0 && std::isfinite(s.simulationTime) &&
              std::isfinite(s.rotationX) && std::isfinite(s.rotationY) && std::isfinite(s.cameraZ),
          "non-finite scene state");
  return s;
}

MirrorSession::MirrorSession(std::uint64_t sessionGeneration) : generation_(sessionGeneration) {
  Require(generation_ > 0, "invalid session generation");
}

void MirrorSession::Accept(framebridge::protocol::Message message) {
  Require(phase_ != Phase::Closed, "session closed");
  Require(message.sequence > lastSequence_, "non-monotonic sequence");
  Require(message.type != MessageType::FrameAccepted && message.type != MessageType::Error,
          "server-only message");
  Require(message.type != MessageType::CreateBuffer && message.type != MessageType::DestroyResource &&
              message.type != MessageType::Draw,
          "reserved message rejected by native mirror");
  if (message.type == MessageType::BeginSession) {
    Require(phase_ == Phase::AwaitingBegin, "duplicate BeginSession");
    phase_ = Phase::Active;
  } else {
    Require(phase_ == Phase::Active, "message before BeginSession");
    if (message.type == MessageType::Resize) {
      Require(!openFrame_, "Resize inside frame");
      Require(message.payload.size() == 16, "invalid Resize payload");
      const auto width = framebridge::protocol::Get32(message.payload, 0);
      const auto height = framebridge::protocol::Get32(message.payload, 4);
      const auto generation = framebridge::protocol::Get64(message.payload, 8);
      Require(width > 0 && height > 0 && width <= 8192 && height <= 8192 && generation > lastResizeGeneration_,
              "invalid Resize state");
      width_ = width; height_ = height; lastResizeGeneration_ = generation;
    } else if (message.type == MessageType::BeginFrame) {
      Require(!openFrame_, "nested BeginFrame");
      auto state = DecodeScene(message.payload);
      Require(state.frame > lastFrame_ && state.resizeGeneration == lastResizeGeneration_ &&
                  state.width == width_ && state.height == height_,
              "invalid frame continuity");
      openFrame_ = state;
    } else if (message.type == MessageType::EndFrame) {
      Require(openFrame_.has_value(), "EndFrame without BeginFrame");
      if (queue_.size() == 2) { queue_.pop_front(); ++droppedFrames_; }
      queue_.push_back({*openFrame_, message.sequence});
      lastFrame_ = openFrame_->frame; openFrame_.reset();
    } else if (message.type == MessageType::SetRtxMode) {
      Require(message.payload.size() == 1 && message.payload[0] == 0, "invalid RTX mode");
    } else {
      Require(message.type == MessageType::Ping || message.type == MessageType::EndSession,
              "unsupported native mirror message");
      if (message.type == MessageType::EndSession) {
        Require(!openFrame_, "EndSession with open frame");
        phase_ = Phase::Closed; queue_.clear();
      }
    }
  }
  lastSequence_ = message.sequence;
}

std::optional<CompleteFrame> MirrorSession::ProcessOne() {
  if (queue_.empty()) return std::nullopt;
  auto value = queue_.front(); queue_.pop_front(); return value;
}

std::vector<std::uint8_t> MirrorSession::EncodeFrameAccepted(const CompleteFrame& frame) const {
  std::vector<std::uint8_t> payload(40);
  framebridge::protocol::Put64(payload, 0, generation_);
  framebridge::protocol::Put64(payload, 8, frame.state.frame);
  framebridge::protocol::Put64(payload, 16, frame.endSequence);
  framebridge::protocol::Put32(payload, 24, static_cast<std::uint32_t>(droppedFrames_));
  framebridge::protocol::Put32(payload, 28, 0);
  framebridge::protocol::Put64(payload, 32, frame.state.resizeGeneration);
  return framebridge::protocol::Encode({MessageType::FrameAccepted, 0, frame.endSequence, 0, payload});
}

}  // namespace framebridge::native_mirror
