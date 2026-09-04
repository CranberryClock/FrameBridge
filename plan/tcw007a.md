# TCW-007A live native return

This packet continues TCW-007 from the authenticated browser image-return path.
The browser remains authoritative and parity URLs freeze state for capture only;
the ordinary URL is the live showcase. TCW-006R temporal correctness findings
remain deferred, unresolved debt, and are not reopened here. The current return
transport is CPU readback/copy diagnostic data, not zero-copy, scanout, or DLSS.

The existing CPU magenta marker is retained as a transport diagnostic and is
not GPU-rendered. The native moving cube itself is the provenance signal for
the post-reference-upscale output. A future packet may add a GPU-coded stripe.

## Proposed next task

TCW-008 — Browser Texture Upload to Native Cube

One fixed-size diagnostic RGBA texture generated or loaded in the browser is
uploaded once to the native runtime, sampled by Dawn on the existing cube, and
replaceable by resource revision. The proven return path is reused; no general
geometry/material protocol or arbitrary asset loading is proposed.

## Known issues at review handoff

- The 65-second live run received and displayed one native image, then native
  submissions stopped advancing; the browser correctly bounded outstanding
  scene frames and entered stale-image fallback. Continuous live return is not
  accepted.
- Native-to-browser image flow has no independently bounded return queue or
  explicit send-completion telemetry yet; the 12 FPS timer alone is not the
  complete flow-control solution.
- The visible magenta marker is CPU-applied after readback and is not a
  GPU-rendered frame diagnostic. A changing GPU-coded stripe remains future
  work.
- A concise `artifacts/tcw-007a/` evidence bundle and safe recording were not
  produced because the continuous-live gate was not demonstrated.
- TCW-006R temporal correctness findings remain unresolved deferred debt.
