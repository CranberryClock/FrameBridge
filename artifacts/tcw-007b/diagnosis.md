# TCW-007B diagnosis handoff

The ordinary live URL reproduced the one-image stall. Dawn rendering,
reference upscale, output readback, image serialization, FrameAccepted send,
and the first 921,676-byte native-image enqueue all completed. The browser
received and submitted that image to its canvas and attempted authenticated
`ImageConsumed` feedback. The native process remained alive, but no later
inbound scene or consumption message was dispatched and only three native
frames were submitted.

This locates the current blocker after the first large IXWebSocket server send
and before subsequent inbound callback dispatch. It does not establish whether
the pinned library is blocked in socket flushing, polling, or callback progress;
a native thread stack or the remaining explicit A/B/C isolation modes is still
needed before changing libraries or architecture.

Implemented in this attempt: versioned `ImageConsumed` message, exact shared
fixture, per-session one-image credit, duplicate/correlation checks, obsolete
resize credit retirement, FPS decay, and unknown timing display. These controls
pass automated tests but do not fix the underlying dispatch stall.

Not run and therefore not claimed: render-only mode A, readback-discard mode B,
synthetic-send mode C, 60-second successful run, resize/reconnect recovery, or
visual acceptance. No token, private path, or environment value is retained.
