# TCW-005R implementation plan

Baseline: `1a5ee319a0586a73d0262b07a6368b3b12d63eb5`, clean main.

1. Verify the TCW-002/003 cached pinned Dawn checkout using their CMake caches; reuse its dependencies and build RelWithDebInfo libraries. Add explicit, idempotent bootstrap tooling.
2. Extract the accepted cube renderer into a shared library; preserve its legacy capture entry point while adding the documented Three.js solid-cyan scene contract and same-device Win32/DXGI presentation.
3. Move socket I/O and GPU work outside receiver locks. ACK only successfully submitted frames; freeze drop counts when dequeuing. Explicit test-only harness mode.
4. Add Release lifecycle/failure tests and independent Three.js matrix/corner parity; direct native-render integration, reconnect, resize and GPU validation.
5. Commit implementation, validate a clean checkout, run 600 seconds of real rendering, capture safe evidence and prepare human instructions. Commit evidence separately and push; stop for review.

The earlier missing-Dawn classification was incorrect: TCW-003's local CMake cache identifies an existing clean checkout at the exact required commit. Local paths will not be published in new evidence.
