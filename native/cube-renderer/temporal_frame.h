#pragma once
#include "scene_math.h"
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <memory>

struct ID3D12Resource;
struct ID3D12GraphicsCommandList;
struct ID3D12Device;

namespace framebridge::temporal {

enum class ResetReason : std::uint32_t { None=0, Initialization=1, FirstFrame=2,
  Session=4, Dimensions=8, ResizeGeneration=16, RenderScale=32, Explicit=64 };
inline ResetReason operator|(ResetReason a, ResetReason b) { return static_cast<ResetReason>(static_cast<std::uint32_t>(a)|static_cast<std::uint32_t>(b)); }
inline bool HasReset(ResetReason a, ResetReason b) { return (static_cast<std::uint32_t>(a)&static_cast<std::uint32_t>(b))!=0; }

struct Extent { std::uint32_t width=0, height=0; bool operator==(const Extent&) const = default; };
struct MotionVector { float x=0, y=0; };
struct TemporalFrameResources {
  ID3D12Resource* inputColor=nullptr; ID3D12Resource* inputDepth=nullptr;
  ID3D12Resource* inputMotion=nullptr; ID3D12Resource* outputColor=nullptr;
  Extent inputExtent{}, outputExtent{};
  const char* inputColorFormat="RGBA8Unorm"; const char* inputDepthFormat="R32Float";
  const char* inputMotionFormat="RG16Float"; const char* outputColorFormat="RGBA8Unorm";
  std::uint64_t logicalFrame=0, previousLogicalFrame=0, presentationOrdinal=0, resizeGeneration=0;
  ResetReason resetReason=ResetReason::None;
  bool reset=false, jitterEnabled=false, motionIncludesJitter=false;
  std::array<float,2> jitterOffsetPixels{}; std::array<float,2> motionScale{};
  framebridge::render::Matrices currentUnjittered{}, previousUnjittered{};
  framebridge::render::Matrix jitteredProjection{};
  std::array<MotionVector,8> cornerMotion{};
  std::string motionConvention="previous-to-current; render pixels; top-left origin; camera-inclusive; unjittered";
};

struct TemporalInput { Extent output{}, input{}; float renderScale=1; std::uint64_t presentationOrdinal=0;
  ResetReason resetReason=ResetReason::None; bool jitterEnabled=false; };

bool ValidateInput(const TemporalInput&, std::string* error=nullptr);
std::array<float,2> HaltonJitter(std::uint64_t sample, Extent input);
TemporalFrameResources BuildFrame(const framebridge::render::SceneState&, const TemporalInput&,
  const std::optional<TemporalFrameResources>& previous);

enum class UpscaleStatus { Success, InvalidFrame, SubmissionFailure };
struct UpscaleResult { UpscaleStatus status=UpscaleStatus::InvalidFrame; std::string message; };
class IUpscaler {
 public: virtual ~IUpscaler() = default;
  virtual UpscaleResult Evaluate(const TemporalFrameResources&, ID3D12GraphicsCommandList*) = 0;
};
class ReferenceUpscaler final : public IUpscaler {
 public: UpscaleResult Evaluate(const TemporalFrameResources&, ID3D12GraphicsCommandList*) override;
  void Initialize(::ID3D12Device*);
  ReferenceUpscaler(); ~ReferenceUpscaler();
  ReferenceUpscaler(const ReferenceUpscaler&) = delete;
  ReferenceUpscaler& operator=(const ReferenceUpscaler&) = delete;
 private: struct Impl; std::unique_ptr<Impl> impl_;
};

} // namespace framebridge::temporal
