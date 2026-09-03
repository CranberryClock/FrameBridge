# TCW-005R human visual review — historical result

Status: PASS. Human supervisor validated the live Chrome/native mirror.
Screenshots were not provided because the local test screenshot displayed a
personal session token. No token-bearing screenshot is included in the repository.
Automated real-Chrome connection, frame continuity,
resize acknowledgements and native GPU rendering are recorded separately.

The human supervisor completed this procedure and returned PASS. It is retained
as a historical record; no new run is required.

From the repository root on this validated machine:

```powershell
./tools/start-native-demo.ps1
```

The helper uses the ignored local Chrome configuration created during validation.
On another machine, pass `-Chrome <installed Chrome executable>` after following
the build instructions in `docs/tcw005-native-mirror.md`.

1. The script starts Vite, opens Chrome and starts the native receiver. Its console
   prints READY with an ephemeral port and a fresh 48-hex-character token.
2. Paste the port and token into the webpage, then click **Connect mirror**.
   Do not put the token in a URL or save it. The field clears after authentication.
3. Keep Chrome's cube and **FrameBridge Native Mirror** visible together. Confirm
   matching solid cyan cube motion, orientation, framing and dark background.
   The native title identifies native-dawn and the NVIDIA adapter, received/submitted
   frame, resize generation and dropped count.
4. Click **800 × 450**, wait for the acknowledged generation to match, and capture
   both windows with the native client area enlarged. Then click **640 × 360**,
   wait for its acknowledgement, and capture both windows again.
5. Note the browser frame/session generation; disconnect. Confirm browser motion
   and logical frame keep advancing. Re-enter the current console token and
   reconnect. Note the later browser/accepted frame and new session generation.
   Confirm the native cube converges to current browser motion.
6. Close the native window. Confirm the process exits cleanly and the browser cube
   continues. The helper stops the Vite process it started after native exit.

The original run validated both resize states, disconnect/reconnect continuity,
smooth motion and clean native close. A screenshot was not returned because the
local capture displayed a personal session token. Do not capture or publish that
image. The safe final shutdown JSON may be copied; never copy the READY line or
token.

Human supervisor result: PASS. Motion was smooth with no stutters, back-and-forth
motion worked, reconnect continuity was validated, and the final native close was
clean. The visual result is accepted without publishing the token-bearing capture.
