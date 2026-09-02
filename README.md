# FrameBridge

FrameBridge is a Windows research system. The Cube Works is its first pinned Three.js showcase, retaining the ordinary browser WebGPU cube.

TCW-001 through TCW-003 are complete. TCW-004 is **MIRROR SPIKE — NOT THREE BACKEND**. Its Node loopback endpoint is test/developer infrastructure. The browser owns simulation and scene state.

The final TCW-004 rework adds explicit binary session lifecycle, correlated frame/resize acknowledgements, independent TS/C++ fixtures for all twelve messages, exhaustive protocol rejection assertions, delayed-processor backpressure tests, and deterministic refresh scheduling tests. CreateBuffer and Draw remain unused reserved spike messages; the active contract is scene state, viewport/Resize, and lifecycle. The original create/update/draw/destroy fragmentation formulation is superseded by the narrow human mirror gate described in the protocol contract. Minor rework `c1a979d117a8310809a7afce2604a6ecdfca14c0` passes clean local validation and fresh diagnostics; the accepted 1,800.024-second soak remains linked to the unchanged prior implementation. TCW-BUILD-001, TCW-PROTO-001/002/003/004, and TCW-CONT-001 pass; the exact 120/144 Hz and separately captured 800×450 acknowledgement sub-checks remain HUMAN_REQUIRED. See the [evidence manifest](artifacts/tcw-004/manifest.json), [protocol contract](docs/tcw004-protocol.md), and [human checklist](artifacts/tcw-004/human-browser-steps.md).

Local validation: `node tools/tcw004-validate.mjs` records exact commands, output, and exit codes, including executing the C++ test. Run in a clean checkout to prove frozen installation.

Run repeated diagnostics with `$env:TCW004_DURATION_SECONDS='60'; node tools/tcw004-soak.mjs`. Commit stable implementation before acceptance, then run with duration 1800. Every run writes a unique directory under artifacts/tcw-004/runs with source/runtime hashes, timestamps, measured acceptance checks, memory samples, and process exit code. The prior reconstructed real-soak.json is stale and provides no acceptance evidence. Current gate decisions belong in the evidence manifest.

No native socket endpoint, native bridge renderer, DLSS, Streamline, extension, installer, custom Three.js backend, or Unreal work is part of this task. Stop for supervisor review after TCW-004.
