# ADR 0002: Dawn/D3D12 shared-texture interop baseline

## Status

Accepted for the next bridge phase after TCW-002 validation.

## Decision

Use Dawn commit `34b1fca4d6c3d7025a2231d82b4fc719ca57fd71` on Windows x64. Create the native D3D12 resource from Dawn's underlying D3D12 device, import it through `SharedTextureMemoryD3D12Resource`, bracket Dawn rendering with `BeginAccess`/`EndAccess`, and hand the resource to a native D3D12 command list after an explicit same-queue fence signal/wait boundary.

## Evidence

The TCW-002 spike selected the NVIDIA GeForce RTX 4080 Laptop GPU on D3D12, logged its adapter LUID, rendered a deterministic WGSL RGBA pattern, consumed the imported resource with native `CopyResource`, and completed 10,000 frames without CPU pixel readback. Dawn's same-device path exported zero shared fences; the explicit native queue signal/wait boundary is therefore the synchronization evidence for this path.

## Consequences

This validates the core ownership handoff and does not authorize a raw-D3D12 renderer. Cross-device or cross-process sharing must add a separate shared-fence experiment before being treated as supported.
