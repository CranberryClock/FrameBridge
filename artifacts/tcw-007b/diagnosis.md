# TCW-007B Recovery diagnosis

The one-image stall was reproduced before the recovery patch. The original 921,676-byte native image was serialized and sent, but subsequent inbound `ImageConsumed` and scene messages stopped being dispatched. A/B/C isolation separated the cause from the renderer:

- A disabled return: native Dawn rendering and presentation completed 30 frames with 30 acknowledgements and no images.
- B readback-discard: native Dawn rendering, reference upscale, GPU synchronization and 921,600-byte output readback completed 30 frames with no images.
- C synthetic return: 128x72 and 320x180 completed all 30 images; 512x288 and 640x360 reproduced the large-message stall.
- D full return: the same stall occurred with actual GPU readback, proving it was not a readback-only fault.

The demonstrated transport correction is applied through the existing top-level `third_party/ixwebsocket-limits.cmake` build-tree patch. It keeps the pinned IXWebSocket gitlink unchanged, disables server-side blocking sends, and yields after one flush write. The rebuilt 512x288 synthetic case completed 30/30 images; the rebuilt 640x360 synthetic and full cases completed 30/30 acknowledgements and 29/30 images in the bounded shutdown harness with no client or process error. The final live browser run continuously displayed native pixels, including 800x450 and return to 640x360, with no protocol error.

The TypeScript decoder had a second independent defect: it applied the 1 MiB ordinary-payload ceiling before recognizing `NativeImage`. It now selects the native-image ceiling from the message type, and a regression test covers a valid 800x450 image.

The return channel remains one-image-credit at a time. The browser sends `ImageConsumed` after display; stale image credit is retired on resize. FrameAccepted acknowledgements remain independent of image return, and the browser remains authoritative for simulation and scene state.

No tokens, private paths, browser logs, or personal screenshots are included in committed evidence. TCW-008 and DLSS/Streamline work remain out of scope.
