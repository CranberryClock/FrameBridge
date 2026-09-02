# TCW-005R review summary

Automated gates PASS; TCW-NATIVE-MIRROR-009 remains HUMAN_REQUIRED.
Implementation: `011174ca607ae6c24572f55a4703ea1a7c763564`.
All final hardware tests and the acceptance run use that clean detached checkout.

The prior missing-Dawn claim was wrong: TCW-002/003's cache contained the clean
required pin `34b1fca4d6c3d7025a2231d82b4fc719ca57fd71`. Its RelWithDebInfo static
libraries were built and used by the clean Release application build. The shared
TCW-003 renderer now consumes the canonical browser state and presents in a
separate Win32 window. It verifies the NVIDIA device/LUID and native queue device
identity. Default READY/native-dawn requires successful GPU/window initialization.
Explicit protocol-only infrastructure reports test-harness.

The native receiver assembles a bounded queue of two complete frames; GPU work,
socket operations and fence waits occur outside the receiver mutex. ACK follows
successful rendering, same-device GPU presentation and validation. Dropped counts
are frozen at dequeue to preserve exact cumulative ACK correlation. The browser
remains authoritative; Node never proxies webpage frames.

## Measured acceptance

- Clean frozen pnpm 10.14 install, TypeScript build/tests and Vite build: exit 0.
- Clean native Release build and CTest: exit 0, 5/5 cases passed.
- Active parity/render-gate checks: 709; native session checks: 5.
- Deliberate failed Release check: exit 1, proving checks are active.
- Direct native integration: 14 scenarios passed; delayed queue dropped 78
  complete frames under controlled processing delay, with correlated ACKs.
- Real Chrome 152.0.7977.75 connected directly to C++; browser frame advanced
  30 -> 54 while disconnected -> 74 after reconnect, session 4 -> 5.
  Both 800x450 and 640x360 resize acknowledgements repeated twice.
- Independent actual Three.js r185 oracle: frames 60/120/180, both dimensions,
  model/view/projection/MVP and all eight projected corners. Double tolerance
  1e-12; f32 projected-corner tolerance 2e-6. Fixture regeneration is unchanged.
- Real stability: 600.0136082 seconds; 35,907 submitted and acknowledged frames;
  26 dropped complete frames; 3 reconnects; 119 resizes; queue maximum 2.
- Peak private memory 223,039,488 bytes; peak working set 200,368,128 bytes.
  Private change after warmup -237,568 bytes. The unchanged bounds are 512 MiB
  peak and 64 MiB post-warmup growth. Exactly two target and swapchain allocations.
- Zero Dawn validation, D3D12 and DXGI warning/error messages; no device loss;
  clean shutdown, all owned threads joined. Raw stability stderr is empty.
- Legacy TCW-003 capture still hashes to
  `86a7cc833484d175765763909cc6fbd639647df72318fbe8eaf6b3d288ee6b01`
  after 100 resize cycles. Accepted TCW-001 through TCW-004 evidence is unchanged.
- GitHub Actions run 33686292843 passed on the final implementation source.

The first 600-second attempt at 3704615 failed a memory-growth bound and did not
retain measurements. The second at 35892e2 was intentionally stopped after a
window inspection exposed incorrect shrink scaling. Neither counts as acceptance.
Their failure records are retained. The final full-buffer GPU blit fixes the
presentation scaling while keeping resize storage bounded; no CPU presentation
readback is used. The Dawn cube, not the blit, computes scene geometry.

## Evidence and remaining human gate

Eleven PNGs: three browser canvas captures, three native render-target captures,
four browser UI captures and one actual native-window capture after 640->800->640.
The native PNG readbacks are optional evidence, not the presentation path.
Computer-use inspection verified the real window was centered and correctly
scaled after shrinking; Alt+F4 produced clean joined shutdown. The parity pairs
agree in geometry, orientation, framing, cyan color and dark background. Browser
canvas screenshots include a one-pixel CSS border; pixel-perfect equality is not
claimed. All UI captures have cleared token fields and no unrelated desktop data.

The user must still judge simultaneous live motion and return the two size-state
screenshots and continuity observations listed in human-browser-steps.md. Run
`./tools/start-native-demo.ps1` on this validated machine. Do not count automated
screenshots as a human PASS.

Known limits: narrow solid-cube scene only; separate native window, not overlay;
machine-specific NVIDIA adapter identity; point-sampled GPU presentation can differ
at edge pixels from the browser; Vite's large Three.js bundle advisory is nonfatal.
No DLSS, Streamline runtime, installer, extension, general Three backend or TCW-006.
Stop for supervisor review.
