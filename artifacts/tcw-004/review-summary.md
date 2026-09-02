# TCW-004 focused rework handoff

Implementation: `172fc2bff20a1a9f43e309cf7fefc6e3ca6c36ba`, following protocol implementation commit `fc5904613dff9367f3ff1ab75cc36f88ab86d4a5`. No history was rewritten. The evidence commit changes documentation, artifacts, and a Git attribute preserving evidence bytes without line-ending conversion. Tested implementation files are unchanged.

## Gates

| Gate | Result |
|---|---|
| TCW-BUILD-001 | PASS |
| TCW-PROTO-001 | PASS |
| TCW-PROTO-002 | PASS |
| TCW-PROTO-003 | HUMAN_REQUIRED |
| TCW-PROTO-004 | PASS |
| TCW-CONT-001 | HUMAN_REQUIRED |

## Tests and commands

The clean checkout was `D:/FrameBridge/out/tcw004-clean-final`, without node_modules before installation. `node tools/tcw004-validate.mjs` exited 0. Exact executables, arguments, working directories, expected/observed exit codes, and log paths are in `commands.txt` and `validation/clean-172fc2bff20a/result.json`.

- Frozen install, TypeScript build/tests, Vite build, CMake configure/build, and native execution: each exit 0.
- Missing native fixture directory: exit 1, expected rejection.
- Protocol suite: 74 cases — encoder rejection 6; valid fixtures 13 (12 binary plus a supported-type coverage assertion); malformed binary 11; session 22; JSON authentication 1; scheduling 4; acknowledgement 8; capabilities 8; resize 1.
- Loopback suite: 15 cases — authentication 7; transport rejection 6; reconnect continuity 1; delayed processing/backpressure 1.
- Demo identity: 3 assertions.
- C++: 12 canonical valid binaries, 11 malformed binaries, 6 encoder rejections, 2 JSON file-presence checks. JSON authentication is tested in TypeScript, not claimed as C++ JSON parsing.
- Both encoders independently construct the same canonical binary bytes from shared field specifications. Both decoders read those exact fixture files.
- The real delayed-processor transport test queues six complete frames, drops the oldest four, and acknowledges only frames five and six.

GitHub Actions run 33644093416 completed successfully for the implementation SHA, with no failing step: https://github.com/CranberryClock/FrameBridge/actions/runs/33644093416 .

## Diagnostic and acceptance evidence

Both final 60-second diagnostics exited 0: 60.0181965 seconds at 59.9151626 Hz, and 60.0102396 seconds at 59.9397707 Hz. Earlier diagnostic failures remain unchanged under `runs/`; they are not reclassified as passing. The documented short-only RSS trend allowance is 4 MiB/minute; acceptance retains 1 MiB/minute. Heap trend, absolute memory, and total growth limits are unchanged.

`TCW004_DURATION_SECONDS=1800; node tools/tcw004-soak.mjs` exited 0. The actual PowerShell environment assignment and execution are represented in `commands.txt` as the exact command plus environment.

- Start: 2026-09-02T14:47:18.667Z. End: 2026-09-02T15:17:18.693Z.
- Monotonic measured elapsed: 1800.0237294 seconds; attempted cadence: 59.9964313 Hz.
- Target logical frame: 108000. Attempted/sent/acknowledged complete frames: 107995 each.
- Five logical frames skipped during scheduling/reconnect; zero receiver-dropped frames and zero outstanding at completion. These are different counters.
- One reconnect; measured downtime 126.0288 ms. New session generation 2 first sent and acknowledged frame 54006.
- Final authoritative and acknowledged frame both 108000; resize generation both 361.
- Zero protocol errors, invalid acknowledgements, or unexpected closes.
- 181 memory samples. Peak retained heap 7693640 bytes; peak RSS 51675136 bytes. Heap growth 992792 bytes; RSS growth 3805184 bytes.
- Post-warmup heap trend 8180.3283 bytes/minute; RSS trend 61218.8096 bytes/minute. Both pass the acceptance threshold.
- Recorded implementation was clean, all exercised source/runtime hashes remained unchanged, and every acceptance predicate evaluated true.

Raw measurements: `runs/acceptance-2026-09-02T14-47-18-590Z-fveQIb/result.json`. `real-soak.json` is only a pointer to that generated record. The invalid historical reconstructed artifact supplies no acceptance evidence.

## Scope and remaining human review

MIRROR SPIKE — NOT THREE BACKEND. The endpoint is a Node test/developer harness, not a native listener or renderer. The long soak uses the DOM-independent canonical state and shared client over real loopback WebSockets, not a human-driven Chrome session. Memory is measured with explicit sample-point GC; pre-GC heap is also recorded.

Follow `human-browser-steps.md`: verify local Chrome connection; session generation change and current-frame resynchronization on reconnect; both resize sizes and matching acknowledgements; uninterrupted cube animation while disconnected; 120/144 Hz behavior; and a screenshot with the token field empty. No human screenshot or visual gate is claimed by this handoff.

The complete implementation changed-file list is in `manifest.json`. It covers the protocol/session/client/tests, browser UI, shared fixtures, native codec tests, CI/CMake, runners, and documentation. No TCW-005, DLSS, Streamline, native presentation, extension, installer, custom Three.js backend, or Unreal work was performed. Stop for supervisor review.
