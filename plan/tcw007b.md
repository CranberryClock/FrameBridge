# TCW-007B one-image stall diagnosis

Reproduce the ordinary live URL, trace dequeue/render/readback/serialization/send
stages, isolate rendering from readback and transport, then apply only the
demonstrated fix. The return channel will carry at most one unconsumed image;
browser scene frames and native FrameAccepted messages remain independent.

Evidence must include failed runs as well as recovery. TCW-008 remains blocked
until continuously changing post-reference-upscale pixels pass review.
