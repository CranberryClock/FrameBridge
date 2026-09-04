#include <windows.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <d3dcompiler.h>
#include <atomic>
#include <optional>
#include <stdexcept>
#include "renderer.h"
#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include <dawn/dawn_proc.h>
#include <dawn/native/D3D12Backend.h>
#include <dawn/native/DawnNative.h>
#include <dawn/webgpu_cpp.h>

using Microsoft::WRL::ComPtr;

namespace {

std::atomic<bool> g_badDeviceLost{false};
std::atomic<bool> g_uncapturedError{false};
uint32_t g_d3d12Messages = 0;
uint32_t g_dxgiMessages = 0;

void Die(const char* operation, HRESULT hr = S_OK) {
    throw std::runtime_error(std::string(operation) + " HRESULT=" + std::to_string(static_cast<unsigned long>(hr)));
}
void Check(HRESULT hr, const char* operation) { if (FAILED(hr)) Die(operation, hr); }
void CheckStatus(wgpu::Status status, const char* operation) {
    if (status != wgpu::Status::Success) Die(operation);
}
std::string ToString(wgpu::StringView s) { return std::string(s.data, s.length); }
std::string ReadEnv(const char* name) { char* value=nullptr; size_t size=0; if(_dupenv_s(&value,&size,name)!=0 || !value) return {}; std::string result=value; free(value); return result; }

struct Mat4 { float v[16]{}; };
Mat4 Multiply(const Mat4& a, const Mat4& b) {
    Mat4 out{};
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
        for (int k = 0; k < 4; ++k) out.v[c * 4 + r] += a.v[k * 4 + r] * b.v[c * 4 + k];
    return out;
}
Mat4 Perspective(float aspect) {
    Mat4 m{}; const float f = 1.0f / std::tan(3.14159265358979323846f / 4.0f); // fixed 90-degree vertical FOV
    // WebGPU/D3D12 clip depth is [0, 1]; the camera looks down +Z.
    m.v[0] = f / aspect; m.v[5] = f; m.v[10] = 1.001f; m.v[11] = 1.0f; m.v[14] = -0.1001f;
    return m;
}
Mat4 Rotation(float radians) {
    Mat4 m{}; const float c = std::cos(radians), s = std::sin(radians);
    m.v[0] = c; m.v[2] = -s; m.v[5] = 1.0f; m.v[8] = s; m.v[10] = c; m.v[15] = 1.0f;
    return m;
}
Mat4 Translate(float z) { Mat4 m{}; m.v[0] = m.v[5] = m.v[10] = m.v[15] = 1.0f; m.v[14] = z; return m; }

uint32_t Crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xffffffffu;
    for (size_t i = 0; i < size; ++i) { crc ^= data[i]; for (int j = 0; j < 8; ++j) { const uint32_t mask = (crc & 1u) ? 0xffffffffu : 0u; crc = (crc >> 1) ^ (0xedb88320u & mask); } }
    return ~crc;
}
void Put32(std::vector<uint8_t>& out, uint32_t value) { out.push_back(static_cast<uint8_t>(value >> 24)); out.push_back(static_cast<uint8_t>(value >> 16)); out.push_back(static_cast<uint8_t>(value >> 8)); out.push_back(static_cast<uint8_t>(value)); }
void Chunk(std::vector<uint8_t>& out, const char* type, const std::vector<uint8_t>& data) {
    Put32(out, static_cast<uint32_t>(data.size())); size_t start = out.size();
    out.insert(out.end(), type, type + 4); out.insert(out.end(), data.begin(), data.end());
    Put32(out, Crc32(out.data() + start, 4 + data.size()));
}
void WritePng(const std::string& path, uint32_t width, uint32_t height, const std::vector<uint8_t>& rgba) {
    std::vector<uint8_t> png = {137,80,78,71,13,10,26,10};
    std::vector<uint8_t> ihdr; Put32(ihdr, width); Put32(ihdr, height); ihdr.insert(ihdr.end(), {8,6,0,0,0}); Chunk(png, "IHDR", ihdr);
    std::vector<uint8_t> raw; raw.reserve((width * 4 + 1) * height);
    for (uint32_t y = 0; y < height; ++y) { raw.push_back(0); raw.insert(raw.end(), rgba.begin() + y * width * 4, rgba.begin() + (y + 1) * width * 4); }
    std::vector<uint8_t> z = {120, 1}; size_t pos = 0;
    while (pos < raw.size()) { uint16_t n = static_cast<uint16_t>(std::min<size_t>(65535, raw.size() - pos)); bool last = pos + n == raw.size(); z.push_back(last ? 1 : 0); z.push_back(static_cast<uint8_t>(n)); z.push_back(static_cast<uint8_t>(n >> 8)); uint16_t inv = static_cast<uint16_t>(~n); z.push_back(static_cast<uint8_t>(inv)); z.push_back(static_cast<uint8_t>(inv >> 8)); z.insert(z.end(), raw.begin() + pos, raw.begin() + pos + n); pos += n; }
    uint32_t adler = 1; uint32_t a = 1, b = 0; for (uint8_t x : raw) { a = (a + x) % 65521; b = (b + a) % 65521; } adler = (b << 16) | a; Put32(z, adler); Chunk(png, "IDAT", z); Chunk(png, "IEND", {});
    std::ofstream file(path, std::ios::binary); file.write(reinterpret_cast<const char*>(png.data()), png.size());
}

struct Readback { ComPtr<ID3D12Resource> buffer; uint32_t rowPitch = 0; std::vector<uint8_t> rgba; };

