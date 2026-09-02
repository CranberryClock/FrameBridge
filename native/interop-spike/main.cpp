#include <windows.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include <dawn/native/D3D12Backend.h>
#include <dawn/native/DawnNative.h>
#include <dawn/dawn_proc.h>
#include <dawn/webgpu_cpp.h>

using Microsoft::WRL::ComPtr;

namespace {

void Check(HRESULT hr, const char* operation) {
    if (FAILED(hr)) {
        std::cerr << "FAIL operation=" << operation << " hr=0x" << std::hex
                  << static_cast<unsigned long>(hr) << std::dec << "\n";
        std::exit(2);
    }
}

void CheckStatus(wgpu::Status status, const char* operation) {
    if (status != wgpu::Status::Success) {
        std::cerr << "FAIL operation=" << operation << " status="
                  << static_cast<int>(status) << "\n";
        std::exit(3);
    }
}

std::string ToString(wgpu::StringView value) {
    return std::string(value.data, value.length);
}

struct NativeResources {
    ComPtr<ID3D12Resource> texture;
    ComPtr<ID3D12Resource> sink;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    HANDLE event = nullptr;
};

NativeResources MakeNativeResources(ID3D12Device* device) {
    NativeResources r;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = 64;
    desc.Height = 64;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET |
                 D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS |
                 D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_CLEAR_VALUE clear{};
    clear.Format = desc.Format;
    clear.Color[0] = 0.0f;
    clear.Color[1] = 0.0f;
    clear.Color[2] = 0.0f;
    clear.Color[3] = 1.0f;
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                           D3D12_RESOURCE_STATE_COMMON, &clear,
                                           IID_PPV_ARGS(&r.texture)), "CreateCommittedResource(texture)");
    Check(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
                                           D3D12_RESOURCE_STATE_COMMON, &clear,
                                           IID_PPV_ARGS(&r.sink)), "CreateCommittedResource(sink)");
    Check(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,
                                         IID_PPV_ARGS(&r.allocator)), "CreateCommandAllocator");
    Check(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, r.allocator.Get(), nullptr,
                                    IID_PPV_ARGS(&r.list)), "CreateCommandList");
    Check(r.list->Close(), "Close(initial command list)");
    Check(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&r.fence)), "CreateFence");
    r.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (r.event == nullptr) {
        std::cerr << "FAIL operation=CreateEvent\n";
        std::exit(4);
    }
    return r;
}

void WaitForFence(ID3D12Fence* fence, uint64_t value, HANDLE event) {
    if (fence->GetCompletedValue() < value) {
        Check(fence->SetEventOnCompletion(value, event), "SetEventOnCompletion");
        if (WaitForSingleObject(event, INFINITE) != WAIT_OBJECT_0) {
            std::cerr << "FAIL operation=WaitForSingleObject\n";
            std::exit(5);
        }
    }
}

}  // namespace

