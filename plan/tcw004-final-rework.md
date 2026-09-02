# TCW-004 final focused rework

Approved base: 97a5367155d5f580e863561322eab712bd2ddb32. Preserve history.

1. Unify codec schemas and independent fixture inputs for all twelve message types.
2. Enforce authenticated binary lifecycle and validate before mutation. Correlate complete-frame acknowledgements using session generation, EndFrame sequence, logical frame, and resize generation.
3. Share a DOM-independent client controller between browser, loopback tests, and soak. Assert the supervisor rejection matrix, refresh schedules, reconnect, resize, and transport backpressure.
4. Replace stale soak evidence with uniquely named generated runs. A parent runner records actual child exit codes; hash source and compiled protocol files. Validate cadence, acknowledgements, reconnect, convergence, memory bounds and trends.
5. Run clean build, native executable, repeated 60-second diagnostics; inspect CI. Commit implementation before the 1800-second run; commit evidence separately. Browser gates remain HUMAN_REQUIRED.

Decisions: use decimal strings for uint64 JSON generation; drop oldest complete queued frame; keep default processing delay zero with injectable delayed processing for tests. Use bounded deadline catch-up for diagnostics/soak (at most four logical states per tick), latest-only frame selection for browser callbacks. Memory thresholds: heap <=128 MiB, RSS <=256 MiB, growth <=32/64 MiB, positive trend <=1 MiB/min after warmup. No NVIDIA or later task work.
