#include "temporal_frame.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#ifdef _WIN32
#include <d3d12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#endif

namespace framebridge::temporal {
namespace {
float RadicalInverse(std::uint64_t n, std::uint32_t base) { float f=1, r=0; while(n){f/=static_cast<float>(base);r+=f*static_cast<float>(n%base);n/=base;} return r; }
float NdcX(const std::array<double,3>& p) { return static_cast<float>(p[0]); }
float NdcY(const std::array<double,3>& p) { return static_cast<float>(p[1]); }
}
bool ValidateInput(const TemporalInput& in, std::string* error) {
  const auto fail=[&](const char* e){if(error)*error=e;return false;};
  if(!(in.renderScale==1.0f||in.renderScale==0.5f)) return fail("render scale must be 0.5 or 1.0");
  if(!in.output.width||!in.output.height||in.output.width>4096||in.output.height>4096) return fail("invalid output extent");
  const auto w=static_cast<std::uint32_t>(std::lround(in.output.width*in.renderScale));
  const auto h=static_cast<std::uint32_t>(std::lround(in.output.height*in.renderScale));
  if(!w||!h||w>4096||h>4096 || in.input != Extent{w,h}) return fail("input extent must equal rounded output extent times render scale");
  if(in.presentationOrdinal==0) return fail("presentation ordinal must be positive");
  return true;
}
std::array<float,2> HaltonJitter(std::uint64_t sample, Extent input) {
  if(!input.width||!input.height) return {0,0};
  const auto n=sample%8+1; return {RadicalInverse(n,2)-.5f, RadicalInverse(n,3)-.5f};
}
TemporalFrameResources BuildFrame(const framebridge::render::SceneState& state,const TemporalInput& in,const std::optional<TemporalFrameResources>& previous) {
  std::string error; if(!ValidateInput(in,&error)) throw std::invalid_argument(error);
  TemporalFrameResources out; out.outputExtent=in.output; out.inputExtent=in.input; out.logicalFrame=state.frame;
  out.presentationOrdinal=in.presentationOrdinal; out.resizeGeneration=state.resizeGeneration; out.jitterEnabled=in.jitterEnabled;
  out.resetReason=in.resetReason;
  out.reset=in.resetReason!=ResetReason::None || !previous.has_value();
  out.jitterOffsetPixels=in.jitterEnabled?HaltonJitter(in.presentationOrdinal,in.input):std::array<float,2>{0,0};
  out.currentUnjittered=framebridge::render::SceneMatrices(state);
  out.jitteredProjection=out.currentUnjittered.projection;
  if(in.jitterEnabled) { out.jitteredProjection[8]+=2.0*static_cast<double>(out.jitterOffsetPixels[0])/in.input.width; out.jitteredProjection[9]-=2.0*static_cast<double>(out.jitterOffsetPixels[1])/in.input.height; }
  if(previous) { out.previousUnjittered=previous->currentUnjittered; out.previousLogicalFrame=previous->logicalFrame; }
  if(!previous || out.reset) out.previousUnjittered=out.currentUnjittered;
  for(std::size_t i=0;i<8;++i) {
    if(!previous || out.reset) continue;
    const auto& c=out.currentUnjittered.corners[i]; const auto& p=out.previousUnjittered.corners[i];
    out.cornerMotion[i]={ (NdcX(c)-NdcX(p))*static_cast<float>(in.input.width)*.5f,
      (NdcY(p)-NdcY(c))*static_cast<float>(in.input.height)*.5f };
  }
  out.motionScale={1.0f/static_cast<float>(in.input.width),1.0f/static_cast<float>(in.input.height)};
  return out;
}
struct ReferenceUpscaler::Impl {
#ifdef _WIN32
  Microsoft::WRL::ComPtr<ID3D12Device> device;
  Microsoft::WRL::ComPtr<ID3D12RootSignature> root;
  Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline;
  Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srv,rtv;
  UINT stride=0;
#endif
};
ReferenceUpscaler::ReferenceUpscaler():impl_(std::make_unique<Impl>()){}
ReferenceUpscaler::~ReferenceUpscaler()=default;
void ReferenceUpscaler::Initialize(ID3D12Device* raw) {
#ifdef _WIN32
  if(!raw) throw std::invalid_argument("reference upscaler device");
  auto& i=*impl_; raw->QueryInterface(IID_PPV_ARGS(&i.device)); if(!i.device) throw std::runtime_error("reference upscaler device identity");
  D3D12_DESCRIPTOR_HEAP_DESC hd{};hd.NumDescriptors=1;hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
  if(FAILED(i.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&i.srv))))throw std::runtime_error("reference SRV heap");
  hd.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;hd.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  if(FAILED(i.device->CreateDescriptorHeap(&hd,IID_PPV_ARGS(&i.rtv))))throw std::runtime_error("reference RTV heap");
  D3D12_DESCRIPTOR_RANGE range{};range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=1;
  D3D12_ROOT_PARAMETER p{};p.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;p.DescriptorTable.NumDescriptorRanges=1;p.DescriptorTable.pDescriptorRanges=&range;p.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_STATIC_SAMPLER_DESC samp{};samp.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;samp.AddressU=samp.AddressV=samp.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;samp.MaxLOD=D3D12_FLOAT32_MAX;samp.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;samp.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
  D3D12_ROOT_SIGNATURE_DESC rd{};rd.NumParameters=1;rd.pParameters=&p;rd.NumStaticSamplers=1;rd.pStaticSamplers=&samp;
  Microsoft::WRL::ComPtr<ID3DBlob> blob,err,vs,ps;
  if(FAILED(D3D12SerializeRootSignature(&rd,D3D_ROOT_SIGNATURE_VERSION_1,&blob,&err))||FAILED(i.device->CreateRootSignature(0,blob->GetBufferPointer(),blob->GetBufferSize(),IID_PPV_ARGS(&i.root))))throw std::runtime_error("reference root signature");
  const char* src="Texture2D<float4> t:register(t0);SamplerState s:register(s0);struct V{float4 p:SV_Position;float2 uv:TEXCOORD0;};V vs(uint id:SV_VertexID){V o;o.uv=float2((id<<1)&2,id&2);o.p=float4(o.uv*float2(2,-2)+float2(-1,1),0,1);return o;}float4 ps(V v):SV_Target{return t.Sample(s,v.uv);}";
  if(FAILED(D3DCompile(src,strlen(src),nullptr,nullptr,nullptr,"vs","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vs,&err))||FAILED(D3DCompile(src,strlen(src),nullptr,nullptr,nullptr,"ps","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&ps,&err)))throw std::runtime_error("reference shaders");
  D3D12_GRAPHICS_PIPELINE_STATE_DESC pd{};pd.pRootSignature=i.root.Get();pd.VS={vs->GetBufferPointer(),vs->GetBufferSize()};pd.PS={ps->GetBufferPointer(),ps->GetBufferSize()};pd.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;pd.SampleMask=UINT_MAX;pd.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;pd.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;pd.RasterizerState.DepthClipEnable=TRUE;pd.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;pd.NumRenderTargets=1;pd.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;pd.SampleDesc.Count=1;
  if(FAILED(i.device->CreateGraphicsPipelineState(&pd,IID_PPV_ARGS(&i.pipeline))))throw std::runtime_error("reference pipeline");
  i.stride=i.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
