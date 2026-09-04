#pragma once
#include "scene_math.h"
#include <memory>
#include <string>
#include <cstdint>
#include "temporal_frame.h"
#include <vector>
#include <span>

namespace framebridge::render {
class Renderer {
 public:
  explicit Renderer(bool window = true);
  ~Renderer();
  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;
  bool Pump();
  void Submit(const SceneState& state, std::uint64_t dropped, const std::string& capture = {}, std::vector<std::uint8_t>* output = nullptr);
  void SetSessionGeneration(std::uint64_t generation);
  void UpdateTexture(std::span<const std::uint8_t> pixels);
  void Legacy(std::uint64_t frame, std::uint32_t width, std::uint32_t height, const std::string& capture = {});
  void Validate();
  std::string Adapter() const;
  std::string UpscalerMode() const;
  float RenderScale() const;
  std::string Telemetry() const;
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
