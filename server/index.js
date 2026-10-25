const express = require("express");
const path = require("node:path");
const { timingSafeEqual } = require("node:crypto");
const { stops } = require("./stops.json");
const { createAnnouncementStore } = require("./announcement-store");

function createApp({ token = process.env.DEVICE_INGEST_TOKEN, controlToken = process.env.STOP_CONTROL_TOKEN,
  publicOrigin = process.env.PUBLIC_ORIGIN, store = createAnnouncementStore() } = {}) {
  const app = express();
  app.disable("x-powered-by");
  app.use(express.json({ limit: "4kb" }));
  app.get("/healthz", (req, res) => res.json({ ok: true }));
  function matchesSecret(supplied, expected) {
    const a = Buffer.from(supplied || ""), b = Buffer.from(expected || "");
    return a.length === b.length && timingSafeEqual(a, b);
  }
  app.get("/stop/:id", (req, res) => {
    res.set("Cache-Control", "no-store");
    if (req.params.id !== store.snapshot().stopId) return res.status(404).json({ error: "Stop not found" });
    res.json(store.snapshot());
  });
  function deviceAuth(req, res, next) {
    if (!token) return res.status(503).json({ error: "Device ingest is not configured" });
    const supplied = Buffer.from(req.get("authorization") || "");
    const expected = Buffer.from(`Bearer ${token}`);
    if (supplied.length !== expected.length || !timingSafeEqual(supplied, expected))
      return res.status(401).json({ error: "Unauthorized" });
    next();
  }
  app.get("/api/stops", (req, res) => res.json({ stops, direction: "INBOUND", routes: ["8", "13"], controlsRequireKey: Boolean(controlToken) }));
  app.get("/api/state", (req, res) => {
    res.set("Cache-Control", "no-store").json(store.snapshot());
  });
  app.post("/api/stop", (req, res) => {
    // Fly terminates TLS before forwarding HTTP. Compare with the configured
    // public origin instead of trusting client-supplied forwarding headers.
    const origin = req.get("origin");
    if (origin && origin !== (publicOrigin || `${req.protocol}://${req.get("host")}`))
      return res.status(403).json({ error: "Use the StopMate website to switch stops." });
    if (controlToken && !matchesSecret(req.get("x-stopmate-control"), controlToken))
      return res.status(401).json({ error: "Enter the stop-control access code to switch stops." });
    if (!req.is("application/json") || !stops.some(stop => stop.id === req.body?.stopId))
      return res.status(400).json({ error: "Choose a supported RTS stop." });
    try { res.status(202).json(store.selectStop(req.body.stopId)); }
    catch (error) { res.status(409).json({ error: error.message }); }
  });
  app.get("/api/device/command", deviceAuth, (req, res) => {
    res.set("Cache-Control", "no-store").json({ command: store.command() });
  });
  app.post("/api/device/events", deviceAuth, (req, res) => {
    try {
      res.json({ accepted: store.ingest(req.body) });
    } catch {
      res.status(400).json({ error: "Invalid device event" });
    }
  });
  app.get("/events", (req, res) => {
    res.set({ "Content-Type": "text/event-stream", "Cache-Control": "no-cache, no-transform", Connection: "keep-alive" });
    res.flushHeaders();
    const send = state => res.write(`data: ${JSON.stringify(state)}\n\n`);
    send(store.snapshot());
    const unsubscribe = store.subscribe(send);
    const timer = setInterval(() => send(store.snapshot()), 15000);
    req.on("close", () => { clearInterval(timer); unsubscribe(); });
  });
  app.use(express.static(path.join(__dirname, "public")));
  app.use((error, req, res, next) => {
    if (res.headersSent) return next(error);
    res.status(error.status === 413 ? 413 : 400).json({ error: "Invalid request" });
  });
  return app;
}

function startServer() {
  if (process.env.NODE_ENV === "production" &&
      (!process.env.DEVICE_INGEST_TOKEN || !process.env.STOP_CONTROL_TOKEN || !process.env.PUBLIC_ORIGIN))
    throw new Error("Production requires DEVICE_INGEST_TOKEN, STOP_CONTROL_TOKEN, and PUBLIC_ORIGIN");
  const port = Number(process.env.PORT || 3000);
  const host = process.env.HOST || "127.0.0.1";
  return createApp().listen(port, host, () => console.log(`StopMate: http://${host}:${port}`));
}
if (require.main === module) startServer();
module.exports = { createApp, startServer };
