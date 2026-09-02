# TCW-005 — Native Mirror Cube

TCW-005 is a scene-state mirror spike, not a Three.js backend. The browser
remains authoritative and sends the accepted protocol-v0 binary scene state
directly to the C++ loopback receiver. Node remains an oracle/test client only
and is not in the native frame path.

The native receiver owns the loopback WebSocket listener, hello authentication,
binary validation, per-session lifecycle, bounded complete-frame queue, and
FrameAccepted transmission. IXWebSocket v12.0.1 is pinned as a submodule for
RFC 6455 transport handling. nlohmann/json v3.11.3 is pinned for strict hello
parsing. The receiver uses one controlling session and a worker thread for
complete-frame processing; the queue is capped at two and drops the oldest
complete frame on saturation.

Current implementation status is partial: the direct native protocol receiver,
semantic session queue, native-dawn capability advertisement, and TypeScript
direct-client integration are implemented. The accepted TCW-003 Dawn/D3D12
renderer and comparison window are not yet wired because the pinned Dawn
checkout is not available on this machine. No native-renderer, DLSS,
Streamline, overlay, extension, installer, or general Three.js backend claim is
made. TCW-NATIVE-MIRROR-004/007/008/009 remain blocked or human-required until
that dependency and the hardware render path are available.

Thread ownership:

| Item | Owner |
| --- | --- |
| WebSocket socket callbacks | IXWebSocket connection thread |
| session and protocol state | receiver mutex, network callback |
| complete-frame queue | receiver mutex, network and processing worker |
| native processing/ack | processing worker |
| Dawn device/surface/swapchain | reserved for render worker integration |

Clean build requires initialized submodules:

```powershell
git submodule update --init --recursive
$cmake = 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake -S . -B build/tcw005 -G 'Visual Studio 17 2022' -A x64 -DUSE_ZLIB=OFF -DUSE_TLS=OFF -DIXWEBSOCKET_INSTALL=OFF
& $cmake --build build/tcw005 --config Release
```
