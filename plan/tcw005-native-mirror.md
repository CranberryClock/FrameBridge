# TCW-005 implementation plan

1. Audit the accepted protocol and Dawn cube, then pin a small maintained native WebSocket dependency or stop with `BLOCKED_ARCHITECTURE` if none is available without a floating fetch.
2. Extract shared native protocol/session semantics into a C++ receiver library with strict loopback WebSocket framing, authentication, queueing, acknowledgements, and active Release tests.
3. Reuse the accepted Dawn/D3D12 cube renderer behind a render-thread handoff driven only by received scene state; add a comparison window and telemetry.
4. Update the browser capability contract for `native-dawn` while preserving browser-authoritative fallback behavior and reconnect continuity.
5. Add cross-language/native integration, parity, clean-build, human-test, and 600-second stability evidence. Every long-running result records its source commit.

Stop conditions: no unsafe partial WebSocket parser, no Node proxy in the accepted path, no native-clock animation, no unbounded queue, no DLSS/Streamline/Three Backend work, and no claim for an unrun gate.
