# TCW-004 protocol and validation contract

This is MIRROR SPIKE — NOT THREE BACKEND. The Node endpoint is loopback-only test/developer infrastructure. No native socket listener or active D3D12 renderer is claimed.

Wire version 0 has a 36-byte little-endian header: magic u32 at 0, version u16 at 4, type u16 at 6, flags u32 at 8, payload length u32 at 12, sequence u64 at 16, object ID u64 at 24, FNV-1a checksum u32 at 32. The checksum detects corruption; it provides no authentication. Token authentication is a single bounded JSON hello whose body origin and HTTP Origin both equal the launch-configured origin. One controlling client is permitted. Capabilities expose a positive u64 session generation as a decimal string.

All binary payload sizes are fixed:
| Type | Bytes | Shape |
| --- | ---: | --- |
| BeginSession / EndSession / Ping | 0 | Empty |
| Error | 4 | Server-only u32 error code |
| SetRtxMode | 1 | Only 0 accepted (RTX unavailable) |
| CreateBuffer | 8 | Reserved unused spike message: declared allocation size u64; resource ID must be nonzero |
| DestroyResource | 0 | Resource ID must exist |
| BeginFrame | 48 | Frame u64, simulation time f64, rotation X/Y f32, camera Z f32, width/height u32, resize generation u64, reserved u32 zero |
| Draw | 16 | Reserved unused spike message: draw command bytes; no rendering claim |
| EndFrame | 0 | Closes exactly the open frame |
| Resize | 16 | Width/height u32 and positive resize generation u64 |
| FrameAccepted | 40 | Generation, frame, EndFrame sequence (u64 each), cumulative dropped frames/status (u32 each), resize generation u64 |

The pinned cube uses fixed 60-degree vertical FOV, near/far 0.1/100, camera looking down -Z and the authoritative viewport aspect. These fixed camera parameters are part of this narrow version-0 contract.

After capabilities: BeginSession once, initial Resize, frames, EndSession. Before BeginSession, after EndSession, duplicate BeginSession, and client Error/FrameAccepted are rejected. Resource/resize changes occur only outside frames. Sequence increases across all messages in a session; logical frames and resize generations increase independently.

Quotas: hello 8 KiB; payload 1 MiB; WebSocket message 1 MiB + 36 bytes; 256 live resources; 64 MiB declared live bytes; 4096 draws/frame; 8192 messages/frame (including BeginFrame/EndFrame); dimensions 1..8192; queue 2 complete frames. Validation occurs before persistent state mutation.

A queued item stores the entire frame and its EndFrame sequence. Drop the oldest unprocessed complete frame on saturation. Delay defaults to zero; an injected 100 ms processor in the loopback test forces saturation. ACKs are emitted only when processing finishes. The browser correlates generation/sequence/frame/resize and retires skipped predecessors only when cumulative drop telemetry explains them.

Fixtures in packages/protocol/fixtures/canonical.tsv specify independent encoder input fields and payload bytes. TS and C++ encode those fields without decoder output and compare against the twelve shared canonical .hex files. Both decode and reject the eleven malformed files. C++ reports codec coverage; Node tests provide authentication/session/transport coverage. CreateBuffer and Draw are intentionally unused reserved spike messages: the active cube contract contains scene state, viewport/Resize, and session lifecycle only. They must not be expanded in TCW-004.

The browser uses latest-only logical-frame scheduling per animation callback. It keeps rendering and advancing simulation through disconnect. Reconnect resets only session-local sequencing and sends the current resize and full frame. Node diagnostic/soak generation uses the same canonical state and client protocol controller, with bounded catch-up (at most four complete logical frames per scheduler tick).

Soak: TCW004_DURATION_SECONDS=60 for diagnostics; unset or 1800 for acceptance. Run repeated diagnostics first. The parent creates a unique run directory, hashes source and compiled protocol files before/after, captures actual child exit code, command, versions, timestamps, and acceptance inputs. Acceptance requires committed source. Do not edit tested implementation after its acceptance run.

Memory thresholds: retained heap <=128 MiB; RSS <=256 MiB; start-to-end growth <=32 MiB heap /64 MiB RSS; least-squares positive heap trend <=1 MiB/minute after min(60 s, duration/2) warmup. RSS trend is limited to 4 MiB/minute for diagnostics shorter than 120 seconds and 1 MiB/minute for all longer runs, including acceptance. Several preserved 60-second failures showed roughly 2–3 MiB total startup RSS growth; a 120-second diagnostic then passed the unchanged 1 MiB/minute limit with only 43 KiB/minute post-warmup RSS trend. The short-only allowance avoids extrapolating startup allocations into a long-run leak claim. Every record includes the effective thresholds; no failed record is reclassified.

Samples occur on monotonic 10-second deadlines plus start/end. The worker uses --expose-gc and explicitly collects at those sample points, recording pre-GC heap and retained heap; this avoids treating GC sawtooth timing as a leak. Cadence includes this instrumentation cost. All checks affect process exit status. Short diagnostic PASS never promotes TCW-PROTO-004; it additionally requires >=1800 seconds.

CI and local builds execute the C++ test from repository root. The original create/update/draw/destroy fragmentation formulation of TCW-PROTO-003 is superseded for this narrow spike. Its replacement human mirror gate verifies authenticated loopback scene-state transmission, continued browser-authoritative simulation during disconnect, reconnect at the current logical frame, matching FrameAccepted state, both explicit resizes, an empty token field, and the applicable display-rate check. Browser gates remain HUMAN_REQUIRED. The old real-soak.json is invalid evidence, retained only in Git history; no reconstructed fields are carried forward.
