# StopMate device companion

This app shows what the ESP32 actually announces for its selected stop, initially
**0440 — The Standard Apartments**. It does not fetch a separate RTS schedule or invent preview arrivals.
The firmware reports the route, destination, and exact minute value handed to
audio playback after validating its audio files. Started, completed, and failed
playback events update the browser immediately over Server-Sent Events (SSE).
The existing “No buses available” fallback is mirrored too, including when the
firmware uses that fallback for an unsuccessful RTS lookup.

## Run locally

Requirements: Node.js 22+, Python 3, and the ESP32 connected by a USB data cable.
Upload the matching project in `firmware code/` first, then close PlatformIO
Serial Monitor and any recording downloader so the bridge can own the USB port.

From this directory:

```sh
npm ci
python3 -m pip install -r requirements.txt
npm run local
```

Open **http://127.0.0.1:3000**. Wave, press the device button, or say
**Hey StopMate**. Keep the ESP32's hotspot enabled for its RTS lookup.
The computer and ESP32 do not need to share Wi-Fi: the local connection is
**ESP32 → USB Serial → Python bridge → Express server → browser**.

The bridge automatically selects a single connected USB serial device. To select
a port or an existing Python environment with pyserial installed:

```sh
SERIAL_PORT=/dev/ttyUSB1 PYTHON=/path/to/python npm run local
```

The `/dev/serial/by-id/...` path is preferable when available because Linux can
renumber ttyUSB ports. With automatic selection, the bridge searches again after
a disconnect. It retries every three seconds without requesting announcements.
Normal startup stays silent. `npm run local -- --test-request` sends one explicit
`schedule` command after the first device event, for an end-to-end test only.

`npm run local` creates an ephemeral authentication token and shares it with the
bridge through its environment. It is never placed in browser code, command-line
arguments, firmware, or logs. The server listens on **127.0.0.1** by default.
Stop it with Ctrl+C before uploading firmware or using Serial Monitor.

## Switch stops from the website

Expand **Change stop**, use **Choose a stop**, then **Switch stop & announce**. The site sends a command
through the USB bridge, waits for the device to confirm, then displays its fresh
announcement. The active stop name and old announcements stay together until that
confirmation; once confirmed, the previous stop's announcements are cleared.
The ESP32 saves the stop in NVS, so button/wave/voice requests and restarts use it.
Startup remains silent.

The catalog contains 60 official RTS stops serving the currently supported audio
routes: **8 INBOUND (UF Health Shands)** and **13 INBOUND (Cottage Grove)**.
Other routes at those stops are not announced. Stops without supported audio
routes are deliberately unavailable. Switching requires the updated firmware, a
USB connection, and no announcement already in progress. Unconfirmed changes
time out after 90 seconds; the site always labels data with the device's reported
stop. A device reported as connected may still have lost USB recently, so the
confirmation is required even when its badge says connected.

The device reports `supportsStopSelection: true`. The bridge polls the authenticated
`GET /api/device/command` endpoint and writes `stop <four-digit-ID> <command-UUID>`.
The device validates the stop, persists it, emits `stop_selected` with `commandId`,
and requests one announcement. Retrying the same command ID during a boot only
repeats its acknowledgment, not the audio. An older firmware build keeps working
as a read-only announcement source and the site explains that it needs an update.

`stops.json` is sourced from RTS `getstops`, verified 2026-10-24. To refresh after
changing the firmware route/audio table, provide a local `include/secrets.h` and run:

```sh
python3 scripts/refresh-stops.py --firmware ../"firmware code"
```

This updates both `stops.json` and `include/supported_stops.h`. Restart the server
and upload matching firmware. When adding different routes/directions, also update
the selector's route-support description to match the audio table.

## Behavior and limits

- Each request clears the previous route list. Routes appear as the device starts
  playing them; the browser keeps all announcements from that request.
- The times are **the times spoken**, including firmware adjustments for elapsed
  lookup/playback time. They do not count down or refresh independently. Routes
  omitted by audio validation or the 60-minute speech limit are not fabricated.
- “Refresh” reads the server's latest received state; it does not request another
  device announcement. Use the wave/button/wake phrase to request a fresh schedule.
- Completed and interrupted playback are distinguished. If no audio could play,
  the website says so instead of claiming an announcement occurred.
- Heartbeats normally arrive every 10 seconds. The UI marks the device disconnected
  after 90 seconds without an event, allowing for blocking network/audio operations.
  Previous announcements retain their original timestamp when disconnected.
- State is kept in memory. Restarting the server waits for a new device request;
  it cannot recover an announcement made while the bridge was disconnected.
- The bridge retries local delivery up to three times. A failed delivery is logged;
  the next device request resynchronizes the view. A completion event contains the
  full announcement and can recover a missed start event.
- The USB bridge can relay to the local server or the Fly.io deployment. The ESP32
  still needs this computer and its USB connection; it does not contact Fly directly.

