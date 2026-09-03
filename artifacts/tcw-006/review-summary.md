# TCW-006 evidence

Implementation commit: 6ce7e74fbd348ee2d0c9aa00061956fae09b8852

The clean Release validation and real wall-clock 600-second acceptance run passed. Browser state remained authoritative through disconnect/reconnect; the native receiver accepted the current logical frame and resize generation without restarting simulation. The native path is explicitly a reference-upscale seam, not DLSS.

The temporal resource contract and native output presentation are validated on the pinned Dawn/D3D12 path. Depth and motion remain provisional same-device resource roles for the future vendor integration task; no Streamline or DLSS code was implemented or executed.

The raw machine result is the unmodified stdout capture in [real-soak.json](real-soak.json).
