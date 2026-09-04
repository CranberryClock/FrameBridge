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
