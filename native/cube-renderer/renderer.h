#pragma once
#include "scene_math.h"
#include <memory>
#include <string>
#include <cstdint>
#include "temporal_frame.h"

namespace framebridge::render {
class Renderer {
 public:
  explicit Renderer(bool window = true);
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;
  bool Pump();
  void Submit(const SceneState& state, std::uint64_t dropped, const std::string& capture = {});
  void SetSessionGeneration(std::uint64_t generation);
  void Legacy(std::uint64_t frame, std::uint32_t width, std::uint32_t height, const std::string& capture = {});
  void Validate();
  std::string Adapter() const;
  std::string Telemetry() const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
