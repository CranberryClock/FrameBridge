# FrameBridge

FrameBridge is a narrow Windows research system. Its first showcase demo is **The Cube Works**: a supported Three.js cube that can eventually switch from browser WebGPU to native Dawn/D3D12 with DLSS Super Resolution.

This repository has completed TCW-001 through TCW-003. TCW-004 supervisor rework adds the authenticated binary mirror protocol, strict validation, per-session state, browser connection controls, and shared TypeScript/C++ fixture checks. TCW-004 remains blocked pending a successful 1,800-second 60 Hz wall-clock soak and browser-control screenshot; the browser bridge is not yet accepted, and extension, presentation, and DLSS implementation remain intentionally absent.

For the native build, use `VsDevCmd.bat -arch=x64 -vcvars_ver=14.44`, then run the CMake presets. The doctor only reports; it never installs.

The TCW-002 spike is configured separately with `cmake -S native/interop-spike -B out/tcw002 -G Ninja -DDAWN_ROOT=<external Dawn checkout>`. It requires the pinned Dawn build and runs without CPU pixel readback.