## API and event protocol

- `GET /api/state`: latest device state (`Cache-Control: no-store`).
- `GET /stop/:id`: latest state only if the ID matches the device's active stop.
- `GET /api/stops`: supported stop catalog, with names and route numbers.
- `POST /api/stop`: same-origin JSON `{ "stopId": "0441" }`; returns 202 while
  awaiting device confirmation, or 400/409 for invalid/unavailable changes.
- `GET /api/device/command`: authenticated pending stop command for the USB bridge.
- `GET /events`: SSE snapshots, plus periodic connection-state updates.
- `POST /api/device/events`: authenticated event ingest. Requires
  `Authorization: Bearer <DEVICE_INGEST_TOKEN>` and a JSON body, limited to 4 KiB.

For running the server and bridge separately, set the same `DEVICE_INGEST_TOKEN`
in both environments. `npm start` starts only the web server. Do not put that
token or an RTS API key in `public/`. Public web data contains no credentials.

Firmware lines begin with `STOPMATE_EVENT ` followed by one JSON object. Version 1
requires `deviceId`, `stopId`, `bootId`, monotonic `sequence`, `requestId`, `type`,
and `uptimeMs`. Types are `ready`, `heartbeat`, `request_started`, `announcement`,
`request_finished`, and `stop_selected`. Announcement events include `kind`, `phase`, and `text`;
arrival events also include `route`, `destination`, `minutesAway`, and `color`.
Phases are `started`, `completed`, and `failed`. Request completion has a boolean
`success`. Duplicate or older sequence numbers are ignored. New boots reset the
view; delayed events from previously seen boots are ignored.

To add routes, update the firmware's route table/audio support. This app renders
the routes supplied by the device; there is no separate hard-coded route list.

## Verification

```sh
npm test
node --check public/index.js
python3 -m py_compile scripts/serial_bridge.py
```

Tests cover arrival values and multiple routes, no-buses replacement, stale
connections, frozen timestamps, reboot/replay handling, interrupted playback,
authentication, input limits, and delivery through both HTTP and SSE.

Stop-control tests also cover pending/confirmed/late changes, old firmware, busy
and disconnected devices, duplicate acknowledgments, origin checks, and a real
Express-to-USB-pseudoterminal-to-Express control round trip.

## Fly.io deployment

Live URL: **https://stopmate-accessible-transit.fly.dev**.

The container runs only Express. The USB bridge runs on the computer attached to
the ESP32 and posts events over HTTPS to Fly.io. The ESP32 still fetches RTS using
its configured hotspot. No firmware change or RTS key on the server is needed.

To start the configured remote bridge on this computer, close Serial Monitor and
any local bridge, then run from `server/`:

```sh
npm run bridge
```

Keep that terminal open and the board connected. Stop with Ctrl+C before using
Serial Monitor. `npm run bridge -- --test-request` sends one explicit schedule
request after the first ready/heartbeat event; normal bridge startup is silent.
`npm run local` still runs an independent local dashboard instead of the Fly bridge.
Only one bridge/serial monitor should own the board at a time. If the bridge was
started in the background, stop it with `pkill -f '[s]cripts/run-bridge.js'` before
opening Serial Monitor.

The private `.env.bridge` file holds `STOPMATE_SERVER_URL` and
`DEVICE_INGEST_TOKEN`. For another computer, install the requirements and create
that file locally using the same device token configured as a Fly secret.
Do not commit this file. Public deployment and bridge endpoints require HTTPS;
HTTP is accepted only for localhost development.

Viewing announcements is public. Changing the physical device's stop requires
**Stop-control access code**, stored on this computer in
`.data/stop-control-code.txt`. Enter it in the website's password field; it is not
saved in browser storage. The independent `STOP_CONTROL_TOKEN` Fly secret
protects `POST /api/stop`; the device ingest token does not grant stop-control
access. The bridge never needs the operator code. Production startup refuses
missing secrets or a missing `PUBLIC_ORIGIN`.

Deployment files are `Dockerfile`, `.dockerignore`, and `fly.toml`. The image
includes only the server and public assets, without local credentials, firmware,
recordings, logs, or the USB bridge. The app uses one shared CPU with 256 MB RAM in
Dallas. Configure `DEVICE_INGEST_TOKEN` and `STOP_CONTROL_TOKEN` with Fly secrets,
then deploy:

```sh
fly deploy --local-only --ha=false
```

Use exactly **one machine**: announcement state and pending control commands are
in memory. Automatic stopping is disabled to preserve the live session; a deploy
or machine restart clears the dashboard until subsequent device events arrive.
Do not scale to multiple machines without adding shared state. `GET /healthz`
checks server health independently of whether a board is connected. The HTTPS
origin in `PUBLIC_ORIGIN` is used for control requests behind Fly's TLS proxy;
update it if moving to a custom domain.