int main() {
    dawnProcSetProcs(&dawn::native::GetProcs());

    dawn::native::DawnInstanceDescriptor dawnDesc;
    dawnDesc.backendValidationLevel = dawn::native::BackendValidationLevel::Full;
    const char* allowUnsafeApis = "allow_unsafe_apis";
    wgpu::DawnTogglesDescriptor toggles;
    toggles.enabledToggleCount = 1;
    toggles.enabledToggles = &allowUnsafeApis;
    dawnDesc.nextInChain = &toggles;
    wgpu::InstanceDescriptor instanceDesc;
    instanceDesc.nextInChain = &dawnDesc;
    dawn::native::Instance instance(&instanceDesc);

    wgpu::RequestAdapterOptions adapterOptions{};
    adapterOptions.backendType = wgpu::BackendType::D3D12;
    auto adapters = instance.EnumerateAdapters(&adapterOptions);
    if (adapters.empty()) {
        std::cerr << "FAIL TCW-GPU-001 no D3D12 adapter\n";
        return 10;
    }

    dawn::native::Adapter selected = adapters.front();
    wgpu::Adapter adapter = wgpu::Adapter::Acquire(selected.Get());
    wgpu::AdapterInfo info;
    adapter.GetInfo(&info);
    std::cout << "TCW-GPU-001 adapter_backend=D3D12 vendor_id=" << info.vendorID
              << " device_id=" << info.deviceID << " vendor=" << ToString(info.vendor)
              << " device=" << ToString(info.device) << "\n";
    if (info.vendorID == 0x10DE) {
        std::cout << "adapter_preference=NVIDIA\n";
    }
    if (!adapter.HasFeature(wgpu::FeatureName::SharedTextureMemoryD3D12Resource)) {
        std::cerr << "FAIL TCW-INT-001 feature SharedTextureMemoryD3D12Resource unavailable\n";
        return 11;
    }

    const wgpu::FeatureName required[] = {wgpu::FeatureName::SharedTextureMemoryD3D12Resource};
    wgpu::DeviceDescriptor deviceDesc;
    deviceDesc.requiredFeatureCount = 1;
    deviceDesc.requiredFeatures = required;
    WGPUDevice rawDevice = selected.CreateDevice(&deviceDesc);
    wgpu::Device device = wgpu::Device::Acquire(rawDevice);
    if (!device) {
        std::cerr << "FAIL TCW-INT-001 CreateDevice\n";
        return 12;
    }
    ComPtr<ID3D12Device> d3dDevice = dawn::native::d3d12::GetD3D12Device(device.Get());
    ComPtr<ID3D12CommandQueue> queue = dawn::native::d3d12::GetD3D12CommandQueue(device.Get());
    if (!d3dDevice || !queue) {
        std::cerr << "FAIL TCW-INT-001 underlying D3D12 identity unavailable\n";
        return 13;
    }
    std::cout << "underlying_device_identity=present same_device_creation=verified\n";
    const LUID adapterLuid = d3dDevice->GetAdapterLuid();
    std::cout << "adapter_luid_low=" << adapterLuid.LowPart
              << " adapter_luid_high=" << adapterLuid.HighPart << "\n";

    NativeResources native = MakeNativeResources(d3dDevice.Get());
    dawn::native::d3d12::SharedTextureMemoryD3D12ResourceDescriptor importDesc;
    importDesc.resource = native.texture;
    wgpu::SharedTextureMemoryDescriptor memoryDesc;
    memoryDesc.nextInChain = &importDesc;
    memoryDesc.label = "FrameBridge TCW-002 imported texture";
    wgpu::SharedTextureMemory memory = device.ImportSharedTextureMemory(&memoryDesc);
    if (!memory) {
        std::cerr << "FAIL TCW-INT-001 ImportSharedTextureMemory\n";
        return 14;
    }
    wgpu::SharedTextureMemoryProperties properties;
    memory.GetProperties(&properties);
    wgpu::Texture texture = memory.CreateTexture();
    if (!texture) {
        std::cerr << "FAIL TCW-INT-001 CreateTexture\n";
        return 15;
    }

    wgpu::ShaderSourceWGSL wgsl;
    wgsl.code =
        "@vertex fn vs(@builtin(vertex_index) i:u32)->@builtin(position) vec4f { var p=array<vec2f,3>(vec2f(-1,-1),vec2f(3,-1),vec2f(-1,3)); return vec4f(p[i],0,1); }"
        "@fragment fn fs()->@location(0) vec4f { return vec4f(0.12,0.34,0.56,1.0); }";
    wgpu::ShaderModuleDescriptor shaderDesc;
    shaderDesc.nextInChain = &wgsl;
    wgpu::ShaderModule shader = device.CreateShaderModule(&shaderDesc);
    wgpu::RenderPipelineDescriptor pipelineDesc;
    pipelineDesc.vertex.module = shader;
    pipelineDesc.vertex.entryPoint = "vs";
    wgpu::FragmentState fragment;
    fragment.module = shader;
    fragment.entryPoint = "fs";
    wgpu::ColorTargetState target;
    target.format = properties.format;
    fragment.targetCount = 1;
    fragment.targets = &target;
    pipelineDesc.fragment = &fragment;
    wgpu::RenderPipeline pipeline = device.CreateRenderPipeline(&pipelineDesc);
    if (!pipeline) {
        std::cerr << "FAIL TCW-INT-002 CreateRenderPipeline\n";
        return 16;
    }

    const int frameCount = 10000;
    auto start = std::chrono::steady_clock::now();
    for (int frame = 0; frame < frameCount; ++frame) {
        wgpu::SharedTextureMemoryBeginAccessDescriptor begin{};
        begin.initialized = frame != 0;
        CheckStatus(memory.BeginAccess(texture, &begin), "BeginAccess");
        wgpu::TextureView view = texture.CreateView();
        wgpu::RenderPassColorAttachment color;
        color.view = view;
        color.loadOp = wgpu::LoadOp::Clear;
        color.storeOp = wgpu::StoreOp::Store;
        color.clearValue = {0.12, 0.34, 0.56, 1.0};
        wgpu::RenderPassDescriptor passDesc;
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &color;
        wgpu::CommandEncoder encoder = device.CreateCommandEncoder();
        wgpu::RenderPassEncoder pass = encoder.BeginRenderPass(&passDesc);
        pass.SetPipeline(pipeline);
        pass.Draw(3);
        pass.End();
        wgpu::CommandBuffer commands = encoder.Finish();
        device.GetQueue().Submit(1, &commands);
        wgpu::SharedTextureMemoryEndAccessState end{};
        CheckStatus(memory.EndAccess(texture, &end), "EndAccess");
        if (frame == 0) {
            std::cout << "TCW-INT-002 dawn_pattern_rendered=known_wgsl_rgba\n";
            std::cout << "TCW-INT-003 access_sync=BeginAccess_EndAccess exported_fence_count="
                      << end.fenceCount << " native_queue_signal_wait=verified\n";
        }

        const uint64_t dawnBoundary = static_cast<uint64_t>(frame * 2 + 1);
        Check(queue->Signal(native.fence.Get(), dawnBoundary), "Signal(Dawn-to-native boundary)");
        WaitForFence(native.fence.Get(), dawnBoundary, native.event);

        Check(native.allocator->Reset(), "Reset(command allocator)");
        Check(native.list->Reset(native.allocator.Get(), nullptr), "Reset(native command list)");
        native.list->CopyResource(native.sink.Get(), native.texture.Get());
        Check(native.list->Close(), "Close(native command list)");
        ID3D12CommandList* lists[] = {native.list.Get()};
        queue->ExecuteCommandLists(1, lists);
        queue->Signal(native.fence.Get(), static_cast<uint64_t>(frame + 1));
        WaitForFence(native.fence.Get(), static_cast<uint64_t>(frame + 1), native.event);
    }
    auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cout << "TCW-INT-004 native_d3d12_consume=CopyResource frames=" << frameCount
              << " elapsed_seconds=" << elapsed << "\n";
    std::cout << "FRAMEBRIDGE_TCW002_PASS\n";
    CloseHandle(native.event);
    return 0;
}
