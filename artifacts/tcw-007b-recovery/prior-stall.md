# Prior failure retained for comparison

Before the recovery build-tree transport correction, the pinned server stalled after the first large native image. The 512x288 synthetic case produced 1 acknowledgement and 1 image, and the 640x360 synthetic/full cases stopped after the first image while the native process remained alive. The smaller 128x72 and 320x180 synthetic cases completed. This artifact records the diagnosed baseline without retaining its token-bearing launch log.
