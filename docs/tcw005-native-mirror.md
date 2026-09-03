# TCW-005R — Native Mirror Cube

MIRROR SPIKE — NOT THREE BACKEND

The browser owns logical time, rotations, cameraZ, viewport and resize generation.
It connects directly to the C++ receiver. Node is only a test client/oracle or the
Vite asset server. Native presentation is a separate Win32 window.

## Canonical scene

Three.js r185, BoxGeometry(1,1,1), solid unlit cyan #36d6ff, background #080b12,
no lights, no MSAA, pixel ratio 1. Camera is at (0,0,cameraZ), cameraZ > 0,
looking down -Z; FOV 60 degrees, near 0.1, far 100. Euler XYZ, z rotation zero.
The protocol carries f32 X/Y rotations and cameraZ, f64 simulation time, u64
logical frame and resize generation. Both sides use those decoded values.
The native renderer never derives rotations from a clock or frame number.

Matrices are column-major, column vectors, right handed. Model = Rx * Ry * Rz;
MVP = projection * view * model. WebGPU/D3D12 clip depth is [0,1].
The independent oracle uses actual Three Matrix4, Euler, PerspectiveCamera with
WebGPUCoordinateSystem, and Vector3.applyMatrix4. Native double matrices and
all eight projected corners must match within absolute 1e-12. Float uniform
projection must match within 2e-6 (rounding of f32 multiply/add and division).
Fixtures cover frames 60/120/180 at both 640x360 and 800x450.

The extracted TCW-003 library retains its legacy colored 90-degree-FOV capture
entry point. Its device, pipeline, shared texture, fence and capture machinery
are shared with the canonical renderer. The old colored cube is not the parity
reference. Native PNG readback is optional evidence capture only; presentation
always blits on the GPU to a same-device DXGI flip-discard swapchain.
Readback buffers are allocated lazily only for explicit captures. Live resize does
not allocate CPU readback storage; color/depth views are reused between resizes.
A bounded two-entry target pool (active plus one cached size) reuses native and
Dawn targets across the two demo viewport sizes. New allocations start with
initialized=false; reused textures retain their own successful EndAccess state.
Dimensions outside the cache evict its oldest inactive target. The acceptance
soak checks that the alternating demo sizes require exactly two target allocations.
Swapchain storage grows only when required. The native client area matches each
received viewport exactly. A same-device D3D12 fullscreen triangle samples the
entire Dawn-rendered texture into the full swapchain buffer using normalized UVs;
DXGI stretch maps that full buffer to the client area. The cube itself is rendered
by Dawn, not by this presentation-only blit. This avoids repeated allocation churn
while preserving centered, full-window presentation after shrinking the viewport.
No CPU readback or browser streaming is used for presentation.

## Device and synchronization

Dawn pin: 34b1fca4d6c3d7025a2231d82b4fc719ca57fd71.
Select TCW-002 NVIDIA vendor 0x10de/device 0x27e0 through DXGI, request that exact
current LUID from Dawn, compare the extracted D3D12 device LUID and queue COM
identity. LUIDs can change after reboot; a historical LUID is not portable.
Dawn Full validation and D3D12 debug/InfoQueue are required, with every attributable
warning/error failing rendering. Device loss also fails. GPU waits time out after
10 seconds. The Windows SDK FXC DLL is copied to the ignored executable directory.
DXGI diagnostic storage is also bounded and drained; its warnings/errors fail.

Every new imported color texture starts initialized=false; BeginAccess uses that
state; only successful EndAccess makes it true. EndAccess flushes Dawn, followed
by an explicit same-queue fence/wait. Native blits to the swapchain, presents,
waits before allocator reuse, checks validation, then permits an ACK.

READY and native-dawn capabilities require successful device, target and window
initialization. Default startup without Dawn fails. Explicit --test-protocol-only
has backend=test-harness. --test-init-failure and --test-render-failure are
controlled negative test hooks and always fail the relevant gate.

## Ownership and limits

IXWebSocket v12.0.1 and nlohmann/json v3.11.3 remain pinned submodules.
Network callbacks validate and assemble complete frames under a short mutex.
The process main thread owns Win32 message pumping and all Dawn/D3D12 resources.
It dequeues under the mutex, releases it, renders/presents and sends the ACK.
All send/close calls occur outside that mutex. Shared client ownership protects
in-flight work, an atomic active bit suppresses obsolete session acknowledgements,
and weak callback captures avoid socket cycles. IX joins all network threads on
stop; there are no detached or blocking stdin threads.

The queue holds at most two complete frames, dropping the oldest unprocessed
frame on saturation. The cumulative drop count is frozen when a frame is dequeued;
drops of later frames during rendering must appear only in a later ACK. Frames
already in flight are neither queued nor dropped. ACK correlation is per session.

At most four transport connections can wait for authentication; only one controls
the scene. Exact launch-configured Origin is checked in both HTTP and hello.
Hello deadline is five seconds. Hello is at most 8 KiB, binary at most 1 MiB+36.
The reviewed build-tree quota patch in third_party/ixwebsocket-limits.cmake checks
advertised frame and accumulated fragment sizes before receive-buffer growth,
and bounds fragments at 256; its upstream source SHA is verified before patching.
No upstream source is rewritten. CreateBuffer, DestroyResource and Draw are
reserved and rejected. No arbitrary resources are allocated by protocol messages.
Checksum detects corruption, not authentication.

Disconnect ends the controlling session and clears its queue; the window remains
available for reconnect. Closing the native window, pressing Enter in its console,
or Ctrl+C ends the process, closes clients and joins transport threads. The browser
continues its own simulation after disconnect or native exit.

## Build and validation

From a VS 2022 machine with MSVC 14.44 and SDK 10.0.26100, Python and Go:

~~~powershell
git submodule update --init --recursive
./tools/bootstrap-dawn.ps1
. ./tools/msvc-env.ps1
cmake -S . -B build/tcw005r -G Ninja -DCMAKE_BUILD_TYPE=Release -DDAWN_ROOT=.local/dawn
cmake --build build/tcw005r --parallel 8
ctest --test-dir build/tcw005r --output-on-failure
corepack pnpm install --frozen-lockfile
corepack pnpm build
corepack pnpm test
corepack pnpm --filter @framebridge/demo-web exec vite build
node tools/tcw005r-parity-oracle.mjs
node tools/tcw005r-integration.mjs build/tcw005r/framebridge-native-mirror.exe
node tools/tcw005r-browser.mjs build/tcw005r/framebridge-native-mirror.exe
node tools/tcw005r-stability.mjs build/tcw005r/framebridge-native-mirror.exe 600
~~~

Bootstrap accepts -DawnRoot, -Python and -GoBin for existing caches. -SkipFetch
reuses already fetched dependencies. It verifies the pin and preserves dirty or
mismatched checkouts by refusing to overwrite them. Configure never fetches Dawn.
FRAMEBRIDGE_CHROME points browser tests at an installed Chrome executable.
The stability run requires a committed clean tracked source checkout. Its bounds
are 512 MiB peak working/private memory and 64 MiB private growth after warmup.
The result records all evaluated gates and measurements even when a bound fails.
Short CI builds run codec/session/parity and protocol-only transport tests without
requiring NVIDIA hardware. The hardware acceptance run is recorded separately.

See artifacts/tcw-005/manifest.json for measured results and human-browser-steps.md
for the historical human review result, which passed. The local token-bearing
screenshot was intentionally excluded. No TCW-006, DLSS, Streamline, overlay,
extension, installer or general Three backend is included.
