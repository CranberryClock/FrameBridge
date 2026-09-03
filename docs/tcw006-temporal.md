# TCW-006 — Temporal input contract

`MIRROR SPIKE — NOT THREE BACKEND`

TCW-006 keeps the browser authoritative and adds a native temporal seam between
the complete mirrored scene state and presentation. The received viewport is the
output extent. Native render input is `round(output * renderScale)`: 320x180 for
640x360 and 400x225 for 800x450 at 0.5x. Scale 1.0 remains the regression path.
The local setting is `FRAMEBRIDGE_RENDER_SCALE`; it is not a protocol mode.

`TemporalFrameResources` owns the input color, provisional sampled depth,
provisional motion-vector and explicit output-color D3D12 resource identities,
all created on the exact D3D12 device and queue extracted from pinned Dawn. The
output resource is reused by a bounded two-size cache. The reference upscaler is
an `IUpscaler` implementation with a native D3D12 fullscreen GPU pass. It has no
Streamline, DLSS, NGX or vendor types. The existing presentation pass samples the
explicit output texture, never the low-resolution input directly.

The temporal convention is previous-to-current motion in render-pixel units. The
origin is top-left, X increases right and Y increases down. Camera motion is
included; jitter is excluded. Motion is derived from unjittered projected cube
corners and the GPU-written motion target uses the same convention. The
Streamline-facing scale is `mvecScale=(1/inputRenderWidth,1/inputRenderHeight)`;
for example `{1/320,1/180}` at 320x180. The previous state is the previous successfully
submitted native frame, not `logicalFrame-1`, so a dropped browser frame produces
the correct longer-interval vector.

Temporal history is render-owner state. Initialization, first frame, session,
dimension, resize-generation, scale and explicit reinitialization set reset.
Ordinary queue drops preserve the previous submitted state. Presentation ordinal
is independent of browser logical frame and increases only for submitted frames.

The optional jitter diagnostic uses the eight-sample Halton(2,3) sequence. Values
are render-pixel offsets in `[-0.5,0.5)`. The raster diagnostic projection receives
the offset; the stored current and previous matrices remain unjittered and motion
metadata declares `motionIncludesJitter=false`. Ordinary presentation leaves this
diagnostic disabled to avoid visible wobble without a temporal reconstruction
filter.

Depth is a non-inverted D3D12 `R32_FLOAT` render target in the normal [0,1] clip
depth convention: background clears to 1.0 and nearer cube fragments are lower.
Motion is a GPU-written `RG16_FLOAT` render target, cleared to zero on reset.
Input color and output color are `RGBA8Unorm`. These are same-device resources
and are not claimed as Streamline-compatible until
the future SDK task tests them. No native second device is created.

The native window title identifies the active path as `reference-upscale 0.5x NOT
DLSS`. Diagnostic metadata is emitted beside explicit test-only captures; normal
presentation performs no CPU readback. TCW-007 will need a separately supplied
Streamline SDK, its signed/runtime interposer binaries, the supervised NVIDIA
application ID, and the matching SDK documentation/licensing details. They are
not obtained or integrated here.
