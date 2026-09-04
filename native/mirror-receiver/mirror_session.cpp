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
              std::isfinite(s.rotationX) && std::isfinite(s.rotationY) && std::isfinite(s.cameraZ) && s.cameraZ > 0,
          "non-finite scene state");
  return s;
}

MirrorSession::MirrorSession(std::uint64_t sessionGeneration) : generation_(sessionGeneration) {
  Require(generation_ > 0, "invalid session generation");
}

void MirrorSession::Accept(framebridge::protocol::Message message) {
  Require(phase_ != Phase::Closed, "session closed");
  Require(message.objectId == 0 || message.type == MessageType::TextureUpload, "scene messages require session object ID zero");
  Require(message.sequence > lastSequence_, "non-monotonic sequence");
  Require(message.type != MessageType::FrameAccepted && message.type != MessageType::NativeImage && message.type != MessageType::Error,
          "server-only message");
  Require(message.type != MessageType::CreateBuffer && message.type != MessageType::DestroyResource &&
              message.type != MessageType::Draw,
          "reserved message rejected by native mirror");
  Require(message.type != MessageType::TextureAccepted, "server-only message");
  if (message.type == MessageType::BeginSession) {
    Require(phase_ == Phase::AwaitingBegin, "duplicate BeginSession");
    phase_ = Phase::Active;
  } else {
    Require(phase_ == Phase::Active, "message before BeginSession");
    if (message.type == MessageType::TextureUpload) {
      Require(!openFrame_ && message.payload.size() >= 40, "texture upload state");
      const auto p=message.payload; TextureUpload t; t.resourceId=framebridge::protocol::Get64(p,8); t.revision=framebridge::protocol::Get64(p,16); t.width=framebridge::protocol::Get32(p,24); t.height=framebridge::protocol::Get32(p,28); t.format=framebridge::protocol::Get32(p,32); const auto bytes=framebridge::protocol::Get32(p,36);
      Require(framebridge::protocol::Get64(p,0)==generation_ && t.resourceId==message.objectId && t.resourceId==1 && t.revision>textureRevision_ && t.width==256 && t.height==256 && t.format==1 && bytes==256*256*4 && p.size()==40+bytes,"invalid texture upload");
      Require(!pendingTexture_, "texture upload queue full"); t.pixels.assign(p.begin()+40,p.end()); pendingTexture_=std::move(t);
    } else if (message.type == MessageType::Resize) {
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
      Require(message.type == MessageType::Ping || message.type == MessageType::ImageConsumed || message.type == MessageType::EndSession,
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
  auto value = queue_.front(); queue_.pop_front(); value.droppedBefore = droppedFrames_; return value;
}

std::vector<std::uint8_t> MirrorSession::EncodeFrameAccepted(const CompleteFrame& frame) const {
  std::vector<std::uint8_t> payload(40);
  framebridge::protocol::Put64(payload, 0, generation_);
  framebridge::protocol::Put64(payload, 8, frame.state.frame);
  framebridge::protocol::Put64(payload, 16, frame.endSequence);
  framebridge::protocol::Put32(payload, 24, static_cast<std::uint32_t>(frame.droppedBefore));
  framebridge::protocol::Put32(payload, 28, 0);
  framebridge::protocol::Put64(payload, 32, frame.state.resizeGeneration);
  return framebridge::protocol::Encode({MessageType::FrameAccepted, 0, frame.endSequence, 0, payload});
}
std::vector<std::uint8_t> MirrorSession::EncodeNativeImage(const CompleteFrame& frame, std::uint64_t nativeFrame, std::span<const std::uint8_t> rgba) const {
  Require(nativeFrame>0 && rgba.size()==static_cast<std::size_t>(frame.state.width)*frame.state.height*4,"invalid native image");
  std::vector<std::uint8_t> payload(48+rgba.size()); framebridge::protocol::Put64(payload,0,generation_);framebridge::protocol::Put64(payload,8,frame.state.frame);framebridge::protocol::Put64(payload,16,nativeFrame);framebridge::protocol::Put64(payload,24,frame.state.resizeGeneration);framebridge::protocol::Put32(payload,32,frame.state.width);framebridge::protocol::Put32(payload,36,frame.state.height);framebridge::protocol::Put64(payload,40,textureRevision_);std::copy(rgba.begin(),rgba.end(),payload.begin()+48);
  return framebridge::protocol::Encode({MessageType::NativeImage,0,frame.endSequence,0,payload});
}

std::optional<TextureUpload> MirrorSession::TakeTextureUpload() { if(!pendingTexture_) return std::nullopt; auto value=std::move(*pendingTexture_); pendingTexture_.reset(); textureRevision_=value.revision; return value; }
std::vector<std::uint8_t> MirrorSession::EncodeTextureAccepted(const TextureUpload& texture) const { std::vector<std::uint8_t> p(32); framebridge::protocol::Put64(p,0,generation_);framebridge::protocol::Put64(p,8,texture.resourceId);framebridge::protocol::Put64(p,16,texture.revision);framebridge::protocol::Put32(p,24,texture.width);framebridge::protocol::Put32(p,28,texture.height); return framebridge::protocol::Encode({MessageType::TextureAccepted,0,texture.revision,0,p}); }

}  // namespace framebridge::native_mirror
