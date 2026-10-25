const { spawn } = require('node:child_process');
const path = require('node:path');
if (!process.env.STOPMATE_SERVER_URL || !process.env.DEVICE_INGEST_TOKEN) {
  console.error('Set STOPMATE_SERVER_URL and DEVICE_INGEST_TOKEN in .env.bridge. See README.md.');
  process.exit(1);
}
const base = new URL(process.env.STOPMATE_SERVER_URL);
if (base.pathname !== '/' || base.search || base.hash || base.username || base.password) {
  console.error('STOPMATE_SERVER_URL must contain only the server origin.');
  process.exit(1);
}
const args = [path.join(__dirname, 'serial_bridge.py'), '--url', `${base.origin}/api/device/events`];
if (process.env.SERIAL_PORT) args.push('--port', process.env.SERIAL_PORT);
if (process.argv.includes('--test-request')) args.push('--test-request');
console.log(`StopMate USB bridge → ${base.origin}`);
const bridge = spawn(process.env.PYTHON || 'python3', args, { env: process.env, stdio: 'inherit' });
bridge.on('error', error => { console.error(error.message); process.exitCode = 1; });
bridge.on('exit', code => { process.exitCode = code || 0; });
for (const signal of ['SIGINT', 'SIGTERM']) process.on(signal, () => bridge.kill(signal));