class CubeRenderer {
  public:
    CubeRenderer(wgpu::Device device, ComPtr<ID3D12Device> nativeDevice, ComPtr<ID3D12CommandQueue> nativeQueue, bool canonical)
        : device_(device), d3d_(std::move(nativeDevice)), queue_(std::move(nativeQueue)) {
        queueWeb_ = device_.GetQueue();
        BuildPipeline(canonical);
        float vertices[] = {
            -1,-1,-1, 1,0,0,  1,-1,-1, 0,1,0,  1,1,-1, 0,0,1, -1,1,-1, 1,1,0,
            -1,-1, 1, 1,0,1,  1,-1, 1, 0,1,1,  1,1,1, 1,1,1, -1,1,1, 0,0,0};
        if (canonical) for (int i=0;i<8;++i) for(int j=0;j<3;++j) vertices[i*6+j]*=.5f;
        const uint16_t indices[] = {0,1,2, 2,3,0, 1,5,6, 6,2,1, 5,4,7, 7,6,5, 4,0,3, 3,7,4, 3,2,6, 6,7,3, 4,5,1, 1,0,4};
        vertex_ = MakeBuffer(sizeof(vertices), wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst);
        index_ = MakeBuffer(sizeof(indices), wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst);
        uniform_ = MakeBuffer(256, wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst);
        queueWeb_.WriteBuffer(vertex_, 0, vertices, sizeof(vertices)); queueWeb_.WriteBuffer(index_, 0, indices, sizeof(indices));
        wgpu::BindGroupEntry entry; entry.binding = 0; entry.buffer = uniform_; entry.size = 256;
        wgpu::BindGroupDescriptor bgd; bgd.layout = bindLayout_; bgd.entryCount = 1; bgd.entries = &entry; bindGroup_ = device_.CreateBindGroup(&bgd);
        Check(d3d_->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator_)), "Create(capture allocator)");
        Check(d3d_->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_)), "Create(capture fence)" ); event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    }
    ~CubeRenderer() { if (event_) CloseHandle(event_); }
    void Resize(uint32_t width, uint32_t height) {
        if(width_==width && height_==height) return;
        Target previous{width_,height_,textureInitialized_,std::move(nativeTexture_),std::move(memory_),std::move(texture_),std::move(depth_),std::move(colorView_),std::move(depthView_)};
        if(cached_ && cached_->width==width && cached_->height==height) {
            auto next=std::move(*cached_); cached_=std::move(previous);
            nativeTexture_=std::move(next.native); memory_=std::move(next.memory); texture_=std::move(next.texture);
            depth_=std::move(next.depth); colorView_=std::move(next.colorView); depthView_=std::move(next.depthView);
            textureInitialized_=next.initialized; width_=width; height_=height;
            readback_={}; readback_.rowPitch=(width*4+255)&~255u; return;
        }
        cached_=std::move(previous);
        width_ = width; height_ = height; depth_ = {}; readback_ = {}; textureInitialized_ = false;
        ++targetAllocations_;
        D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; desc.Width = width; desc.Height = height; desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT; D3D12_CLEAR_VALUE clear{}; clear.Format = desc.Format; clear.Color[3] = 1;
        Check(d3d_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, &clear, IID_PPV_ARGS(&nativeTexture_)), "CreateCommittedResource(color)");
        dawn::native::d3d12::SharedTextureMemoryD3D12ResourceDescriptor importDesc; importDesc.resource = nativeTexture_;
        wgpu::SharedTextureMemoryDescriptor md; md.nextInChain = &importDesc; md.label = "FrameBridge native cube color"; memory_ = device_.ImportSharedTextureMemory(&md); texture_ = memory_.CreateTexture();
        wgpu::TextureDescriptor depthDesc; depthDesc.dimension = wgpu::TextureDimension::e2D; depthDesc.size = {width, height, 1}; depthDesc.format = wgpu::TextureFormat::Depth24Plus; depthDesc.usage = wgpu::TextureUsage::RenderAttachment; depth_ = device_.CreateTexture(&depthDesc);
        colorView_ = texture_.CreateView(); depthView_ = depth_.CreateView();
        readback_.rowPitch = (width * 4 + 255) & ~255u;
    }
    void Render(const Mat4& mvp, bool capture, bool canonical) {
        wgpu::SharedTextureMemoryBeginAccessDescriptor begin{}; begin.initialized = textureInitialized_; CheckStatus(memory_.BeginAccess(texture_, &begin), "BeginAccess(cube)");
        queueWeb_.WriteBuffer(uniform_, 0, mvp.v, sizeof(mvp.v));
        wgpu::RenderPassColorAttachment color; color.view = colorView_; color.loadOp = wgpu::LoadOp::Clear; color.storeOp = wgpu::StoreOp::Store; color.clearValue = canonical ? wgpu::Color{8.0/255,11.0/255,18.0/255,1} : wgpu::Color{0.03,0.04,0.07,1};
        wgpu::RenderPassDepthStencilAttachment depth; depth.view = depthView_; depth.depthLoadOp = wgpu::LoadOp::Clear; depth.depthStoreOp = wgpu::StoreOp::Store; depth.depthClearValue = 1;
        wgpu::RenderPassDescriptor pd; pd.colorAttachmentCount = 1; pd.colorAttachments = &color; pd.depthStencilAttachment = &depth; wgpu::CommandEncoder enc = device_.CreateCommandEncoder(); wgpu::RenderPassEncoder pass = enc.BeginRenderPass(&pd); pass.SetPipeline(pipeline_); pass.SetBindGroup(0, bindGroup_); pass.SetVertexBuffer(0, vertex_); pass.SetIndexBuffer(index_, wgpu::IndexFormat::Uint16); pass.DrawIndexed(36); pass.End(); wgpu::CommandBuffer cb = enc.Finish(); queueWeb_.Submit(1, &cb);
        wgpu::SharedTextureMemoryEndAccessState end{}; CheckStatus(memory_.EndAccess(texture_, &end), "EndAccess(cube)"); textureInitialized_ = true;
        uint64_t value = (serial_ += 2); Check(queue_->Signal(fence_.Get(), value), "Signal(Dawn cube boundary)"); Wait(value);
        if (capture) Capture(value);
    }
    ID3D12Resource* Resource() const { return nativeTexture_.Get(); }
    uint64_t TargetAllocations() const { return targetAllocations_; }
    uint32_t width() const { return width_; } uint32_t height() const { return height_; }
    const std::vector<uint8_t>& pixels() const { return readback_.rgba; }
  private:
    struct Target {
        uint32_t width=0,height=0; bool initialized=false;
        ComPtr<ID3D12Resource> native; wgpu::SharedTextureMemory memory;
        wgpu::Texture texture,depth; wgpu::TextureView colorView,depthView;
    };
    std::optional<Target> cached_;
    uint64_t targetAllocations_=0;
    wgpu::Buffer MakeBuffer(uint64_t size, wgpu::BufferUsage usage) { wgpu::BufferDescriptor bd; bd.size = size; bd.usage = usage; return device_.CreateBuffer(&bd); }
    void BuildPipeline(bool canonical) {
        wgpu::BindGroupLayoutEntry be; be.binding = 0; be.visibility = wgpu::ShaderStage::Vertex; be.buffer.type = wgpu::BufferBindingType::Uniform;
        wgpu::BindGroupLayoutDescriptor bld; bld.entryCount = 1; bld.entries = &be; bindLayout_ = device_.CreateBindGroupLayout(&bld); wgpu::PipelineLayoutDescriptor pld; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bindLayout_; wgpu::PipelineLayout layout = device_.CreatePipelineLayout(&pld);
        const char* source = "struct U { mvp: mat4x4f }; @group(0) @binding(0) var<uniform> u: U; struct V { @location(0) pos: vec3f, @location(1) color: vec3f }; struct O { @builtin(position) pos: vec4f, @location(0) color: vec3f }; @vertex fn vs(v:V)->O { var o:O; o.pos=u.mvp*vec4f(v.pos,1); o.color=v.color; return o; } @fragment fn fs(o:O)->@location(0) vec4f { return vec4f(o.color,1); }";
        std::string shader(source); if(canonical) { const auto pos=shader.find("vec4f(o.color,1)"); shader.replace(pos,16,"vec4f(54.0/255.0,214.0/255.0,1,1)"); }
        wgpu::ShaderSourceWGSL wgsl; wgsl.code = shader.c_str(); wgpu::ShaderModuleDescriptor sd; sd.nextInChain = &wgsl; wgpu::ShaderModule sm = device_.CreateShaderModule(&sd); wgpu::VertexAttribute attrs[2]; attrs[0].format = wgpu::VertexFormat::Float32x3; attrs[0].offset = 0; attrs[0].shaderLocation = 0; attrs[1].format = wgpu::VertexFormat::Float32x3; attrs[1].offset = 12; attrs[1].shaderLocation = 1; wgpu::VertexBufferLayout vb; vb.arrayStride = 24; vb.attributeCount = 2; vb.attributes = attrs; wgpu::ColorTargetState ct; ct.format = wgpu::TextureFormat::RGBA8Unorm; wgpu::FragmentState fs; fs.module = sm; fs.entryPoint = "fs"; fs.targetCount = 1; fs.targets = &ct; wgpu::DepthStencilState ds; ds.format = wgpu::TextureFormat::Depth24Plus; ds.depthWriteEnabled = true; ds.depthCompare = wgpu::CompareFunction::Less; wgpu::RenderPipelineDescriptor rp; rp.layout = layout; rp.vertex.module = sm; rp.vertex.entryPoint = "vs"; rp.vertex.bufferCount = 1; rp.vertex.buffers = &vb; rp.fragment = &fs; rp.depthStencil = &ds; pipeline_ = device_.CreateRenderPipeline(&rp);
    }
    void Wait(uint64_t value) { if (fence_->GetCompletedValue() < value) { Check(fence_->SetEventOnCompletion(value, event_), "SetEventOnCompletion"); if (WaitForSingleObject(event_, 10000) != WAIT_OBJECT_0) Die("GPU fence timeout"); } }
    void Capture(uint64_t value) { if(!readback_.buffer) { D3D12_RESOURCE_DESC rb{}; rb.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rb.Width = static_cast<uint64_t>(readback_.rowPitch) * height_; rb.Height = 1; rb.DepthOrArraySize = 1; rb.MipLevels = 1; rb.SampleDesc.Count = 1; rb.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; D3D12_HEAP_PROPERTIES readHeap{}; readHeap.Type = D3D12_HEAP_TYPE_READBACK; Check(d3d_->CreateCommittedResource(&readHeap, D3D12_HEAP_FLAG_NONE, &rb, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback_.buffer)), "CreateCommittedResource(readback)"); } Check(allocator_->Reset(), "Reset(capture allocator)"); if (!list_) { Check(d3d_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr, IID_PPV_ARGS(&list_)), "Create(capture list)"); Check(list_->Close(), "Close(initial capture list)"); } Check(list_->Reset(allocator_.Get(), nullptr), "Reset(capture list)"); D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = nativeTexture_.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = 0; D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = readback_.buffer.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM; dst.PlacedFootprint.Footprint.Width = width_; dst.PlacedFootprint.Footprint.Height = height_; dst.PlacedFootprint.Footprint.Depth = 1; dst.PlacedFootprint.Footprint.RowPitch = readback_.rowPitch; list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr); Check(list_->Close(), "Close(capture list)"); ID3D12CommandList* lists[] = {list_.Get()}; queue_->ExecuteCommandLists(1, lists); uint64_t copyValue = value + 1; Check(queue_->Signal(fence_.Get(), copyValue), "Signal(capture)"); Wait(copyValue); void* mapped = nullptr; D3D12_RANGE range{0, static_cast<SIZE_T>(readback_.rowPitch) * height_}; Check(readback_.buffer->Map(0, &range, &mapped), "Map(capture)"); readback_.rgba.resize(static_cast<size_t>(width_) * height_ * 4); for (uint32_t y = 0; y < height_; ++y) std::copy_n(static_cast<uint8_t*>(mapped) + y * readback_.rowPitch, width_ * 4, readback_.rgba.begin() + y * width_ * 4); D3D12_RANGE written{0,0}; readback_.buffer->Unmap(0, &written); }
    wgpu::Device device_; wgpu::Queue queueWeb_; ComPtr<ID3D12Device> d3d_; ComPtr<ID3D12CommandQueue> queue_; ComPtr<ID3D12Resource> nativeTexture_; wgpu::SharedTextureMemory memory_; wgpu::Texture texture_, depth_; wgpu::TextureView colorView_, depthView_; wgpu::Buffer vertex_, index_, uniform_; wgpu::BindGroupLayout bindLayout_; wgpu::BindGroup bindGroup_; wgpu::RenderPipeline pipeline_; uint32_t width_ = 0, height_ = 0; bool textureInitialized_ = false; Readback readback_; ComPtr<ID3D12CommandAllocator> allocator_ = nullptr; ComPtr<ID3D12GraphicsCommandList> list_; ComPtr<ID3D12Fence> fence_; HANDLE event_ = nullptr; uint64_t serial_ = 0;
};

} // namespace

