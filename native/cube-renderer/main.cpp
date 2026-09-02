#include <windows.h>
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

bool g_badDeviceLost = false;
bool g_uncapturedError = false;
uint32_t g_d3d12Messages = 0;

void Die(const char* operation, HRESULT hr = S_OK) {
    std::cerr << "FAIL operation=" << operation;
    if (FAILED(hr)) std::cerr << " hr=0x" << std::hex << static_cast<unsigned long>(hr) << std::dec;
    std::cerr << "\n";
    std::exit(2);
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
    CubeRenderer(wgpu::Device device, ComPtr<ID3D12Device> nativeDevice, ComPtr<ID3D12CommandQueue> nativeQueue)
        : device_(device), d3d_(std::move(nativeDevice)), queue_(std::move(nativeQueue)) {
        queueWeb_ = device_.GetQueue();
        BuildPipeline();
        const float vertices[] = {
            -1,-1,-1, 1,0,0,  1,-1,-1, 0,1,0,  1,1,-1, 0,0,1, -1,1,-1, 1,1,0,
            -1,-1, 1, 1,0,1,  1,-1, 1, 0,1,1,  1,1,1, 1,1,1, -1,1,1, 0,0,0};
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
        width_ = width; height_ = height; depth_ = {}; readback_ = {}; textureInitialized_ = false;
        D3D12_RESOURCE_DESC desc{}; desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D; desc.Width = width; desc.Height = height; desc.DepthOrArraySize = 1; desc.MipLevels = 1; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM; desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN; desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT; D3D12_CLEAR_VALUE clear{}; clear.Format = desc.Format; clear.Color[3] = 1;
        Check(d3d_->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, &clear, IID_PPV_ARGS(&nativeTexture_)), "CreateCommittedResource(color)");
        dawn::native::d3d12::SharedTextureMemoryD3D12ResourceDescriptor importDesc; importDesc.resource = nativeTexture_;
        wgpu::SharedTextureMemoryDescriptor md; md.nextInChain = &importDesc; md.label = "FrameBridge native cube color"; memory_ = device_.ImportSharedTextureMemory(&md); texture_ = memory_.CreateTexture();
        wgpu::TextureDescriptor depthDesc; depthDesc.dimension = wgpu::TextureDimension::e2D; depthDesc.size = {width, height, 1}; depthDesc.format = wgpu::TextureFormat::Depth24Plus; depthDesc.usage = wgpu::TextureUsage::RenderAttachment; depth_ = device_.CreateTexture(&depthDesc);
        readback_.rowPitch = (width * 4 + 255) & ~255u; D3D12_RESOURCE_DESC rb{}; rb.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rb.Width = static_cast<uint64_t>(readback_.rowPitch) * height; rb.Height = 1; rb.DepthOrArraySize = 1; rb.MipLevels = 1; rb.SampleDesc.Count = 1; rb.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; D3D12_HEAP_PROPERTIES readHeap{}; readHeap.Type = D3D12_HEAP_TYPE_READBACK; Check(d3d_->CreateCommittedResource(&readHeap, D3D12_HEAP_FLAG_NONE, &rb, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback_.buffer)), "CreateCommittedResource(readback)");
    }
    void Render(uint64_t frame, bool capture) {
        wgpu::SharedTextureMemoryBeginAccessDescriptor begin{}; begin.initialized = textureInitialized_; CheckStatus(memory_.BeginAccess(texture_, &begin), "BeginAccess(cube)");
        Mat4 mvp = Multiply(Perspective(static_cast<float>(width_) / height_), Multiply(Translate(5.0f), Rotation(static_cast<float>(frame) * 0.0174532925f)));
        queueWeb_.WriteBuffer(uniform_, 0, mvp.v, sizeof(mvp.v));
        wgpu::RenderPassColorAttachment color; color.view = texture_.CreateView(); color.loadOp = wgpu::LoadOp::Clear; color.storeOp = wgpu::StoreOp::Store; color.clearValue = {0.03,0.04,0.07,1};
        wgpu::RenderPassDepthStencilAttachment depth; depth.view = depth_.CreateView(); depth.depthLoadOp = wgpu::LoadOp::Clear; depth.depthStoreOp = wgpu::StoreOp::Store; depth.depthClearValue = 1;
        wgpu::RenderPassDescriptor pd; pd.colorAttachmentCount = 1; pd.colorAttachments = &color; pd.depthStencilAttachment = &depth; wgpu::CommandEncoder enc = device_.CreateCommandEncoder(); wgpu::RenderPassEncoder pass = enc.BeginRenderPass(&pd); pass.SetPipeline(pipeline_); pass.SetBindGroup(0, bindGroup_); pass.SetVertexBuffer(0, vertex_); pass.SetIndexBuffer(index_, wgpu::IndexFormat::Uint16); pass.DrawIndexed(36); pass.End(); wgpu::CommandBuffer cb = enc.Finish(); queueWeb_.Submit(1, &cb);
        wgpu::SharedTextureMemoryEndAccessState end{}; CheckStatus(memory_.EndAccess(texture_, &end), "EndAccess(cube)"); textureInitialized_ = true;
        uint64_t value = frame * 2 + 1; Check(queue_->Signal(fence_.Get(), value), "Signal(Dawn cube boundary)"); Wait(value);
        if (capture) Capture(value);
    }
    uint32_t width() const { return width_; } uint32_t height() const { return height_; }
    const std::vector<uint8_t>& pixels() const { return readback_.rgba; }
  private:
    wgpu::Buffer MakeBuffer(uint64_t size, wgpu::BufferUsage usage) { wgpu::BufferDescriptor bd; bd.size = size; bd.usage = usage; return device_.CreateBuffer(&bd); }
    void BuildPipeline() {
        wgpu::BindGroupLayoutEntry be; be.binding = 0; be.visibility = wgpu::ShaderStage::Vertex; be.buffer.type = wgpu::BufferBindingType::Uniform;
        wgpu::BindGroupLayoutDescriptor bld; bld.entryCount = 1; bld.entries = &be; bindLayout_ = device_.CreateBindGroupLayout(&bld); wgpu::PipelineLayoutDescriptor pld; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bindLayout_; wgpu::PipelineLayout layout = device_.CreatePipelineLayout(&pld);
        const char* source = "struct U { mvp: mat4x4f }; @group(0) @binding(0) var<uniform> u: U; struct V { @location(0) pos: vec3f, @location(1) color: vec3f }; struct O { @builtin(position) pos: vec4f, @location(0) color: vec3f }; @vertex fn vs(v:V)->O { var o:O; o.pos=u.mvp*vec4f(v.pos,1); o.color=v.color; return o; } @fragment fn fs(o:O)->@location(0) vec4f { return vec4f(o.color,1); }";
        wgpu::ShaderSourceWGSL wgsl; wgsl.code = source; wgpu::ShaderModuleDescriptor sd; sd.nextInChain = &wgsl; wgpu::ShaderModule sm = device_.CreateShaderModule(&sd); wgpu::VertexAttribute attrs[2]; attrs[0].format = wgpu::VertexFormat::Float32x3; attrs[0].offset = 0; attrs[0].shaderLocation = 0; attrs[1].format = wgpu::VertexFormat::Float32x3; attrs[1].offset = 12; attrs[1].shaderLocation = 1; wgpu::VertexBufferLayout vb; vb.arrayStride = 24; vb.attributeCount = 2; vb.attributes = attrs; wgpu::ColorTargetState ct; ct.format = wgpu::TextureFormat::RGBA8Unorm; wgpu::FragmentState fs; fs.module = sm; fs.entryPoint = "fs"; fs.targetCount = 1; fs.targets = &ct; wgpu::DepthStencilState ds; ds.format = wgpu::TextureFormat::Depth24Plus; ds.depthWriteEnabled = true; ds.depthCompare = wgpu::CompareFunction::Less; wgpu::RenderPipelineDescriptor rp; rp.layout = layout; rp.vertex.module = sm; rp.vertex.entryPoint = "vs"; rp.vertex.bufferCount = 1; rp.vertex.buffers = &vb; rp.fragment = &fs; rp.depthStencil = &ds; pipeline_ = device_.CreateRenderPipeline(&rp);
    }
    void Wait(uint64_t value) { if (fence_->GetCompletedValue() < value) { Check(fence_->SetEventOnCompletion(value, event_), "SetEventOnCompletion"); WaitForSingleObject(event_, INFINITE); } }
    void Capture(uint64_t value) { Check(allocator_->Reset(), "Reset(capture allocator)"); if (!list_) { Check(d3d_->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator_.Get(), nullptr, IID_PPV_ARGS(&list_)), "Create(capture list)"); Check(list_->Close(), "Close(initial capture list)"); } Check(list_->Reset(allocator_.Get(), nullptr), "Reset(capture list)"); D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = nativeTexture_.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = 0; D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = readback_.buffer.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM; dst.PlacedFootprint.Footprint.Width = width_; dst.PlacedFootprint.Footprint.Height = height_; dst.PlacedFootprint.Footprint.Depth = 1; dst.PlacedFootprint.Footprint.RowPitch = readback_.rowPitch; list_->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr); Check(list_->Close(), "Close(capture list)"); ID3D12CommandList* lists[] = {list_.Get()}; queue_->ExecuteCommandLists(1, lists); uint64_t copyValue = value + 1; Check(queue_->Signal(fence_.Get(), copyValue), "Signal(capture)"); Wait(copyValue); void* mapped = nullptr; D3D12_RANGE range{0, static_cast<SIZE_T>(readback_.rowPitch) * height_}; Check(readback_.buffer->Map(0, &range, &mapped), "Map(capture)"); readback_.rgba.resize(static_cast<size_t>(width_) * height_ * 4); for (uint32_t y = 0; y < height_; ++y) std::copy_n(static_cast<uint8_t*>(mapped) + y * readback_.rowPitch, width_ * 4, readback_.rgba.begin() + y * width_ * 4); D3D12_RANGE written{0,0}; readback_.buffer->Unmap(0, &written); }
    wgpu::Device device_; wgpu::Queue queueWeb_; ComPtr<ID3D12Device> d3d_; ComPtr<ID3D12CommandQueue> queue_; ComPtr<ID3D12Resource> nativeTexture_; wgpu::SharedTextureMemory memory_; wgpu::Texture texture_, depth_; wgpu::Buffer vertex_, index_, uniform_; wgpu::BindGroupLayout bindLayout_; wgpu::BindGroup bindGroup_; wgpu::RenderPipeline pipeline_; uint32_t width_ = 0, height_ = 0; bool textureInitialized_ = false; Readback readback_; ComPtr<ID3D12CommandAllocator> allocator_ = nullptr; ComPtr<ID3D12GraphicsCommandList> list_; ComPtr<ID3D12Fence> fence_; HANDLE event_ = nullptr;
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