#else
  (void)raw;
#endif
}
UpscaleResult ReferenceUpscaler::Evaluate(const TemporalFrameResources& frame, ID3D12GraphicsCommandList* list) {
  if(!frame.inputColor||!frame.outputColor||!frame.inputExtent.width||!frame.outputExtent.width)
    return {UpscaleStatus::InvalidFrame,"reference upscaler requires explicit input/output resources"};
#ifdef _WIN32
  if(!list||!impl_->pipeline) return {UpscaleStatus::SubmissionFailure,"reference upscaler is not initialized"};
  auto& i=*impl_;D3D12_RESOURCE_BARRIER b[2]{};
  b[0].Type=b[1].Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b[0].Transition={frame.inputColor,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE};b[1].Transition={frame.outputColor,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_RENDER_TARGET};list->ResourceBarrier(2,b);
  D3D12_SHADER_RESOURCE_VIEW_DESC sd{};sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;sd.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;sd.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;sd.Texture2D.MipLevels=1;i.device->CreateShaderResourceView(frame.inputColor,&sd,i.srv->GetCPUDescriptorHandleForHeapStart());i.device->CreateRenderTargetView(frame.outputColor,nullptr,i.rtv->GetCPUDescriptorHandleForHeapStart());
  auto r=i.rtv->GetCPUDescriptorHandleForHeapStart();list->OMSetRenderTargets(1,&r,FALSE,nullptr);D3D12_VIEWPORT v{0,0,(float)frame.outputExtent.width,(float)frame.outputExtent.height,0,1};D3D12_RECT sc{0,0,(LONG)frame.outputExtent.width,(LONG)frame.outputExtent.height};list->RSSetViewports(1,&v);list->RSSetScissorRects(1,&sc);ID3D12DescriptorHeap* hs[]={i.srv.Get()};list->SetDescriptorHeaps(1,hs);list->SetGraphicsRootSignature(i.root.Get());list->SetPipelineState(i.pipeline.Get());list->SetGraphicsRootDescriptorTable(0,i.srv->GetGPUDescriptorHandleForHeapStart());list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);list->DrawInstanced(3,1,0,0);
  std::swap(b[0].Transition.StateBefore,b[0].Transition.StateAfter);std::swap(b[1].Transition.StateBefore,b[1].Transition.StateAfter);list->ResourceBarrier(2,b);
#endif
  return {UpscaleStatus::Success,"reference GPU scaling pass accepted"};
}
} // namespace framebridge::temporal
