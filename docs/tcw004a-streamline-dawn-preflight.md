# TCW-004A — Streamline/Dawn compatibility preflight

Status: `BLOCKED_EXTERNAL_DEPENDENCY`.

This is a bounded architecture probe. It does not initialize DLSS, render with
Streamline, replace the native renderer, or change the browser mirror.

The official Streamline 2.12 manual-hooking sequence is:

1. securely validate and load the full-path `sl.interposer.dll`;
2. call `slInit` with `eUseManualHooking`, D3D12, a valid NVIDIA application ID,
   and the DLSS feature requested;
3. create the host D3D12 device on the validated NVIDIA adapter;
4. pass that device to `slSetD3DDevice`, then query feature requirements and
   support;
5. call `slShutdown` before destroying the D3D12 objects.

The existing Dawn integration can obtain its D3D12 device through
`dawn::native::d3d12::GetD3D12Device`. That is an implementation-level Dawn
helper, not a stable public Dawn interop contract, so a future successful run
must record that limitation explicitly. TCW-004A does not promote it to a
supported public ABI.

The diagnostic target is deliberately SDK-independent at build time. NVIDIA
headers, libraries, DLLs, app IDs, and SDK archives stay outside Git. At run
time it reads `FRAMEBRIDGE_STREAMLINE_ROOT` and
`FRAMEBRIDGE_NVIDIA_APP_ID`; values are never printed. Missing or invalid
external prerequisites return exit code 3 and the exact architecture
classification `BLOCKED_EXTERNAL_DEPENDENCY`.

## Local invocation

From the repository root:

```powershell
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
& $cmake -S . -B build/tcw004a -G 'Visual Studio 17 2022' -A x64
& $cmake --build build/tcw004a --config Release
& $ctest --test-dir build/tcw004a -C Release --output-on-failure
& .\build\tcw004a\Release\framebridge-streamline-dawn-preflight.exe
```

Do not put the app ID in a committed command, URL, screenshot, log, or
evidence file. A successful SDK-backed run must additionally record the SDK
release, hashes, signature result, adapter identity/LUID, COM identity result,
Streamline result codes, requirements/support queries, and cleanup result.

References: NVIDIA-RTX Streamline `ProgrammingGuide.md`,
`ProgrammingGuideManualHooking.md`, and `ProgrammingGuideDLSS.md`, all pinned
in the TCW-004A task packet as the official integration references.