void ReportInfoQueue(ID3D12Device* device) {
    ComPtr<ID3D12InfoQueue> queue;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&queue)))) return;
    const UINT64 count = queue->GetNumStoredMessages();
    for (UINT64 i = 0; i < count; ++i) {
        SIZE_T bytes = 0; queue->GetMessage(i, nullptr, &bytes);
        std::vector<uint8_t> storage(bytes); auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
        if (SUCCEEDED(queue->GetMessage(i, message, &bytes)) && message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING) {
            ++g_d3d12Messages; std::cerr << "FAIL d3d12_infoqueue severity=" << static_cast<int>(message->Severity) << " description=" << message->pDescription << "\n";
        }
    }
    queue->ClearStoredMessages();
}

namespace framebridge::render {
struct Renderer::Impl {
    std::unique_ptr<dawn::native::Instance> instance;
    wgpu::Device device;
    ComPtr<ID3D12Device> native;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<IDXGIFactory4> factory;
    ComPtr<IDXGIInfoQueue> dxgiInfo;
    ComPtr<IDXGISwapChain3> swapchain;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    ComPtr<ID3D12RootSignature> blitRoot;
    ComPtr<ID3D12PipelineState> blitPipeline, temporalPipeline;
    ComPtr<ID3D12RootSignature> temporalRoot;
    ComPtr<ID3D12DescriptorHeap> temporalRtv;
    ComPtr<ID3D12Resource> temporalConstants;
    struct PlaneReadback { ComPtr<ID3D12Resource> buffer; UINT rowPitch=0; UINT bytesPerPixel=0; } depthReadback, motionReadback;
    ComPtr<ID3D12Resource> outputReadback; UINT64 outputReadbackBytes=0; ComPtr<ID3D12DescriptorHeap> outputDiagnosticRtv; uint64_t outputDiagnosticFrame=0;
    uint64_t depthForegroundPixels=0, motionNonZeroPixels=0, temporalReadbackFrames=0;
    bool temporalResourcesSameDevice=false;
    ComPtr<ID3D12DescriptorHeap> srvHeap,rtvHeap;
    struct AuxTarget { uint32_t inputWidth=0,inputHeight=0,outputWidth=0,outputHeight=0; ComPtr<ID3D12Resource> depth,motion,output; };
    AuxTarget activeAux{};
    std::optional<AuxTarget> cachedAux;
    HANDLE event = nullptr;
    HWND window = nullptr;
    std::unique_ptr<CubeRenderer> cube;
    std::string name;
    LUID luid{};
    uint64_t serial = 0, submitted = 0, resizeGeneration = 0, dropped = 0;
    uint32_t width = 0, height = 0;
    uint32_t swapWidth = 0, swapHeight = 0;
    uint64_t swapAllocations = 0;
    uint64_t outputAllocations = 0, presentationOrdinal = 0, temporalResets = 0, sessionGeneration = 0;
    bool sessionResetPending = false;
    uint32_t outputWidth = 0, outputHeight = 0, inputWidth = 0, inputHeight = 0;
    float renderScale = 1.0f;
    bool jitterDiagnostic = false;
    uint64_t jitteredFrames = 0, nonJitteredFrames = 0;
    std::optional<temporal::TemporalFrameResources> history;
    temporal::ReferenceUpscaler upscaler;
    bool open = true, canonical = true;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
        if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
        return DefWindowProcW(hwnd,msg,wp,lp);
    }
    explicit Impl(bool show) : canonical(show) {
        const auto scaleValue=ReadEnv("FRAMEBRIDGE_RENDER_SCALE");
        if(!scaleValue.empty()) {
            char* end=nullptr; renderScale=std::strtof(scaleValue.c_str(),&end);
            if(!end || *end!='\0' || !(renderScale==1.0f || renderScale==0.5f)) Die("invalid FRAMEBRIDGE_RENDER_SCALE");
        }
        jitterDiagnostic=!ReadEnv("FRAMEBRIDGE_TEMPORAL_JITTER").empty();
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        dawnProcSetProcs(&dawn::native::GetProcs());
        ComPtr<ID3D12Debug> debug;
        Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)), "D3D12 debug layer required");
        debug->EnableDebugLayer();
        Check(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG,IID_PPV_ARGS(&factory)),"CreateDXGIFactory");
        Check(DXGIGetDebugInterface1(0,IID_PPV_ARGS(&dxgiInfo)),"DXGI InfoQueue required");
        Check(dxgiInfo->SetMessageCountLimit(DXGI_DEBUG_DXGI,256),"DXGI message quota");
        ComPtr<IDXGIAdapter1> dxgi;
        DXGI_ADAPTER_DESC1 identity{};
        bool found = false;
        for (UINT i=0; factory->EnumAdapters1(i,&dxgi) != DXGI_ERROR_NOT_FOUND; ++i) {
            Check(dxgi->GetDesc1(&identity),"GetAdapterDesc");
            // TCW-002 identity: NVIDIA RTX device 0x27e0. LUID is revalidated each boot.
            if (identity.VendorId == 0x10de && identity.DeviceId == 0x27e0) { found=true; break; }
            dxgi.Reset();
        }
        if (!found) Die("TCW-002 NVIDIA adapter not found");
        luid = identity.AdapterLuid;
        const char* unsafe = "allow_unsafe_apis";
        wgpu::DawnTogglesDescriptor toggles; toggles.enabledToggleCount=1; toggles.enabledToggles=&unsafe;
        dawn::native::DawnInstanceDescriptor dd; dd.nextInChain=&toggles;
        dd.backendValidationLevel=dawn::native::BackendValidationLevel::Full;
        wgpu::InstanceDescriptor id; id.nextInChain=&dd;
        instance=std::make_unique<dawn::native::Instance>(&id);
        dawn::native::d3d::RequestAdapterOptionsLUID selectedLuid;
        selectedLuid.adapterLUID=luid;
        wgpu::RequestAdapterOptions ao; ao.backendType=wgpu::BackendType::D3D12; ao.nextInChain=&selectedLuid;
        auto adapters=instance->EnumerateAdapters(&ao);
        if(adapters.size()!=1) Die("Enumerate exact adapter LUID");
        wgpu::Adapter adapter(adapters[0].Get()); wgpu::AdapterInfo ai{};
        CheckStatus(adapter.GetInfo(&ai),"AdapterInfo");
        if(ai.vendorID != 0x10de || ai.deviceID != identity.DeviceId) Die("Dawn adapter identity mismatch");
        name=ToString(ai.device);
        const wgpu::FeatureName features[]={wgpu::FeatureName::SharedTextureMemoryD3D12Resource};
        wgpu::DeviceDescriptor desc; desc.requiredFeatureCount=1; desc.requiredFeatures=features;
        desc.SetDeviceLostCallback(wgpu::CallbackMode::AllowProcessEvents, [](const wgpu::Device&,wgpu::DeviceLostReason reason,wgpu::StringView) {
            if(reason != wgpu::DeviceLostReason::Destroyed && reason != wgpu::DeviceLostReason::CallbackCancelled) g_badDeviceLost=true;
        });
        desc.SetUncapturedErrorCallback([](const wgpu::Device&,wgpu::ErrorType,wgpu::StringView message) {
            g_uncapturedError=true; std::cerr<<"DAWN_VALIDATION_ERROR "<<ToString(message)<<"\n";
        });
        device=wgpu::Device::Acquire(adapters[0].CreateDevice(&desc));
        if(!device) Die("Dawn CreateDevice");
        native=dawn::native::d3d12::GetD3D12Device(device.Get());
        queue=dawn::native::d3d12::GetD3D12CommandQueue(device.Get());
        if(!native || !queue) Die("D3D12 extraction");
        const LUID actual=native->GetAdapterLuid();
        if(actual.LowPart!=luid.LowPart || actual.HighPart!=luid.HighPart) Die("D3D12 LUID mismatch");
        ComPtr<ID3D12Device> queueDevice;
        Check(queue->GetDevice(IID_PPV_ARGS(&queueDevice)),"Queue device");
        ComPtr<IUnknown> a,b; Check(native.As(&a),"device identity"); Check(queueDevice.As(&b),"queue identity");
        if(a.Get()!=b.Get()) Die("Queue COM identity mismatch");
        ComPtr<ID3D12InfoQueue> info; Check(native.As(&info),"D3D12 InfoQueue required");
        upscaler.Initialize(native.Get());
        cube=std::make_unique<CubeRenderer>(device,native,queue,canonical);
        Check(native->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"present allocator");
        Check(native->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"present list");
        Check(list->Close(),"initial list close");
        Check(native->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"present fence");
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr); if(!event) Die("present fence event");
        BuildTemporalInputs();
        if(show) {
            BuildBlit();
            WNDCLASSW wc{}; wc.lpfnWndProc=WndProc; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"FrameBridgeMirror";
            if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) Die("RegisterClass");
            window=CreateWindowExW(0,wc.lpszClassName,L"FrameBridge Native Mirror",WS_OVERLAPPEDWINDOW,
                                   CW_USEDEFAULT,CW_USEDEFAULT,640,360,nullptr,nullptr,wc.hInstance,nullptr);
            if(!window) Die("CreateWindow");
            Resize(640,360,static_cast<uint32_t>(640*renderScale),static_cast<uint32_t>(360*renderScale));
            ShowWindow(window,SW_SHOW);
        }
        Validate();
    }
    ~Impl() {
        cube.reset();
        swapchain.Reset();
        if(window && IsWindow(window)) DestroyWindow(window);
        if(event) CloseHandle(event);
        if(device) { device.Destroy(); wgpu::Instance(instance->Get()).ProcessEvents(); }
    }
    void Wait() {
        Check(queue->Signal(fence.Get(),++serial),"present signal");
        if(fence->GetCompletedValue()<serial) {
            Check(fence->SetEventOnCompletion(serial,event),"present wait");
            if(WaitForSingleObject(event,10000)!=WAIT_OBJECT_0) Die("present timeout");
        }
    }
    void BuildBlit() {
        D3D12_DESCRIPTOR_HEAP_DESC heap{}; heap.NumDescriptors=1;
        heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        Check(native->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&srvHeap)),"blit SRV heap");
        heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV; heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        Check(native->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&rtvHeap)),"blit RTV heap");
        D3D12_DESCRIPTOR_RANGE range{}; range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV; range.NumDescriptors=1;
        D3D12_ROOT_PARAMETER param{}; param.ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        param.DescriptorTable.NumDescriptorRanges=1; param.DescriptorTable.pDescriptorRanges=&range; param.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_STATIC_SAMPLER_DESC sampler{}; sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.MaxLOD=D3D12_FLOAT32_MAX; sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS; sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC root{}; root.NumParameters=1;root.pParameters=&param;root.NumStaticSamplers=1;root.pStaticSamplers=&sampler;
        ComPtr<ID3DBlob> serialized,errors,vs,ps;
        Check(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors),"blit root serialization");
        Check(native->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&blitRoot)),"blit root");
        const char* shader=R"(
            Texture2D<float4> image : register(t0); SamplerState pointSampler : register(s0);
            struct V { float4 pos:SV_Position; float2 uv:TEXCOORD0; };
            V vs(uint id:SV_VertexID) { V o; o.uv=float2((id<<1)&2,id&2); o.pos=float4(o.uv*float2(2,-2)+float2(-1,1),0,1); return o; }
            float4 ps(V v):SV_Target { return image.SampleLevel(pointSampler,v.uv,0); }
        )";
        Check(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"vs","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vs,&errors),"blit VS");
        Check(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"ps","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&ps,&errors),"blit PS");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC p{}; p.pRootSignature=blitRoot.Get();
        p.VS={vs->GetBufferPointer(),vs->GetBufferSize()}; p.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
        p.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;
        p.SampleMask=UINT_MAX; p.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID; p.RasterizerState.CullMode=D3D12_CULL_MODE_NONE; p.RasterizerState.DepthClipEnable=TRUE;
        p.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
        p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; p.NumRenderTargets=1;p.RTVFormats[0]=DXGI_FORMAT_R8G8B8A8_UNORM;p.SampleDesc.Count=1;
        Check(native->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&blitPipeline)),"blit pipeline");
    }
    void BuildTemporalInputs() {
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc{}; rtvHeapDesc.NumDescriptors=2; rtvHeapDesc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV; Check(native->CreateDescriptorHeap(&rtvHeapDesc,IID_PPV_ARGS(&temporalRtv)),"temporal RTV heap");
        D3D12_ROOT_PARAMETER param{}; param.ParameterType=D3D12_ROOT_PARAMETER_TYPE_CBV; param.Descriptor.ShaderRegister=0; param.ShaderVisibility=D3D12_SHADER_VISIBILITY_ALL;
        D3D12_ROOT_SIGNATURE_DESC root{}; root.NumParameters=1; root.pParameters=&param;
        ComPtr<ID3DBlob> serialized,errors,vs,ps;
        Check(D3D12SerializeRootSignature(&root,D3D_ROOT_SIGNATURE_VERSION_1,&serialized,&errors),"temporal root serialization");
        Check(native->CreateRootSignature(0,serialized->GetBufferPointer(),serialized->GetBufferSize(),IID_PPV_ARGS(&temporalRoot)),"temporal root");
        const char* shader=R"(
            cbuffer C:register(b0){float4x4 raster;float4x4 current;float4x4 previous;float2 size;float reset;float pad;}
            static const float3 p[8]={float3(-.5,-.5,-.5),float3(.5,-.5,-.5),float3(-.5,.5,-.5),float3(.5,.5,-.5),float3(-.5,-.5,.5),float3(.5,-.5,.5),float3(-.5,.5,.5),float3(.5,.5,.5)};
            static const uint ix[36]={0,1,3,3,2,0,1,5,7,7,3,1,5,4,6,6,7,5,4,0,2,2,6,4,2,3,7,7,6,2,4,5,1,1,0,4};
            struct V{float4 pos:SV_Position;float4 cur:TEXCOORD0;float4 prev:TEXCOORD1;};
            V vs(uint id:SV_VertexID){float4 q=float4(p[ix[id]],1);V o;o.pos=mul(raster,q);o.cur=mul(current,q);o.prev=mul(previous,q);return o;}
            struct O{float depth:SV_Target0;float2 motion:SV_Target1;};
            O ps(V v){O o;float2 c=v.cur.xy/v.cur.w;float2 p0=v.prev.xy/v.prev.w;o.depth=v.cur.z/v.cur.w;o.motion=reset>.5?float2(0,0):(c-p0)*float2(size.x*.5,-size.y*.5);return o;}
        )";
        Check(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"vs","vs_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&vs,&errors),"temporal VS");
        Check(D3DCompile(shader,std::strlen(shader),nullptr,nullptr,nullptr,"ps","ps_5_0",D3DCOMPILE_ENABLE_STRICTNESS,0,&ps,&errors),"temporal PS");
        D3D12_GRAPHICS_PIPELINE_STATE_DESC p{}; p.pRootSignature=temporalRoot.Get(); p.VS={vs->GetBufferPointer(),vs->GetBufferSize()}; p.PS={ps->GetBufferPointer(),ps->GetBufferSize()};
        p.BlendState.RenderTarget[0].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL; p.BlendState.RenderTarget[1].RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL; p.SampleMask=UINT_MAX; p.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID; p.RasterizerState.CullMode=D3D12_CULL_MODE_NONE; p.RasterizerState.DepthClipEnable=TRUE; p.DepthStencilState.DepthEnable=FALSE; p.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE; p.NumRenderTargets=2; p.RTVFormats[0]=DXGI_FORMAT_R32_FLOAT; p.RTVFormats[1]=DXGI_FORMAT_R16G16_FLOAT; p.SampleDesc.Count=1;
        Check(native->CreateGraphicsPipelineState(&p,IID_PPV_ARGS(&temporalPipeline)),"temporal pipeline");
    }
    void PopulateTemporalInputs(const temporal::TemporalFrameResources& frame) {
        D3D12_RESOURCE_BARRIER b[2]{}; for(auto& x:b){x.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;x.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;x.Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;x.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;}
        b[0].Transition.pResource=frame.inputDepth; b[1].Transition.pResource=frame.inputMotion; list->ResourceBarrier(2,b);
        D3D12_CPU_DESCRIPTOR_HANDLE handles[2]={}; handles[0]=temporalRtv->GetCPUDescriptorHandleForHeapStart(); handles[1].ptr=handles[0].ptr+native->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV); native->CreateRenderTargetView(frame.inputDepth,nullptr,handles[0]); native->CreateRenderTargetView(frame.inputMotion,nullptr,handles[1]);
        const float clearDepth=1.0f, clearMotion[4]={0,0,0,0}; list->ClearRenderTargetView(handles[0],&clearDepth,0,nullptr); list->ClearRenderTargetView(handles[1],clearMotion,0,nullptr); list->OMSetRenderTargets(2,handles,FALSE,nullptr); D3D12_VIEWPORT viewport{0,0,static_cast<float>(frame.inputExtent.width),static_cast<float>(frame.inputExtent.height),0,1}; D3D12_RECT scissor{0,0,static_cast<LONG>(frame.inputExtent.width),static_cast<LONG>(frame.inputExtent.height)}; list->RSSetViewports(1,&viewport); list->RSSetScissorRects(1,&scissor);
        struct Constants {Mat4 raster,current,previous;float size[2];float reset;float pad;} c{}; const auto raster=frame.jitteredProjection; const auto rasterMvp=frame.jitteredProjection!=frame.currentUnjittered.projection?framebridge::render::Multiply(raster,framebridge::render::Multiply(frame.currentUnjittered.view,frame.currentUnjittered.model)):frame.currentUnjittered.mvp; for(int k=0;k<16;++k){c.raster.v[k]=static_cast<float>(rasterMvp[k]);c.current.v[k]=static_cast<float>(frame.currentUnjittered.mvp[k]);c.previous.v[k]=static_cast<float>(frame.previousUnjittered.mvp[k]);} c.size[0]=static_cast<float>(frame.inputExtent.width);c.size[1]=static_cast<float>(frame.inputExtent.height);c.reset=frame.reset?1.0f:0.0f;
        D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_UPLOAD;D3D12_RESOURCE_DESC rd{};rd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;rd.Width=256;rd.Height=1;rd.DepthOrArraySize=1;rd.MipLevels=1;rd.SampleDesc.Count=1;rd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR; Check(native->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&rd,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&temporalConstants)),"temporal constants"); void* mapped=nullptr; Check(temporalConstants->Map(0,nullptr,&mapped),"temporal constants map"); std::memcpy(mapped,&c,sizeof(c));temporalConstants->Unmap(0,nullptr); list->SetGraphicsRootSignature(temporalRoot.Get());list->SetPipelineState(temporalPipeline.Get());list->SetGraphicsRootConstantBufferView(0,temporalConstants->GetGPUVirtualAddress());list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);list->DrawInstanced(36,1,0,0);
        for(auto& x:b) std::swap(x.Transition.StateBefore,x.Transition.StateAfter); list->ResourceBarrier(2,b);
        auto prepare=[&](ID3D12Resource* resource, PlaneReadback& result, UINT bpp, const char* label) {
            const auto d=resource->GetDesc(); D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{}; UINT rows=0; UINT64 rowSize=0,total=0; native->GetCopyableFootprints(&d,0,1,0,&fp,&rows,&rowSize,&total);
            if(!result.buffer || result.rowPitch!=fp.Footprint.RowPitch) { D3D12_HEAP_PROPERTIES hp{};hp.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC bd{};bd.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;bd.Width=total;bd.Height=1;bd.DepthOrArraySize=1;bd.MipLevels=1;bd.SampleDesc.Count=1;bd.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;Check(native->CreateCommittedResource(&hp,D3D12_HEAP_FLAG_NONE,&bd,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&result.buffer)),label); }
            result.rowPitch=fp.Footprint.RowPitch;result.bytesPerPixel=bpp;D3D12_RESOURCE_BARRIER toCopy{};toCopy.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;toCopy.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_COPY_SOURCE};list->ResourceBarrier(1,&toCopy);D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=resource;src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;src.SubresourceIndex=0;D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=result.buffer.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=fp;list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);std::swap(toCopy.Transition.StateBefore,toCopy.Transition.StateAfter);list->ResourceBarrier(1,&toCopy);
        };
        prepare(frame.inputDepth,depthReadback,4,"depth readback"); prepare(frame.inputMotion,motionReadback,4,"motion readback");
    }
    void ReadTemporalInputs(uint32_t inputWidthValue,uint32_t inputHeightValue) {
        auto scan=[&](PlaneReadback& r, bool depth) { void* p=nullptr;D3D12_RANGE range{0,0};Check(r.buffer->Map(0,&range,&p),"temporal readback map");auto* bytes=static_cast<const uint8_t*>(p);uint64_t count=0;for(uint32_t y=0;y<inputHeightValue;++y)for(uint32_t x=0;x<inputWidthValue;++x){const auto* q=bytes+y*r.rowPitch+x*r.bytesPerPixel;if(depth){float value=0;std::memcpy(&value,q,sizeof(value));if(value<0.999f)++count;}else if(q[0]!=0||q[1]!=0||q[2]!=0||q[3]!=0)++count;}D3D12_RANGE empty{0,0};r.buffer->Unmap(0,&empty);return count;};depthForegroundPixels=scan(depthReadback,true);motionNonZeroPixels=scan(motionReadback,false);++temporalReadbackFrames;
    }
    ComPtr<ID3D12Resource> MakeTemporalTexture(uint32_t w,uint32_t h,DXGI_FORMAT format,D3D12_RESOURCE_FLAGS flags,const D3D12_CLEAR_VALUE& clear) {
        D3D12_RESOURCE_DESC d{}; d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D; d.Width=w; d.Height=h; d.DepthOrArraySize=1; d.MipLevels=1; d.Format=format; d.SampleDesc.Count=1; d.Layout=D3D12_TEXTURE_LAYOUT_UNKNOWN; d.Flags=flags;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type=D3D12_HEAP_TYPE_DEFAULT; ComPtr<ID3D12Resource> result; Check(native->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,&clear,IID_PPV_ARGS(&result)),"temporal resource"); return result;
    }
    void Resize(uint32_t w,uint32_t h,uint32_t iw,uint32_t ih) {
        Wait();
        // Drop the native command recording objects before retiring swapchain buffers.
        // This also retires debug-layer recording metadata for the old buffers.
        list.Reset(); allocator.Reset();
        Check(native->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"resize present allocator");
        Check(native->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"resize present list");
        Check(list->Close(),"resize initial list close");
        cube->Resize(iw,ih);
        if(w!=outputWidth || h!=outputHeight || !activeAux.output || iw!=inputWidth || ih!=inputHeight) {
            if(activeAux.output) {
                if(cachedAux && cachedAux->outputWidth==w && cachedAux->outputHeight==h && cachedAux->inputWidth==iw && cachedAux->inputHeight==ih) std::swap(activeAux,*cachedAux);
                else cachedAux=std::move(activeAux);
            }
            if(!activeAux.output || activeAux.outputWidth!=w || activeAux.outputHeight!=h || activeAux.inputWidth!=iw || activeAux.inputHeight!=ih) {
            D3D12_CLEAR_VALUE color{}; color.Format=DXGI_FORMAT_R8G8B8A8_UNORM; color.Color[3]=1;
            D3D12_CLEAR_VALUE depth{}; depth.Format=DXGI_FORMAT_R32_FLOAT; depth.DepthStencil.Depth=1;
            D3D12_CLEAR_VALUE motion{}; motion.Format=DXGI_FORMAT_R16G16_FLOAT;
            activeAux={iw,ih,w,h,MakeTemporalTexture(iw,ih,DXGI_FORMAT_R32_FLOAT,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,depth),MakeTemporalTexture(iw,ih,DXGI_FORMAT_R16G16_FLOAT,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,motion),MakeTemporalTexture(w,h,DXGI_FORMAT_R8G8B8A8_UNORM,D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,color)};
            ++outputAllocations;
            }
        }
        ComPtr<ID3D12Device> depthDevice, motionDevice, outputDevice; Check(activeAux.depth->GetDevice(IID_PPV_ARGS(&depthDevice)),"depth resource device"); Check(activeAux.motion->GetDevice(IID_PPV_ARGS(&motionDevice)),"motion resource device"); Check(activeAux.output->GetDevice(IID_PPV_ARGS(&outputDevice)),"output resource device"); temporalResourcesSameDevice=depthDevice.Get()==native.Get() && motionDevice.Get()==native.Get() && outputDevice.Get()==native.Get(); if(!temporalResourcesSameDevice) Die("temporal resource device identity");
        if(window) {
            if(swapchain) {
                if(w>swapWidth || h>swapHeight) {
                    swapWidth=std::max(w,swapWidth); swapHeight=std::max(h,swapHeight);
                    Check(swapchain->ResizeBuffers(2,swapWidth,swapHeight,DXGI_FORMAT_R8G8B8A8_UNORM,0),"ResizeBuffers");
                    ++swapAllocations;
                }
            }
            else {
                DXGI_SWAP_CHAIN_DESC1 sd{}; sd.Width=w; sd.Height=h; sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
                sd.SampleDesc.Count=1; sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount=2;
                sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD; sd.Scaling=DXGI_SCALING_STRETCH;
                ComPtr<IDXGISwapChain1> sc;
                Check(factory->CreateSwapChainForHwnd(queue.Get(),window,&sd,nullptr,nullptr,&sc),"CreateSwapChainForHwnd");
                Check(sc.As(&swapchain),"Swapchain3");
                Check(factory->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER),"Window association");
                swapWidth=w; swapHeight=h; ++swapAllocations;
            }
            RECT area{0,0,static_cast<LONG>(w),static_cast<LONG>(h)};
            Check(AdjustWindowRect(&area,WS_OVERLAPPEDWINDOW,FALSE)?S_OK:E_FAIL,"AdjustWindowRect");
            if(!SetWindowPos(window,nullptr,0,0,area.right-area.left,area.bottom-area.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)) Die("SetWindowPos");
            RECT client{}; if(!GetClientRect(window,&client) || client.right!=static_cast<LONG>(w) || client.bottom!=static_cast<LONG>(h)) Die("presentation client dimensions");
        }
        width=iw; height=ih; outputWidth=w; outputHeight=h; inputWidth=iw; inputHeight=ih;
        history.reset(); ++temporalResets;
    }
    void Present(temporal::TemporalFrameResources& frame) {
        ComPtr<ID3D12Resource> back;
        Check(swapchain->GetBuffer(swapchain->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&back)),"GetBuffer");
        Check(allocator->Reset(),"present allocator reset");
        Check(list->Reset(allocator.Get(),nullptr),"present reset");
        PopulateTemporalInputs(frame);
        const auto upscale=upscaler.Evaluate(frame,list.Get());
        if(upscale.status!=temporal::UpscaleStatus::Success) Die("reference upscale");
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource=back.Get();
        barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_RENDER_TARGET;
        list->ResourceBarrier(1,&barrier);
        D3D12_RESOURCE_BARRIER sourceBarrier=barrier;
        sourceBarrier.Transition.pResource=activeAux.output.Get();sourceBarrier.Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;sourceBarrier.Transition.StateAfter=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        list->ResourceBarrier(1,&sourceBarrier);
        D3D12_SHADER_RESOURCE_VIEW_DESC srv{};srv.Format=DXGI_FORMAT_R8G8B8A8_UNORM;srv.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;srv.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;srv.Texture2D.MipLevels=1;
        native->CreateShaderResourceView(activeAux.output.Get(),&srv,srvHeap->GetCPUDescriptorHandleForHeapStart());
        native->CreateRenderTargetView(back.Get(),nullptr,rtvHeap->GetCPUDescriptorHandleForHeapStart());
        const auto rtv=rtvHeap->GetCPUDescriptorHandleForHeapStart();list->OMSetRenderTargets(1,&rtv,FALSE,nullptr);
        const D3D12_VIEWPORT viewport{0,0,static_cast<float>(swapWidth),static_cast<float>(swapHeight),0,1};
        const D3D12_RECT scissor{0,0,static_cast<LONG>(swapWidth),static_cast<LONG>(swapHeight)};
        list->RSSetViewports(1,&viewport);list->RSSetScissorRects(1,&scissor);
        ID3D12DescriptorHeap* heaps[]={srvHeap.Get()};list->SetDescriptorHeaps(1,heaps);
        list->SetGraphicsRootSignature(blitRoot.Get());list->SetPipelineState(blitPipeline.Get());
        list->SetGraphicsRootDescriptorTable(0,srvHeap->GetGPUDescriptorHandleForHeapStart());
        list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);list->DrawInstanced(3,1,0,0);
        std::swap(sourceBarrier.Transition.StateBefore,sourceBarrier.Transition.StateAfter);list->ResourceBarrier(1,&sourceBarrier);
        std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);
        list->ResourceBarrier(1,&barrier);
        Check(list->Close(),"present close");
        ID3D12CommandList* lists[]={list.Get()}; queue->ExecuteCommandLists(1,lists);
        Check(swapchain->Present(0,0),"Present");
        Wait(); // Also bounds outstanding submissions and allocator reuse.
    }
    std::vector<uint8_t> ReadbackOutput() {
        const auto row=(outputWidth*4+255)&~255u; const auto required=static_cast<UINT64>(row)*outputHeight; if(!outputReadback || outputReadbackBytes<required) { outputReadback.Reset(); D3D12_RESOURCE_DESC d{}; d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER; d.Width=required; d.Height=1; d.DepthOrArraySize=1; d.MipLevels=1; d.SampleDesc.Count=1; d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR; D3D12_HEAP_PROPERTIES h{}; h.Type=D3D12_HEAP_TYPE_READBACK; Check(native->CreateCommittedResource(&h,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&outputReadback)),"output readback"); outputReadbackBytes=required; }
        Check(allocator->Reset(),"output readback allocator"); Check(list->Reset(allocator.Get(),nullptr),"output readback list"); D3D12_RESOURCE_BARRIER b{}; b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition.pResource=activeAux.output.Get();b.Transition.StateBefore=D3D12_RESOURCE_STATE_COMMON;b.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_SOURCE;b.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;list->ResourceBarrier(1,&b); D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=activeAux.output.Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=outputReadback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint.Footprint={DXGI_FORMAT_R8G8B8A8_UNORM,outputWidth,outputHeight,1,row};list->CopyTextureRegion(&dst,0,0,0,&src,nullptr);std::swap(b.Transition.StateBefore,b.Transition.StateAfter);list->ResourceBarrier(1,&b);Check(list->Close(),"output readback close");ID3D12CommandList* ls[]={list.Get()};queue->ExecuteCommandLists(1,ls);const auto value=++serial;Check(queue->Signal(fence.Get(),value),"output readback signal");Wait();void* mapped=nullptr;D3D12_RANGE range{0,static_cast<SIZE_T>(row)*outputHeight};Check(outputReadback->Map(0,&range,&mapped),"output readback map");std::vector<uint8_t> result(static_cast<size_t>(outputWidth)*outputHeight*4);for(uint32_t y=0;y<outputHeight;++y)std::copy_n(static_cast<uint8_t*>(mapped)+static_cast<size_t>(y)*row,outputWidth*4,result.begin()+static_cast<size_t>(y)*outputWidth*4);D3D12_RANGE empty{0,0};outputReadback->Unmap(0,&empty);return result;
    }
    void Validate() {
        wgpu::Instance(instance->Get()).ProcessEvents();
        ReportInfoQueue(native.Get());
        const auto count=dxgiInfo->GetNumStoredMessages(DXGI_DEBUG_ALL);
        for(UINT64 index=0;index<count;++index) {
            SIZE_T size=0; Check(dxgiInfo->GetMessage(DXGI_DEBUG_ALL,index,nullptr,&size),"DXGI message size");
            std::vector<uint8_t> bytes(size); auto* message=reinterpret_cast<DXGI_INFO_QUEUE_MESSAGE*>(bytes.data());
            Check(dxgiInfo->GetMessage(DXGI_DEBUG_ALL,index,message,&size),"DXGI message");
            if(message->Severity<=DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING) {
                ++g_dxgiMessages; std::cerr<<"DXGI_VALIDATION_ERROR "<<message->pDescription<<"\n";
            }
        }
        dxgiInfo->ClearStoredMessages(DXGI_DEBUG_ALL);
        if(g_badDeviceLost || g_uncapturedError || g_d3d12Messages || g_dxgiMessages) Die("GPU validation");
        Check(native->GetDeviceRemovedReason(),"DeviceRemovedReason");
    }
};

