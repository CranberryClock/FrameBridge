#include <windows.h>
#include <dxgi1_6.h>
#include <atomic>
#include <stdexcept>
#include "renderer.h"
#include <d3d12.h>
#include <wrl/client.h>

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
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

void Die(const char* operation, HRESULT hr = S_OK) {
    throw std::runtime_error(std::string(operation) + " HRESULT=" + std::to_string(static_cast<unsigned long>(hr)));
}
void Check(HRESULT hr, const char* operation) { if (FAILED(hr)) Die(operation, hr); }
void CheckStatus(wgpu::Status status, const char* operation) {
    if (status != wgpu::Status::Success) Die(operation);
}
std::string ToString(wgpu::StringView s) { return std::string(s.data, s.length); }

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
        texture_ = {}; memory_ = {}; nativeTexture_.Reset();
        width_ = width; height_ = height; depth_ = {}; readback_ = {}; textureInitialized_ = false;
        D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; desc.Width = width; desc.Height = height; desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT; D3D12_CLEAR_VALUE clear{}; clear.Format = desc.Format; clear.Color[3] = 1;
        Check(d3d_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, &clear, IID_PPV_ARGS(&nativeTexture_)), "CreateCommittedResource(color)");
        dawn::native::d3d12::SharedTextureMemoryD3D12ResourceDescriptor importDesc; importDesc.resource = nativeTexture_;
        wgpu::SharedTextureMemoryDescriptor md; md.nextInChain = &importDesc; md.label = "FrameBridge native cube color"; memory_ = device_.ImportSharedTextureMemory(&md); texture_ = memory_.CreateTexture();
        wgpu::TextureDescriptor depthDesc; depthDesc.dimension = wgpu::TextureDimension::e2D; depthDesc.size = {width, height, 1}; depthDesc.format = wgpu::TextureFormat::Depth24Plus; depthDesc.usage = wgpu::TextureUsage::RenderAttachment; depth_ = device_.CreateTexture(&depthDesc);
        readback_.rowPitch = (width * 4 + 255) & ~255u; D3D12_RESOURCE_DESC rb{}; rb.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rb.Width = static_cast<uint64_t>(readback_.rowPitch) * height; rb.Height = 1; rb.DepthOrArraySize = 1; rb.MipLevels = 1; rb.SampleDesc.Count = 1; rb.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; D3D12_HEAP_PROPERTIES readHeap{}; readHeap.Type = D3D12_HEAP_TYPE_READBACK; Check(d3d_->CreateCommittedResource(&readHeap, D3D12_HEAP_FLAG_NONE, &rb, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback_.buffer)), "CreateCommittedResource(readback)");
    }
    void Render(const Mat4& mvp, bool capture, bool canonical) {
        wgpu::SharedTextureMemoryBeginAccessDescriptor begin{}; begin.initialized = textureInitialized_; CheckStatus(memory_.BeginAccess(texture_, &begin), "BeginAccess(cube)");
        queueWeb_.WriteBuffer(uniform_, 0, mvp.v, sizeof(mvp.v));
        wgpu::RenderPassColorAttachment color; color.view = texture_.CreateView(); color.loadOp = wgpu::LoadOp::Clear; color.storeOp = wgpu::StoreOp::Store; color.clearValue = canonical ? wgpu::Color{8.0/255,11.0/255,18.0/255,1} : wgpu::Color{0.03,0.04,0.07,1};
        wgpu::RenderPassDepthStencilAttachment depth; depth.view = depth_.CreateView(); depth.depthLoadOp = wgpu::LoadOp::Clear; depth.depthStoreOp = wgpu::StoreOp::Store; depth.depthClearValue = 1;
        wgpu::RenderPassDescriptor pd; pd.colorAttachmentCount = 1; pd.colorAttachments = &color; pd.depthStencilAttachment = &depth; wgpu::CommandEncoder enc = device_.CreateCommandEncoder(); wgpu::RenderPassEncoder pass = enc.BeginRenderPass(&pd); pass.SetPipeline(pipeline_); pass.SetBindGroup(0, bindGroup_); pass.SetVertexBuffer(0, vertex_); pass.SetIndexBuffer(index_, wgpu::IndexFormat::Uint16); pass.DrawIndexed(36); pass.End(); wgpu::CommandBuffer cb = enc.Finish(); queueWeb_.Submit(1, &cb);
        wgpu::SharedTextureMemoryEndAccessState end{}; CheckStatus(memory_.EndAccess(texture_, &end), "EndAccess(cube)"); textureInitialized_ = true;
        uint64_t value = (serial_ += 2); Check(queue_->Signal(fence_.Get(), value), "Signal(Dawn cube boundary)"); Wait(value);
        if (capture) Capture(value);
    }
    ID3D12Resource* Resource() const { return nativeTexture_.Get(); }
    uint32_t width() const { return width_; } uint32_t height() const { return height_; }
    const std::vector<uint8_t>& pixels() const { return readback_.rgba; }
  private:
    wgpu::Buffer MakeBuffer(uint64_t size, wgpu::BufferUsage usage) { wgpu::BufferDescriptor bd; bd.size = size; bd.usage = usage; return device_.CreateBuffer(&bd); }
    void BuildPipeline(bool canonical) {
        wgpu::BindGroupLayoutEntry be; be.binding = 0; be.visibility = wgpu::ShaderStage::Vertex; be.buffer.type = wgpu::BufferBindingType::Uniform;
        wgpu::BindGroupLayoutDescriptor bld; bld.entryCount = 1; bld.entries = &be; bindLayout_ = device_.CreateBindGroupLayout(&bld); wgpu::PipelineLayoutDescriptor pld; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bindLayout_; wgpu::PipelineLayout layout = device_.CreatePipelineLayout(&pld);
        const char* source = "struct U { mvp: mat4x4f }; @group(0) @binding(0) var<uniform> u: U; struct V { @location(0) pos: vec3f, @location(1) color: vec3f }; struct O { @builtin(position) pos: vec4f, @location(0) color: vec3f }; @vertex fn vs(v:V)->O { var o:O; o.pos=u.mvp*vec4f(v.pos,1); o.color=v.color; return o; } @fragment fn fs(o:O)->@location(0) vec4f { return vec4f(o.color,1); }";
        std::string shader(source); if(canonical) { const auto pos=shader.find("vec4f(o.color,1)"); shader.replace(pos,16,"vec4f(54.0/255.0,214.0/255.0,1,1)"); }
        wgpu::ShaderSourceWGSL wgsl; wgsl.code = shader.c_str(); wgpu::ShaderModuleDescriptor sd; sd.nextInChain = &wgsl; wgpu::ShaderModule sm = device_.CreateShaderModule(&sd); wgpu::VertexAttribute attrs[2]; attrs[0].format = wgpu::VertexFormat::Float32x3; attrs[0].offset = 0; attrs[0].shaderLocation = 0; attrs[1].format = wgpu::VertexFormat::Float32x3; attrs[1].offset = 12; attrs[1].shaderLocation = 1; wgpu::VertexBufferLayout vb; vb.arrayStride = 24; vb.attributeCount = 2; vb.attributes = attrs; wgpu::ColorTargetState ct; ct.format = wgpu::TextureFormat::RGBA8Unorm; wgpu::FragmentState fs; fs.module = sm; fs.entryPoint = "fs"; fs.targetCount = 1; fs.targets = &ct; wgpu::DepthStencilState ds; ds.format = wgpu::TextureFormat::Depth24Plus; ds.depthWriteEnabled = true; ds.depthCompare = wgpu::CompareFunction::Less; wgpu::RenderPipelineDescriptor rp; rp.layout = layout; rp.vertex.module = sm; rp.vertex.entryPoint = "vs"; rp.vertex.bufferCount = 1; rp.vertex.buffers = &vb; rp.fragment = &fs; rp.depthStencil = &ds; pipeline_ = device_.CreateRenderPipeline(&rp);
    }
    void Wait(uint64_t value) { if (fence_->GetCompletedValue() < value) { Check(fence_->SetEventOnCompletion(value, event_), "SetEventOnCompletion"); if (WaitForSingleObject(event_, 10000) != WAIT_OBJECT_0) Die("GPU fence timeout"); } }
    void Capture(uint64_t value) { Check(allocator_->Reset(), "Reset(capture allocator)"); if (!list_) { Check(d3d_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr, IID_PPV_ARGS(&list_)), "Create(capture list)"); Check(list_->Close(), "Close(initial capture list)"); } Check(list_->Reset(allocator_.Get(), nullptr), "Reset(capture list)"); D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = nativeTexture_.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = 0; D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = readback_.buffer.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM; dst.PlacedFootprint.Footprint.Width = width_; dst.PlacedFootprint.Footprint.Height = height_; dst.PlacedFootprint.Footprint.Depth = 1; dst.PlacedFootprint.Footprint.RowPitch = readback_.rowPitch; list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr); Check(list_->Close(), "Close(capture list)"); ID3D12CommandList* lists[] = {list_.Get()}; queue_->ExecuteCommandLists(1, lists); uint64_t copyValue = value + 1; Check(queue_->Signal(fence_.Get(), copyValue), "Signal(capture)"); Wait(copyValue); void* mapped = nullptr; D3D12_RANGE range{0, static_cast<SIZE_T>(readback_.rowPitch) * height_}; Check(readback_.buffer->Map(0, &range, &mapped), "Map(capture)"); readback_.rgba.resize(static_cast<size_t>(width_) * height_ * 4); for (uint32_t y = 0; y < height_; ++y) std::copy_n(static_cast<uint8_t*>(mapped) + y * readback_.rowPitch, width_ * 4, readback_.rgba.begin() + y * width_ * 4); D3D12_RANGE written{0,0}; readback_.buffer->Unmap(0, &written); }
    wgpu::Device device_; wgpu::Queue queueWeb_; ComPtr<ID3D12Device> d3d_; ComPtr<ID3D12CommandQueue> queue_; ComPtr<ID3D12Resource> nativeTexture_; wgpu::SharedTextureMemory memory_; wgpu::Texture texture_, depth_; wgpu::Buffer vertex_, index_, uniform_; wgpu::BindGroupLayout bindLayout_; wgpu::BindGroup bindGroup_; wgpu::RenderPipeline pipeline_; uint32_t width_ = 0, height_ = 0; bool textureInitialized_ = false; Readback readback_; ComPtr<ID3D12CommandAllocator> allocator_ = nullptr; ComPtr<ID3D12GraphicsCommandList> list_; ComPtr<ID3D12Fence> fence_; HANDLE event_ = nullptr; uint64_t serial_ = 0;
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
    ComPtr<IDXGISwapChain3> swapchain;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    HANDLE event = nullptr;
    HWND window = nullptr;
    std::unique_ptr<CubeRenderer> cube;
    std::string name;
    LUID luid{};
    uint64_t serial = 0, submitted = 0, resizeGeneration = 0, dropped = 0;
    uint32_t width = 0, height = 0;
    bool open = true, canonical = true;

    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
        if (msg == WM_CLOSE) { DestroyWindow(hwnd); return 0; }
        if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
        return DefWindowProcW(hwnd,msg,wp,lp);
    }
    explicit Impl(bool show) : canonical(show) {
        SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        dawnProcSetProcs(&dawn::native::GetProcs());
        ComPtr<ID3D12Debug> debug;
        Check(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)), "D3D12 debug layer required");
        debug->EnableDebugLayer();
        Check(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG,IID_PPV_ARGS(&factory)),"CreateDXGIFactory");
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
        cube=std::make_unique<CubeRenderer>(device,native,queue,canonical);
        Check(native->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)),"present allocator");
        Check(native->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)),"present list");
        Check(list->Close(),"initial list close");
        Check(native->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)),"present fence");
        event=CreateEventW(nullptr,FALSE,FALSE,nullptr); if(!event) Die("present fence event");
        if(show) {
            WNDCLASSW wc{}; wc.lpfnWndProc=WndProc; wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=L"FrameBridgeMirror";
            if(!RegisterClassW(&wc) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS) Die("RegisterClass");
            window=CreateWindowExW(0,wc.lpszClassName,L"FrameBridge Native Mirror",WS_OVERLAPPEDWINDOW,
                                   CW_USEDEFAULT,CW_USEDEFAULT,640,360,nullptr,nullptr,wc.hInstance,nullptr);
            if(!window) Die("CreateWindow");
            Resize(640,360);
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
    void Resize(uint32_t w,uint32_t h) {
        Wait();
        cube->Resize(w,h);
        if(window) {
            if(swapchain) Check(swapchain->ResizeBuffers(2,w,h,DXGI_FORMAT_R8G8B8A8_UNORM,0),"ResizeBuffers");
            else {
                DXGI_SWAP_CHAIN_DESC1 sd{}; sd.Width=w; sd.Height=h; sd.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
                sd.SampleDesc.Count=1; sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; sd.BufferCount=2;
                sd.SwapEffect=DXGI_SWAP_EFFECT_FLIP_DISCARD;
                ComPtr<IDXGISwapChain1> sc;
                Check(factory->CreateSwapChainForHwnd(queue.Get(),window,&sd,nullptr,nullptr,&sc),"CreateSwapChainForHwnd");
                Check(sc.As(&swapchain),"Swapchain3");
                Check(factory->MakeWindowAssociation(window,DXGI_MWA_NO_ALT_ENTER),"Window association");
            }
            RECT area{0,0,static_cast<LONG>(w),static_cast<LONG>(h)};
            Check(AdjustWindowRect(&area,WS_OVERLAPPEDWINDOW,FALSE)?S_OK:E_FAIL,"AdjustWindowRect");
            if(!SetWindowPos(window,nullptr,0,0,area.right-area.left,area.bottom-area.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)) Die("SetWindowPos");
            RECT client{}; if(!GetClientRect(window,&client) || client.right!=static_cast<LONG>(w) || client.bottom!=static_cast<LONG>(h)) Die("presentation client dimensions");
        }
        width=w; height=h;
    }
    void Present() {
        ComPtr<ID3D12Resource> back;
        Check(swapchain->GetBuffer(swapchain->GetCurrentBackBufferIndex(),IID_PPV_ARGS(&back)),"GetBuffer");
        Check(allocator->Reset(),"present allocator reset");
        Check(list->Reset(allocator.Get(),nullptr),"present reset");
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource=back.Get();
        barrier.Transition.Subresource=D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore=D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter=D3D12_RESOURCE_STATE_COPY_DEST;
        list->ResourceBarrier(1,&barrier);
        // Imported simultaneous-access texture is COMMON after EndAccess + same-queue fence.
        list->CopyResource(back.Get(),cube->Resource());
        std::swap(barrier.Transition.StateBefore,barrier.Transition.StateAfter);
        list->ResourceBarrier(1,&barrier);
        Check(list->Close(),"present close");
        ID3D12CommandList* lists[]={list.Get()}; queue->ExecuteCommandLists(1,lists);
        Check(swapchain->Present(0,0),"Present");
        Wait(); // Also bounds outstanding submissions and allocator reuse.
    }
    void Validate() {
        wgpu::Instance(instance->Get()).ProcessEvents();
        ReportInfoQueue(native.Get());
        if(g_badDeviceLost || g_uncapturedError || g_d3d12Messages) Die("GPU validation");
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
void Renderer::Submit(const SceneState& state,uint64_t dropped,const std::string& capture) {
    auto& i=*impl_;
    if(!i.open || !i.canonical) Die("renderer unavailable");
    if(state.width!=i.width || state.height!=i.height || state.resizeGeneration!=i.resizeGeneration) i.Resize(state.width,state.height);
    const auto matrices=SceneMatrices(state);
    Mat4 mvp{}; for(size_t k=0;k<16;++k) mvp.v[k]=static_cast<float>(matrices.mvp[k]);
    i.cube->Render(mvp,!capture.empty(),true);
    i.Present(); i.Validate();
    i.submitted=state.frame; i.resizeGeneration=state.resizeGeneration; i.dropped=dropped;
    if(!capture.empty()) WritePng(capture,state.width,state.height,i.cube->pixels());
    const std::string title="FrameBridge Native Mirror | native-dawn | "+i.name+" | received/submitted "+
        std::to_string(state.frame)+" | resize "+std::to_string(state.resizeGeneration)+" | dropped "+std::to_string(dropped);
    SetWindowTextA(i.window,title.c_str());
}
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
    ",\"luid_verified\":true,\"dawn_validation_errors\":"+std::to_string(g_uncapturedError?1:0)+
    ",\"d3d12_messages\":"+std::to_string(g_d3d12Messages)+",\"device_lost\":"+(g_badDeviceLost?"true":"false")+"}";
}
} // namespace framebridge::render
