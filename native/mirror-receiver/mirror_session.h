#pragma once

#include "codec.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <string>

namespace framebridge::native_mirror {

struct SceneState {
  std::uint64_t frame = 0;
  double simulationTime = 0.0;
  float rotationX = 0.0f;
  float rotationY = 0.0f;
  float cameraZ = 0.0f;
  std::uint32_t width = 0;
  std::uint32_t height = 0;
  std::uint64_t resizeGeneration = 0;
};

struct CompleteFrame { SceneState state; std::uint64_t endSequence = 0; };

class MirrorSession {
 public:
  explicit MirrorSession(std::uint64_t sessionGeneration);
  void Accept(framebridge::protocol::Message message);
  std::optional<CompleteFrame> ProcessOne();
  std::vector<std::uint8_t> EncodeFrameAccepted(const CompleteFrame& frame) const;
  std::uint64_t generation() const { return generation_; }
  std::uint64_t droppedFrames() const { return droppedFrames_; }
  std::size_t queuedFrames() const { return queue_.size(); }
  std::uint64_t lastSequence() const { return lastSequence_; }

 private:
  enum class Phase { AwaitingBegin, Active, Closed };
  static SceneState DecodeScene(std::span<const std::uint8_t> payload);
  static void Require(bool condition, const char* message);

  Phase phase_ = Phase::AwaitingBegin;
  std::uint64_t generation_ = 0;
  std::uint64_t lastSequence_ = 0;
  std::uint64_t lastFrame_ = 0;
  std::uint64_t lastResizeGeneration_ = 0;
  std::uint32_t width_ = 0;
  std::uint32_t height_ = 0;
  std::uint64_t droppedFrames_ = 0;
  std::optional<SceneState> openFrame_;
  std::deque<CompleteFrame> queue_;
};

}  // namespace framebridge::native_mirror
