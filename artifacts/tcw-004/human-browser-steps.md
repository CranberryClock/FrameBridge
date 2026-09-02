# Human Chrome acceptance checklist

Working directory: D:\FrameBridge. First run corepack pnpm -r build.

1. Launch the page: corepack pnpm --filter @framebridge/demo-web exec vite --host 127.0.0.1 --port 5173 --strictPort
2. In another terminal launch the harness: corepack pnpm --filter @framebridge/protocol dev
3. Open http://127.0.0.1:5173 in local Chrome. Enter the ephemeral port and 48-hex-character session token shown by the local developer launch. The token is runtime-only: never include it in a URL or saved evidence.
4. Click Connect mirror. Verify authenticated, backend test-harness, positive session generation, increasing sent/accepted frames and an empty protocol error field. The token field clears after successful authentication.
5. Click 800 × 450, then 640 × 360. Observe exact dimensions and advancing current resize generation. Allow acknowledgement to catch up; current and acknowledged resize generations must match.
6. Note browser frame and session generation; disconnect. The visible cube and browser frame must keep advancing. Reenter the runtime token and reconnect. Session generation must change, and the bridge must acknowledge the current frame, above the pre-disconnect frame. It must not restart at 1.
7. Repeat on a 120/144 Hz display. Verify no duplicate-frame protocol error, increasing accepted frames, bounded outstanding count, and continuing cube animation.
8. Capture a screenshot or short recording showing the exact MIRROR SPIKE — NOT THREE BACKEND label, connection/authentication, session generation, browser/sent/accepted frames, current/acknowledged resize generation, dimensions, dropped count, outstanding count, and last protocol error. Confirm the password/token field is EMPTY before capture.
9. Record observed start/end time, Chrome version, display refresh setting, pre/post reconnect generation and frame, both resize results, and any error. Store only redacted evidence.

TCW-PROTO-003 and TCW-CONT-001 remain HUMAN_REQUIRED until this checklist is executed by the human driver and reviewed. Automated Node acknowledgements alone do not satisfy these gates.
