# TCW-005R implementation plan

Baseline: `1a5ee319a0586a73d0262b07a6368b3b12d63eb5`, clean main.

1. Verify the TCW-002/003 cached pinned Dawn checkout using their CMake caches; reuse its dependencies and build RelWithDebInfo libraries. Add explicit, idempotent bootstrap tooling.
2. Extract the accepted cube renderer into a shared library; preserve its legacy capture entry point while adding the documented Three.js solid-cyan scene contract and same-device Win32/DXGI presentation.
3. Move socket I/O and GPU work outside receiver locks. ACK only successfully submitted frames; freeze drop counts when dequeuing. Explicit test-only harness mode.
4. Add Release lifecycle/failure tests and independent Three.js matrix/corner parity; direct native-render integration, reconnect, resize and GPU validation.
5. Commit implementation, validate a clean checkout, run 600 seconds of real rendering, capture safe evidence and prepare human instructions. Commit evidence separately and push; stop for review.

The earlier missing-Dawn classification was incorrect: TCW-003's local CMake cache identifies an existing clean checkout at the exact required commit. Local paths will not be published in new evidence.

The first 600-second run at 3704615 failed the private-memory growth bound. It is
not acceptance evidence. Follow-up work reuses texture views, drains bounded DXGI
diagnostics and eliminates unused per-resize CPU readback allocation. Capture
storage is now lazy. Repeat the same unchanged memory limits after these fixes.
Fixed-size diagnostics stayed flat while repeated resize allocation grew. The
final resize design bounds the target pool at two entries and grows swapchain
capacity only as needed. A second run at 35892e2 was intentionally stopped after
window inspection found incorrect scaling on shrink with the DXGI source-size
approach. A same-device fullscreen texture blit now fills the entire buffer;
DXGI stretch maps that normalized image to the exact native client dimensions.
The cube remains Dawn-rendered. The acceptance run also checks target/swapchain
allocation counts, alongside the unchanged memory limits. Neither earlier run
counts as acceptance evidence.
