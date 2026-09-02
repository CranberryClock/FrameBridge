# TCW-004 local browser evidence

Run from `D:\FrameBridge` in separate terminals:

1. `corepack pnpm --filter @framebridge/demo-web dev -- --host 127.0.0.1 --port 5173`
2. `corepack pnpm --filter @framebridge/protocol dev`

Open `http://127.0.0.1:5173/` in Chrome. Copy the ephemeral `port` and `token` from the bridge's local console into the developer fields. Never save, screenshot, paste into a URL, or commit the token.

Expected result: the page keeps the `MIRROR SPIKE — NOT THREE BACKEND` label, shows `test-harness` after authentication, displays a changing browser logical frame and a Bridge accepted logical frame, then continues advancing after Disconnect. Reconnect with the same live browser page and confirm the accepted frame resumes at the current browser frame rather than zero. Resize the canvas/window and confirm the viewport and resize generation advance in telemetry. Repeat on a 120/144 Hz display and confirm one accepted message per advancing logical frame with no protocol error.

Capture one screenshot showing the label, connection/authentication state, browser frame, accepted frame, dropped count, and last protocol error field without the token. Record start/end times, reconnect result, resize generation, accepted/dropped totals, and any protocol errors in a redacted local note.
