const { randomUUID } = require("node:crypto");
const { stops } = require("./stops.json");
const STOP_BY_ID = new Map(stops.map(stop => [stop.id, stop]));
const STOP_ID = "0440";
const DEVICE_ID = "stopmate-0440";
const TYPES = new Set(["ready", "heartbeat", "request_started", "request_finished", "announcement", "stop_selected"]);
const PHASES = new Set(["started", "completed", "failed"]);

function validateEvent(input) {
  if (!input || typeof input !== "object" || Array.isArray(input)) throw new Error("Invalid event");
  if (input.version !== 1 || !STOP_BY_ID.has(input.stopId) || input.deviceId !== DEVICE_ID ||
      typeof input.bootId !== "string" || !/^[a-f0-9]{16}$/.test(input.bootId) || !TYPES.has(input.type) ||
      !Number.isSafeInteger(input.sequence) || input.sequence < 1 ||
      !Number.isSafeInteger(input.requestId) || input.requestId < 0 ||
      !Number.isSafeInteger(input.uptimeMs) || input.uptimeMs < 0) throw new Error("Invalid event envelope");
  const event = Object.fromEntries(["version", "stopId", "deviceId", "bootId", "type", "sequence", "requestId", "uptimeMs"].map(key => [key, input[key]]));
  if (typeof input.supportsStopSelection === "boolean") event.supportsStopSelection = input.supportsStopSelection;
  if (event.type === "stop_selected") {
    if (typeof input.commandId !== "string" || !/^[a-f0-9-]{36}$/.test(input.commandId)) throw new Error("Invalid command ID");
    event.commandId = input.commandId;
  }
  if (!["ready", "heartbeat", "stop_selected"].includes(event.type) && !event.requestId) throw new Error("Missing request ID");
  if (event.type === "request_finished") {
    if (typeof input.success !== "boolean") throw new Error("Missing playback result");
    event.success = input.success;
  }
  if (event.type === "announcement") {
    if (!PHASES.has(input.phase) || !["arrival", "no_buses"].includes(input.kind) ||
        typeof input.text !== "string" || !input.text.length || input.text.length > 240) throw new Error("Invalid announcement");
    Object.assign(event, { phase: input.phase, kind: input.kind, text: input.text });
    if (event.kind === "arrival") {
      if (typeof input.route !== "string" || !/^\d{1,4}$/.test(input.route) ||
          typeof input.destination !== "string" || !input.destination.length || input.destination.length > 100 ||
          !Number.isInteger(input.minutesAway) || input.minutesAway < 0 || input.minutesAway > 60 ||
          typeof input.color !== "string" || !/^#[0-9a-f]{6}$/i.test(input.color)) throw new Error("Invalid arrival");
      Object.assign(event, { route: input.route, destination: input.destination,
        minutesAway: input.minutesAway, color: input.color });
    }
  }
  return event;
}

function createAnnouncementStore({ now = Date.now, offlineAfterMs = 90000 } = {}) {
  const serverId = randomUUID();
  const listeners = new Set();
  const retiredBoots = new Set();
  let bootId = null, sequence = 0, lastSeen = null;
  let state = { stopId: STOP_ID, stopName: "The Standard Apartments", deviceId: DEVICE_ID,
    requestId: 0, status: "waiting", revision: 0, updatedAt: null, announcements: [],
    supportsStopSelection: false, stopSwitch: null, stopSelectionError: null };
  function snapshot() {
    if (state.stopSwitch && now() >= state.stopSwitch.expiresAt) {
      state.stopSwitch = null;
      state.stopSelectionError = "The device did not confirm the stop change. Reconnect it and try again.";
      ++state.revision;
    }
    return structuredClone({ ...state, serverId, bootId, deviceConnected: lastSeen !== null && now() - lastSeen < offlineAfterMs,
      lastSeenAt: lastSeen === null ? null : new Date(lastSeen).toISOString(),
      nextBuses: state.announcements.filter(item => item.kind === "arrival") });
  }
  function ingest(raw) {
    const event = validateEvent(raw);
    if (retiredBoots.has(event.bootId)) return false;
    if (event.bootId !== bootId) {
      if (bootId) retiredBoots.add(bootId);
      if (retiredBoots.size > 32) retiredBoots.delete(retiredBoots.values().next().value);
      bootId = event.bootId;
      sequence = 0;
      state = { ...state, requestId: 0, status: "waiting", announcements: [], updatedAt: null, revision: state.revision + 1 };
    }
    if (event.sequence <= sequence) return false;
    sequence = event.sequence;
    lastSeen = now();
    state.supportsStopSelection = event.supportsStopSelection === true;
    if (state.stopId !== event.stopId) {
      state.stopSelectionError = null;
      state = { ...state, stopId: event.stopId, stopName: STOP_BY_ID.get(event.stopId).name,
        requestId: 0, status: "waiting", announcements: [], updatedAt: null, revision: state.revision + 1 };
    }
    // Any event from the target confirms the device actually adopted the stop.
    if (state.stopSwitch && event.stopId === state.stopSwitch.stopId &&
        (event.type !== "stop_selected" || event.commandId === state.stopSwitch.id)) {
      state.stopSwitch = null;
      state.stopSelectionError = null;
      ++state.revision;
    }
    if (!["ready", "heartbeat", "stop_selected"].includes(event.type) && event.requestId >= state.requestId) {
      if (event.requestId > state.requestId) {
        state.requestId = event.requestId;
        state.announcements = [];
        state.status = "checking";
      }
      if (event.type === "request_started") state.status = "checking";
      if (event.type === "announcement") {
        const id = `${bootId}:${event.requestId}:${event.kind}:${event.route || "none"}`;
        const index = state.announcements.findIndex(item => item.id === id);
        const announcement = { id, kind: event.kind, text: event.text, playback: event.phase,
          ...(event.kind === "arrival" ? { route: event.route, destination: event.destination,
            minutesAway: event.minutesAway, color: event.color } : {}) };
        if (index < 0) state.announcements.push(announcement);
        else state.announcements[index] = announcement;
        state.status = event.phase === "failed" ? "playback_error" : "playing";
      }
      if (event.type === "request_finished") {
        state.status = !event.success || state.announcements.some(item => item.playback === "failed")
          ? "playback_error" : state.announcements.length ? "complete" : "no_audio";
      }
      state.updatedAt = new Date(now()).toISOString();
      ++state.revision;
    }
    for (const listener of listeners) listener(snapshot());
    return true;
  }
  function selectStop(stopId) {
    const current = snapshot();
    if (!STOP_BY_ID.has(stopId)) throw new Error("Choose a supported RTS stop.");
    if (!current.deviceConnected) throw new Error("Connect the ESP32 by USB before switching stops.");
    if (!state.supportsStopSelection) throw new Error("Upload the firmware with stop selection support first.");
    if (state.stopSwitch) throw new Error("A stop change is already pending.");
    if (["checking", "playing"].includes(state.status)) throw new Error("Wait for the current announcement to finish.");
    if (stopId === state.stopId) throw new Error("This stop is already selected.");
    state.stopSelectionError = null;
    state.stopSwitch = { id: randomUUID(), stopId, stopName: STOP_BY_ID.get(stopId).name, expiresAt: now() + 90000 };
    ++state.revision;
    for (const listener of listeners) listener(snapshot());
    return snapshot();
  }
  function command() {
    const current = snapshot();
    return current.deviceConnected && current.supportsStopSelection ? current.stopSwitch : null;
  }
  return { snapshot, ingest, selectStop, command, subscribe(listener) { listeners.add(listener); return () => listeners.delete(listener); } };
}

module.exports = { createAnnouncementStore, validateEvent };
