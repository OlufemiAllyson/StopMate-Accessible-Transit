const { randomBytes } = require("node:crypto");
const { spawn } = require("node:child_process");
const path = require("node:path");

// One ephemeral token shared only by this server and its USB bridge.
process.env.DEVICE_INGEST_TOKEN ||= randomBytes(32).toString("hex");
const { startServer } = require("../index");
const server = startServer();
let bridge;
server.on("listening", () => {
  const port = server.address().port;
  const args = [path.join(__dirname, "serial_bridge.py"), "--url", `http://127.0.0.1:${port}/api/device/events`];
  if (process.env.SERIAL_PORT) args.push("--port", process.env.SERIAL_PORT);
  if (process.argv.includes("--test-request")) args.push("--test-request");
  bridge = spawn(process.env.PYTHON || "python3", args, { env: process.env, stdio: "inherit" });
  bridge.on("error", error => console.error(`USB bridge could not start: ${error.message}`));
  bridge.on("exit", code => { if (code) console.error(`USB bridge exited (${code}); the web app remains available.`); });
});
for (const signal of ["SIGINT", "SIGTERM"]) process.on(signal, () => {
  bridge?.kill("SIGTERM");
  server.close(() => process.exit(0));
  setTimeout(() => process.exit(0), 1000).unref();
});
