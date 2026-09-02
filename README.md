# FrameBridge

FrameBridge is a narrow Windows research system. Its first showcase demo is **The Cube Works**, a supported Three.js cube using the browser WebGPU fallback.

TCW-001 through TCW-003 are complete. TCW-004 is an explicit scene-mirroring protocol spike labeled **MIRROR SPIKE — NOT THREE BACKEND**. The browser remains authoritative for logical frame, simulation time, transform, camera, viewport, resize generation, and resource identity. The Node WebSocket endpoint is test/developer infrastructure; it is not a native socket listener, D3D12 renderer, or Three.js backend.

Current TCW-004 status: short build, TypeScript/C++ codec, authentication, lifecycle, quota, and queue tests are implemented. Native and browser acceptance evidence is still under supervisor review; browser gates remain HUMAN_REQUIRED until a local Chrome run captures connection, reconnect, resize, and high-refresh behavior. The real soak must be run with `node tools/tcw004-soak.mjs` and is PASS only when the runner itself exits zero.

For the native build, use `VsDevCmd.bat -arch=x64 -vcvars_ver=14.38`, then configure with the installed Visual Studio CMake generator. The doctor only reports; it never installs. Do not add NVIDIA/DLSS/Streamline binaries to Git.
