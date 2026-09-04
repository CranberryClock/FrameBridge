# Known issues

- The native return remains a CPU readback/copy path and is explicitly `reference-upscale NOT DLSS`; no DLSS or Streamline runtime work was started.
- The bounded isolation harness reports 29 images rather than 30 in the 640x360 cases because one-image credit is intentionally outstanding at shutdown. The final browser run continuously displayed returned pixels.
- The transport fix is a controlled build-tree patch over the pinned IXWebSocket source; the submodule gitlink and upstream history remain unchanged.
- The native window and browser return are diagnostic presentation paths, not a zero-copy browser surface or a custom Three.js backend.
- TCW-008 remains blocked pending supervisor review of this recovery.
