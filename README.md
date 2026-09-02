# FrameBridge

FrameBridge is a narrow Windows research system. Its first showcase demo is **The Cube Works**: a supported Three.js cube that can eventually switch from browser WebGPU to native Dawn/D3D12 with DLSS Super Resolution.

This TCW-001 scaffold contains strict TypeScript, a C++20 null runtime, CMake presets, a dependency ledger, and a non-installing doctor. TCW-002 adds a Windows-only Dawn/D3D12 shared-texture interop spike under `native/interop-spike/`; bridge, extension, presentation, and DLSS implementation remain intentionally absent.

For the native build, use `VsDevCmd.bat -arch=x64 -vcvars_ver=14.44`, then run the CMake presets. The doctor only reports; it never installs.

The TCW-002 spike is configured separately with `cmake -S native/interop-spike -B out/tcw002 -G Ninja -DDAWN_ROOT=<external Dawn checkout>`. It requires the pinned Dawn build and runs without CPU pixel readback.