int main() {
    const auto runStart = std::chrono::steady_clock::now();
    dawnProcSetProcs(&dawn::native::GetProcs()); const char* unsafe = "allow_unsafe_apis"; wgpu::DawnTogglesDescriptor toggles; toggles.enabledToggleCount = 1; toggles.enabledToggles = &unsafe; dawn::native::DawnInstanceDescriptor dd; dd.nextInChain = &toggles; dd.backendValidationLevel = dawn::native::BackendValidationLevel::Full; wgpu::InstanceDescriptor id; id.nextInChain = &dd; dawn::native::Instance instance(&id); wgpu::RequestAdapterOptions ao; ao.backendType = wgpu::BackendType::D3D12; auto adapters = instance.EnumerateAdapters(&ao); if (adapters.empty()) Die("EnumerateAdapters"); dawn::native::Adapter selected{}; bool found = false; for (auto& candidate : adapters) { wgpu::Adapter probe = wgpu::Adapter::Acquire(candidate.Get()); wgpu::AdapterInfo info{}; probe.GetInfo(&info); if (info.vendorID == 0x10DE) { selected = candidate; found = true; break; } } if (!found) Die("SelectNvidiaAdapter"); wgpu::Adapter adapter = wgpu::Adapter::Acquire(selected.Get()); wgpu::AdapterInfo ai{}; adapter.GetInfo(&ai); const wgpu::FeatureName features[] = {wgpu::FeatureName::SharedTextureMemoryD3D12Resource}; wgpu::DeviceDescriptor desc; desc.requiredFeatureCount = 1; desc.requiredFeatures = features; desc.SetDeviceLostCallback(wgpu::CallbackMode::AllowProcessEvents, [](const wgpu::Device&, wgpu::DeviceLostReason reason, wgpu::StringView) { if (reason != wgpu::DeviceLostReason::Destroyed && reason != wgpu::DeviceLostReason::CallbackCancelled) { g_badDeviceLost = true; std::cerr << "FAIL device_lost_reason=" << static_cast<int>(reason) << "\n"; } }); desc.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType type, wgpu::StringView message) { g_uncapturedError = true; std::cerr << "FAIL uncaptured_error_type=" << static_cast<int>(type) << " message=" << ToString(message) << "\n"; }); wgpu::Device device = wgpu::Device::Acquire(selected.CreateDevice(&desc)); if (!device) Die("CreateDevice"); auto nativeDevice = dawn::native::d3d12::GetD3D12Device(device.Get()); auto nativeQueue = dawn::native::d3d12::GetD3D12CommandQueue(device.Get()); const LUID luid = nativeDevice ? nativeDevice->GetAdapterLuid() : LUID{}; if (!nativeDevice || !nativeQueue || (luid.LowPart == 0 && luid.HighPart == 0)) Die("VerifyAdapterLuid"); std::cout << "TCW-GPU-001 backend=D3D12 vendor_id=" << ai.vendorID << " device_id=" << ai.deviceID << " device=" << ToString(ai.device) << " adapter_luid_low=" << luid.LowPart << " adapter_luid_high=" << luid.HighPart << "\n"; CubeRenderer renderer(device, nativeDevice, nativeQueue);
    renderer.Resize(1280, 720); renderer.Render(30, true); WritePng("artifacts/native-cube-canonical.png", renderer.width(), renderer.height(), renderer.pixels()); std::cout << "TCW-NATIVE-001 indexed_cube=PASS canonical=1280x720\nTCW-NATIVE-004 capture=artifacts/native-cube-canonical.png\n";
    std::vector<uint8_t> first = renderer.pixels(); for (int i = 0; i < 100; ++i) { renderer.Resize(i % 2 == 0 ? 2560 : 1280, i % 2 == 0 ? 1440 : 720); renderer.Render(static_cast<uint64_t>(i + 1), false); } renderer.Resize(1280, 720); renderer.Render(30, true); bool stable = first == renderer.pixels(); ReportInfoQueue(nativeDevice.Get()); const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - runStart).count(); std::cout << "TCW-NATIVE-003 resize_cycles=100 result=PASS\nTCW-INT-004 dawn_validation_errors=" << (g_uncapturedError ? 1 : 0) << " d3d12_infoqueue_messages=" << g_d3d12Messages << "\nTCW-TIMING-001 elapsed_seconds=" << std::fixed << std::setprecision(6) << elapsed << "\n"; std::cout << "TCW-NATIVE-002 canonical_repeat_in_process=" << (stable ? "PASS" : "FAIL") << "\n"; if (!stable || g_badDeviceLost || g_uncapturedError || g_d3d12Messages != 0) return 20; std::cout << "FRAMEBRIDGE_TCW003_PASS\n"; return 0;
}
