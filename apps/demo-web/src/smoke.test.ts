import assert from "node:assert/strict";
import { browserFallbackStatus, frameBridgeIdentity } from "./index.js";
assert.equal(frameBridgeIdentity.system, "FrameBridge");
assert.equal(frameBridgeIdentity.demo, "The Cube Works");
assert.equal(browserFallbackStatus(), "BROWSER_WEBGPU_FALLBACK_READY");
console.log("TCW-BUILD-001 PASS");
