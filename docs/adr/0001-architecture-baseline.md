# ADR-0001: FrameBridge architecture baseline

## Status
Accepted for TCW-001 scaffold; empirical gates remain open.

## Decision
FrameBridge remains a narrow Windows 11 x64 research prototype. The browser owns logical scene state; a pinned Three.js backend will later serialize the supported scene to an authenticated loopback runtime. Native rendering will use Dawn/D3D12, Streamline DLSS SR, and a disclosed managed native surface over one fixed page slot. M0 builds without Dawn or NVIDIA binaries through a null runtime.

## Consequences
The public claim remains bounded to The Cube Works. No arbitrary-site, second-engine, Frame Generation, ray-tracing, cross-platform, or direct DOM texture-import claim is implied.
