# TCW-009 — DLSS feasibility closeout

## Outcome

Classification: **B — blocked by specific external prerequisites**.

The browser-controlled textured cube is proved through the native Dawn/D3D12
renderer, reference upscaler, CPU readback, authenticated loopback return, and
browser canvas presentation. Genuine DLSS Super Resolution was not initialized
or evaluated because the validated machine has neither a configured Streamline
SDK root nor an NVIDIA-provided NGX application ID.

This does not prove an architectural incompatibility. It means the first real
vendor call is unavailable until the governed prerequisites are supplied.

## Direct answers

1. **Can the complete Dawn → genuine DLSS SR → webpage path run?** Not proved.
   The Dawn/resource and temporal seams exist, but no Streamline or NGX module
   was loaded and no DLSS output was returned.
2. **Does the complete path demonstrate a benefit?** No. There is no DLSS result
   to compare. The current reference diagnostic path is capped near 12 returned
   images per second and its CPU readback plus roughly 0.92 MB WebSocket image
   copies are presentation overhead, not renderer throughput.

## Fresh reference baseline

The implementation commit `694c82b444cf9c6ff1bfe0f5812a8d6ca07c56eb`
was exercised for 10 seconds in Chrome 152.0.7977.75:

- native-reported mode: `reference-upscale — NOT DLSS`;
- native-reported dimensions: 320x180 input to 640x360 output;
- returned FPS: 6 minimum, 11 median, 11 maximum;
- correlated submission-to-browser age: 10.5 ms minimum, 21.5 ms median,
  37.6 ms maximum;
- 107 images received and displayed;
- 9 dropped complete frames and 1 outstanding frame at the final sample;
- no half-second sample interval without a new image, protocol error, or page
  error.

The low returned cadence can look substantially laggier than the browser's own
rendering even when the age of a returned sampled frame is modest. This run must
not be described as smooth or accelerated.

GPU scene time, GPU upscaler time, and isolated CPU readback/copy time are
`UNAVAILABLE` in this baseline. The returned RGBA8 image contains 921,600 pixel
bytes; the version-0 protocol adds 48 bytes of image metadata and a 36-byte
header, for 921,684 bytes per returned WebSocket message.

## Actual current path

The browser remains authoritative for simulation, camera, transform, viewport,
resize generation, and texture revision. It sends the explicit scene mirror to
the loopback native process. Dawn renders at 320x180, the D3D12 reference pass
upsamples to 640x360, the GPU output is copied to CPU RGBA8 memory, and one
bounded binary WebSocket image is displayed through `putImageData` in the page.

This is an explicit Three.js scene mirror. It is not arbitrary Three.js
compatibility, browser-native DLSS, a custom Three.js backend, or a sandbox
escape.

## Official Streamline 2.12 prerequisite audit

Research was refreshed on 2026-09-04 against NVIDIA's official sources:

- Streamline's current guides identify themselves as version 2.12.0.
- NGX features such as DLSS require an NVIDIA-provided `applicationId` during
  initialization; no documented general-purpose development ID was found.
- Manual hooking requires Streamline initialization, the manual-hooking flag,
  the host D3D12 device supplied to Streamline, feature requirement/support
  queries, correct frame presentation integration, and shutdown before D3D12
  destruction.
- DLSS requires render-resolution color, depth and motion inputs plus an
  output-resolution color target; exposure may be supplied or auto exposure
  explicitly selected. Optimal input dimensions must come from
  `slDLSSGetOptimalSettings`, not an assumed 0.5 scale.
- The runtime set includes `sl.interposer.dll` and `sl.common.dll`, with
  `sl.dlss.dll` and `nvngx_dlss.dll` for DLSS. Production modules are expected
  to be signature-validated from full paths.
- NVIDIA's RTX SDK license governs NGX/DLSS SDK use and distribution. No SDK
  license was accepted and no governed binary was downloaded during TCW-009.

Official references:

- https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuide.md
- https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideManualHooking.md
- https://github.com/NVIDIA-RTX/Streamline/blob/main/docs/ProgrammingGuideDLSS.md
- https://github.com/NVIDIA-RTX/Streamline/blob/main/external/ngx-sdk/license.txt
- https://developer.nvidia.com/rtx/dlss

The installed RTX 4080 Laptop GPU and driver 591.86 are recorded, but driver/GPU
presence alone is not a DLSS support result. `slGetFeatureRequirements` and
`slIsFeatureSupported` could not run without the SDK and application identity.

## Human-action checkpoint

To resume the single bounded real integration probe:

1. Obtain Streamline SDK 2.12 from NVIDIA through an authorized channel and
   personally accept any applicable NVIDIA terms. Keep its headers and signed
   runtime binaries outside Git.
2. Obtain an NVIDIA-issued NGX application ID for FrameBridge/The Cube Works.
   Do not reuse another application's ID and do not send the value in chat.
3. Set `FRAMEBRIDGE_STREAMLINE_ROOT` and `FRAMEBRIDGE_NVIDIA_APP_ID` only in the
   local test environment. Do not put either value in source, commands, URLs,
   screenshots, logs, or evidence.
4. Confirm to the supervisor that the governed prerequisites are locally
   available and authorize their use. The next run should first verify binary
   provenance/signatures, then execute `slInit`, device identity, requirements,
   feature support, optimal dimensions, one genuine evaluation, and cleanup.

## Reproducible reference launch

The reference path remains available without NVIDIA SDK dependencies. From a
configured developer PowerShell:

```powershell
./tools/start-native-demo.ps1
```

The web UI must display `unknown` before authentication and the runtime-reported
reference mode, input/output dimensions, and scale after authentication.
