# FrameBridge — The Cube Works

An open research prototype exploring whether a Three.js/WebGPU webpage can hand rendering work to an explicitly installed native Dawn/D3D12 process and receive the resulting pixels back in the browser.

**The cube works. The product path does not.**

This repository documents a working browser-to-native rendering experiment. It is not a production renderer, browser plug-in, sandbox escape, or working DLSS integration*. (yet)

## What was proven

The prototype demonstrated this round trip on Windows with Chrome, WebGPU, Dawn, D3D12, and an NVIDIA RTX system:

```text
Three.js/WebGPU scene → authenticated loopback bridge → native Dawn/D3D12
→ reference upscale → RGBA8 pixel return → browser canvas
```

The browser remained authoritative for scene state, camera, animation, and texture data. A textured cube was rendered natively and returned to the page. Resize, reconnect, frame pacing, backpressure, and texture revision behavior were exercised.

The native output currently travels through readback and transport. This makes the path observable, but introduces substantial latency and is not evidence of a useful performance improvement.

## What was not proven

DLSS Super Resolution was **not** integrated in this release build or evaluated on this main branch. The final feasibility task was closed because the required external NVIDIA prerequisites were unavailable for this experiment: Streamline/DLSS SDK, approved application ID, governed runtime download, license/redistribution path, and a loaded Streamline runtime.

No NVIDIA binaries, secrets, or private SDK paths are included. 
Do not describe this demo as “DLSS in Three.js.” 
Describe it as a native Dawn bridge with a vendor-independent reference upscale.

The native frame must be derived from the pinned Three.js renderer/backend lifecycle—not from a separately hand-authored look-alike cube.

The experiment is closed at the research boundary. See the [TCW-009 closeout](https://github.com/CranberryClock/FrameBridge/commit/76dd0e593bdc28d9e2d0b3f7fd7a3c1f1ea9608f).

| Area | Status |
| --- | --- |
| Browser Three.js/WebGPU scene | Demonstrated |
| Authenticated loopback bridge | Demonstrated |
| Native Dawn/D3D12 rendering | Demonstrated |
| Native texture sampling | Demonstrated |
| Native output returned to browser | Demonstrated |
| Reference upscale | Demonstrated |
| Genuine NVIDIA DLSS | Blocked by external prerequisites | No Vendor approval
| Zero-copy browser presentation | Not implemented |
| Production performance | Not claimed |

## Roles

- **Supervisor (CranberryClock/Gizmo/ChatGPT):** owns architecture, scope, gates, and review decisions.
- **Human driver (CranberryClock):** operates the Windows/RTX machine, grants SDK licenses, runs hardware tests, captures visual evidence, and chooses product direction.
- **Engineer (Codex/CranberryClock):** implements one approved task at a time, tests it, and returns an evidence bundle.

## Getting started

This is primarily a reproducible engineering handoff and evidence archive. The original prototype was validated on Windows 11 x64, Chrome/WebGPU, an NVIDIA RTX GPU, Visual Studio/C++ build tools, and D3D12-capable drivers.

1. Read [`AGENTS.md`](AGENTS.md).
2. Read [`docs/00_PROJECT_CHARTER.md`](docs/00_PROJECT_CHARTER.md) and [`docs/02_ARCHITECTURE.md`](docs/02_ARCHITECTURE.md).
3. Read [`docs/11_VIABILITY_REVIEW.md`](docs/11_VIABILITY_REVIEW.md).
4. Review completed task artifacts and commit history before attempting a rebuild.

Exact commands and environment details are retained in the task evidence bundles. The original workflow was gated: one task at a time, with evidence and human hardware verification and clean up before commits.

## Security model

The native component is an explicitly installed executable. The browser does not gain arbitrary native execution or filesystem access. The runtime listens on loopback and requires an authenticated session. Never expose it beyond loopback, accept arbitrary commands, or commit credentials/vendor SDK material.

This is a sidecar experiment, not a browser security bypass.

## Scope decisions

The project intentionally excludes Unreal Engine, Unity, arbitrary Three.js compatibility, Frame Generation, ray tracing, Ray Reconstruction, AMD/Intel backends, Linux/macOS support, and a public installer. Those are separate projects, not unfinished promises here. This is PoC it can work.

## Contributing

Include your OS, browser, GPU, driver, commit, exact commands, logs/screenshots, path classification, and measured latency/FPS. Do not commit proprietary SDK files, credentials, generated build trees, or private machine paths. Discuss protocol or scope changes before implementing them.

## License and third-party software

Check the repository and dependency licenses before redistributing a build. NVIDIA Streamline/DLSS materials are not included and remain subject to NVIDIA’s terms.

## Conclusion

The experiment answered its core question: a webpage can remain the source of truth while an installed native renderer performs GPU work and returns an image to the page. It also exposed the practical limits—vendor integration, security, presentation, transport overhead, and performance. 

The bridge is real; making it worth installing is a different problem.

