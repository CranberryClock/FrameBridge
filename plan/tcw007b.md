# TCW-007B one-image stall diagnosis

Reproduce the ordinary live URL, trace dequeue/render/readback/serialization/send
stages, isolate rendering from readback and transport, then apply only the
demonstrated fix. The return channel will carry at most one unconsumed image;
browser scene frames and native FrameAccepted messages remain independent.

Evidence includes the original failure and the recovery runs. TCW-008 remains blocked
until separately approved.

## Recovery record

- A disabled return: 30/30 acknowledgements, 30 submissions, 30 no-image frames, clean exit.
- B readback-discard: 30/30 acknowledgements, 30 submissions, 921,600 readback bytes per frame, clean exit.
- C synthetic return: the original 590 KiB and 922 KiB cases stalled after one image; after the transport patch, 640x360 returned 29/30 images in the bounded harness and 512x288 returned 30/30.
- D full return: 30/30 acknowledgements, 30 submissions, 29 returned images in the bounded harness, no client error, clean exit; the final browser run returned continuously at 640x360, 800x450, and 640x360 again.
- Root cause: the pinned IXWebSocket server transport used blocking sends. The existing top-level build-tree patch now disables server-side blocking sends and yields after one flush write, allowing inbound ImageConsumed and scene messages to be dispatched while large native-image frames are being transmitted.
- The browser decoder also now applies the native-image quota before the ordinary-payload quota, which is required for valid 800x450 RGBA8 images.
