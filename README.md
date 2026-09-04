# FrameBridge

FrameBridge is a Windows research system. The Cube Works is its first pinned Three.js showcase, retaining the ordinary browser WebGPU cube.

TCW-001 through TCW-003 are complete. TCW-004 is **MIRROR SPIKE — NOT THREE BACKEND**. Its Node loopback endpoint is test/developer infrastructure. The browser owns simulation and scene state.

The accepted TCW-004 rework adds explicit binary session lifecycle, correlated frame/resize acknowledgements, independent TS/C++ fixtures, protocol rejection checks and delayed-processor backpressure tests. CreateBuffer and Draw remain unused reserved spike messages; the active contract is scene state, viewport/Resize, and lifecycle. The original create/update/draw/destroy fragmentation formulation is superseded by the narrow human mirror gate. TCW-BUILD-001, TCW-PROTO-001/002/003/004 and TCW-CONT-001 pass. The 240 Hz human result plus deterministic 120/144 Hz tests superseded the physical 120/144 Hz check. The accepted 1,800-second run retains its original source attribution. See the [accepted evidence manifest](artifacts/tcw-004/manifest.json) and [protocol contract](docs/tcw004-protocol.md).

Local validation: `node tools/tcw004-validate.mjs` records exact commands, output, and exit codes, including executing the C++ test. Run in a clean checkout to prove frozen installation.

Run repeated diagnostics with `$env:TCW004_DURATION_SECONDS='60'; node tools/tcw004-soak.mjs`. Commit stable implementation before acceptance, then run with duration 1800. Every run writes a unique directory under artifacts/tcw-004/runs with source/runtime hashes, timestamps, measured acceptance checks, memory samples, and process exit code. Current gate decisions belong in the evidence manifest; accepted historical evidence is not rewritten by TCW-005R.

TCW-004's accepted scope is browser/mirror continuity, not native visual rendering.

TCW-005R completes the direct C++ receiver with the recovered pinned Dawn/D3D12 renderer and a separate Win32 comparison window. The browser's canonical solid cyan cube state drives native rendering; acknowledgements follow successful GPU submission/presentation. Node is only the development asset server or test client, never a frame proxy. Clean Release builds, direct real-Chrome integration, matrix/corner parity, reconnect/resize tests and the 600-second native-rendered stability run are recorded in the [TCW-005 evidence manifest](artifacts/tcw-005/manifest.json). Automated and human supervisor status is `PASS`. The supervisor validated smooth motion, back-and-forth motion, reconnect continuity and clean close; the personal-token screenshot was intentionally not published. See [native mirror notes](docs/tcw005-native-mirror.md) and the [human procedure](artifacts/tcw-005/human-browser-steps.md).

Native rendering is now exercised and browser-to-native visual continuity is accepted by the human supervisor. DLSS, Streamline runtime integration, extension, installer, custom Three.js backend, and Unreal work remain out of scope.

TCW-006 is historical temporal-input and replaceable reference-upscaler work. Its
adds native render/input/output resources, GPU-written depth and motion readbacks,
deterministic unjittered motion vectors, reset/history rules, optional Halton jitter diagnostics and a GPU
`reference-upscale` path explicitly labeled `NOT DLSS`. Streamline, DLSS and
TCW-007 remain out of scope until this task passes review.

TCW-004A is a bounded Streamline/Dawn compatibility preflight scaffold. Its current status is
`BLOCKED_EXTERNAL_DEPENDENCY` because the pinned NVIDIA SDK and NVIDIA application ID are not available locally; no
Streamline or DLSS runtime was loaded.

TCW-007B Recovery completes the native-return experiment's one-image stall recovery. The native Dawn/D3D12 reference-upscale
output is read back as bounded RGBA8 diagnostic data and displayed in the browser
after authenticated loopback delivery, while the browser remains authoritative.
The surface is labeled `Native reference upscale — NOT DLSS.` This is CPU
readback/copy transport, not zero-copy or scanout. TCW-006R temporal findings
remain unresolved deferred debt. TCW-007B Recovery is complete and stops at its
supervisor review gate. TCW-008 is the narrow textured native-return proof: the browser-generated 256x256 RGBA8 texture is mirrored by revision, rendered by Dawn, and returned through the native reference-upscale path. It is not a Three.js backend and is not DLSS; evidence is under `artifacts/tcw-008/` and the work stops at supervisor review.

TCW-009 closes the bounded DLSS feasibility attempt as outcome **B — blocked by
specific external prerequisites**. Streamline 2.12 headers/runtime binaries and
an NVIDIA-provided NGX application ID are not configured on the validated
machine, so no Streamline module was loaded and no DLSS evaluation was claimed.
The latest reference path remains usable and now reports its active mode and
render scale to the browser instead of relying on hardcoded UI text. Its fresh
10-second baseline returned about 11 FPS at 320x180 to 640x360 and did not
demonstrate acceleration or image-quality benefit. See the
[TCW-009 closeout](docs/tcw009-dlss-closeout.md) and
[sanitized evidence](artifacts/tcw-009/manifest.json).