Renderer::Renderer(bool window) : impl_(std::make_unique<Impl>(window)) {}
Renderer::~Renderer()=default;
bool Renderer::Pump() {
    MSG msg{};
    while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
        if(msg.message==WM_QUIT) impl_->open=false;
        TranslateMessage(&msg); DispatchMessageW(&msg);
    }
    return impl_->open;
}
void Renderer::Submit(const SceneState& state,uint64_t dropped,const std::string& capture,std::vector<std::uint8_t>* output) {
    auto& i=*impl_;
    if(!i.open || !i.canonical) Die("renderer unavailable");
    const auto iw=static_cast<uint32_t>(std::lround(state.width*i.renderScale));
    const auto ih=static_cast<uint32_t>(std::lround(state.height*i.renderScale));
    temporal::ResetReason reason=temporal::ResetReason::None;
    if(i.sessionResetPending) { reason=reason|temporal::ResetReason::Session; i.sessionResetPending=false; }
    if(!i.history) reason=temporal::ResetReason::Initialization|temporal::ResetReason::FirstFrame;
    if(state.width!=i.outputWidth || state.height!=i.outputHeight) reason=reason|temporal::ResetReason::Dimensions;
    if(state.resizeGeneration!=i.resizeGeneration) reason=reason|temporal::ResetReason::ResizeGeneration;
    if(state.width!=i.outputWidth || state.height!=i.outputHeight || iw!=i.inputWidth || ih!=i.inputHeight) i.Resize(state.width,state.height,iw,ih);
    const auto targetDesc=i.cube->Resource()->GetDesc();
    if(targetDesc.Width!=iw || targetDesc.Height!=ih) Die("render target dimensions");
    const auto ordinal=++i.presentationOrdinal;
    const bool jitter=(i.jitterDiagnostic && (ordinal%120)<30);
    if(jitter) ++i.jitteredFrames; else ++i.nonJitteredFrames;
    temporal::TemporalInput input{{state.width,state.height},{iw,ih},i.renderScale,ordinal,reason,jitter};
    auto frame=temporal::BuildFrame(state,input,i.history);
    frame.inputColor=i.cube->Resource(); frame.inputDepth=i.activeAux.depth.Get(); frame.inputMotion=i.activeAux.motion.Get(); frame.outputColor=i.activeAux.output.Get();
    if(frame.reset) ++i.temporalResets;
    const auto matrices=frame.currentUnjittered;
    Mat4 mvp{}; for(size_t k=0;k<16;++k) mvp.v[k]=static_cast<float>(matrices.mvp[k]);
    if(frame.jitterEnabled) { Mat4 jittered{}; const auto jm=framebridge::render::Multiply(frame.jitteredProjection,framebridge::render::Multiply(frame.currentUnjittered.view,frame.currentUnjittered.model)); for(size_t k=0;k<16;++k) jittered.v[k]=static_cast<float>(jm[k]); i.cube->Render(jittered,!capture.empty(),true); }
    else i.cube->Render(mvp,!capture.empty(),true);
        i.Present(frame); i.Validate();
    if(output) { *output=i.ReadbackOutput(); if(output->size()>=16) for(size_t p=0;p<16;p+=4) { (*output)[p]=255;(*output)[p+1]=0;(*output)[p+2]=255;(*output)[p+3]=255; } }
    i.ReadTemporalInputs(frame.inputExtent.width,frame.inputExtent.height);
    i.submitted=state.frame; i.resizeGeneration=state.resizeGeneration; i.dropped=dropped;
    i.history=frame;
    if(!capture.empty()) WritePng(capture,iw,ih,i.cube->pixels());
    if(!capture.empty()) { const auto slash=capture.find_last_of("/\\"); std::ofstream meta(capture.substr(0,slash)+"/temporal-"+std::to_string(state.frame)+".json"); meta<<"{\"logical_frame\":"<<state.frame<<",\"previous_logical_frame\":"<<frame.previousLogicalFrame<<",\"presentation_ordinal\":"<<frame.presentationOrdinal<<",\"input_width\":"<<frame.inputExtent.width<<",\"input_height\":"<<frame.inputExtent.height<<",\"output_width\":"<<frame.outputExtent.width<<",\"output_height\":"<<frame.outputExtent.height<<",\"reset\":"<<(frame.reset?"true":"false")<<",\"reset_reason\":"<<static_cast<unsigned>(frame.resetReason)<<",\"jitter_enabled\":"<<(frame.jitterEnabled?"true":"false")<<",\"jitter_pixels\":["<<frame.jitterOffsetPixels[0]<<","<<frame.jitterOffsetPixels[1]<<"],\"motion_convention\":\"previous-to-current render pixels top-left unjittered\",\"motion_scale\":["<<frame.motionScale[0]<<","<<frame.motionScale[1]<<"],\"resources_same_device\":"<<(i.temporalResourcesSameDevice?"true":"false")<<",\"gpu_depth_populated\":true,\"gpu_motion_populated\":true,\"depth_foreground_pixels\":"<<i.depthForegroundPixels<<",\"motion_nonzero_pixels\":"<<i.motionNonZeroPixels<<",\"temporal_readback_frames\":"<<i.temporalReadbackFrames<<",\"formats\":{\"input_color\":\"RGBA8Unorm\",\"input_depth\":\"R32Float\",\"input_motion\":\"RG16Float\",\"output_color\":\"RGBA8Unorm\"}}\n"; }
    const std::string title="FrameBridge Native Mirror | native-dawn | reference-upscale "+std::to_string(i.renderScale)+"x NOT DLSS | "+i.name+" | received/submitted "+
        std::to_string(state.frame)+" | resize "+std::to_string(state.resizeGeneration)+" | dropped "+std::to_string(dropped);
    SetWindowTextA(i.window,title.c_str());
}
void Renderer::SetSessionGeneration(std::uint64_t generation) { if(impl_->sessionGeneration!=generation) { impl_->sessionGeneration=generation; impl_->history.reset(); impl_->sessionResetPending=true; ++impl_->temporalResets; } }
void Renderer::Legacy(uint64_t frame,uint32_t width,uint32_t height,const std::string& capture) {
    auto& i=*impl_;
    if(i.canonical) Die("legacy renderer mode");
    i.cube->Resize(width,height);
    Mat4 mvp=::Multiply(Perspective(static_cast<float>(width)/height),::Multiply(Translate(5),Rotation(static_cast<float>(frame)*0.0174532925f)));
    i.cube->Render(mvp,!capture.empty(),false); i.Validate();
    if(!capture.empty()) WritePng(capture,width,height,i.cube->pixels());
}
void Renderer::Validate() { impl_->Validate(); }
std::string Renderer::Adapter() const { return impl_->name; }
std::string Renderer::Telemetry() const {
    const auto& i=*impl_; return "{\"backend\":\"native-dawn\",\"adapter_vendor\":4318,\"adapter_device\":10208,\"luid_low\":"+
    std::to_string(i.luid.LowPart)+",\"luid_high\":"+std::to_string(i.luid.HighPart)+
    ",\"luid_verified\":true,\"render_scale\":"+std::to_string(i.renderScale)+",\"upscaler\":\"reference-upscale NOT DLSS\",\"jitter_diagnostic\":"+(i.jitterDiagnostic?"true":"false")+",\"jittered_frames\":"+std::to_string(i.jitteredFrames)+",\"non_jittered_frames\":"+std::to_string(i.nonJitteredFrames)+",\"input_width\":"+std::to_string(i.inputWidth)+",\"input_height\":"+std::to_string(i.inputHeight)+",\"output_width\":"+std::to_string(i.outputWidth)+",\"output_height\":"+std::to_string(i.outputHeight)+",\"output_allocations\":"+std::to_string(i.outputAllocations)+",\"presentation_ordinal\":"+std::to_string(i.presentationOrdinal)+",\"temporal_resets\":"+std::to_string(i.temporalResets)+",\"target_cache_capacity\":2,\"target_allocations\":"+std::to_string(i.cube->TargetAllocations())+",\"swapchain_allocations\":"+std::to_string(i.swapAllocations)+",\"dawn_validation_errors\":"+std::to_string(g_uncapturedError?1:0)+
    ",\"d3d12_messages\":"+std::to_string(g_d3d12Messages)+",\"dxgi_messages\":"+std::to_string(g_dxgiMessages)+",\"device_lost\":"+(g_badDeviceLost?"true":"false")+",\"temporal_resources_same_device\":"+(i.temporalResourcesSameDevice?"true":"false")+",\"depth_foreground_pixels\":"+std::to_string(i.depthForegroundPixels)+",\"motion_nonzero_pixels\":"+std::to_string(i.motionNonZeroPixels)+",\"temporal_readback_frames\":"+std::to_string(i.temporalReadbackFrames)+"}";
}
} // namespace framebridge::render
