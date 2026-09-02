import { BridgeServer } from "./server.js";

const server = new BridgeServer();
const port = await server.start();
console.log(`FRAMEBRIDGE_BRIDGE_READY host=127.0.0.1 port=${port} token=${server.token} mode=manual-developer`);
const shutdown = async (): Promise<void> => { await server.close(); process.exit(0); };
process.once("SIGINT", shutdown);
process.once("SIGTERM", shutdown);
