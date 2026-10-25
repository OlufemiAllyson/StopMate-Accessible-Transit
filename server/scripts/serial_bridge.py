"""Relay versioned ESP32 announcement events to a local or hosted web server."""
import argparse
import json
import os
import re
import sys
import time
import urllib.error
import urllib.request
import urllib.parse

import serial
from serial.tools import list_ports

PREFIX = b"STOPMATE_EVENT "


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port")
    parser.add_argument("--url", default="http://127.0.0.1:3000/api/device/events")
    parser.add_argument("--test-request", action="store_true", help="Send one explicit schedule request for an integration test")
    args = parser.parse_args()
    target = urllib.parse.urlparse(args.url)
    if (target.scheme != "https" and not (target.scheme == "http" and target.hostname in ("127.0.0.1", "localhost", "::1"))) or target.username or target.password or target.query or target.fragment or target.path != "/api/device/events":
        parser.error("Use an HTTPS /api/device/events URL (HTTP is allowed only on localhost)")
    token = os.environ.get("DEVICE_INGEST_TOKEN")
    if not token:
        parser.error("DEVICE_INGEST_TOKEN is required (npm run local sets it automatically)")
    requested = False
    while True:
        port_name = args.port
        if not port_name:
            candidates = [port.device for port in list_ports.comports() if port.vid is not None]
            if len(candidates) != 1:
                print("USB: connect one board, or set SERIAL_PORT to choose it.", flush=True)
                time.sleep(3)
                continue
            port_name = candidates[0]
        try:
            port = serial.Serial(port=None, baudrate=115200, timeout=0.2, exclusive=True)
            port.dtr = False
            port.rts = False
            port.port = port_name
            with port:
                print(f"USB: listening on {port_name}", flush=True)
                pending = b""
                next_command_poll = 0
                last_command_id = None
                last_command_sent = 0
                supports_selection = False
                while True:
                    now = time.monotonic()
                    if supports_selection and now >= next_command_poll:
                        next_command_poll = now + 1.0
                        control = urllib.request.Request(args.url.rsplit("/", 1)[0] + "/command",
                            headers={"Authorization": f"Bearer {token}"})
                        try:
                            with urllib.request.urlopen(control, timeout=2) as response:
                                command = json.load(response).get("command")
                            if (isinstance(command, dict) and
                                re.fullmatch(r"[0-9]{4}", str(command.get("stopId", ""))) and
                                re.fullmatch(r"[a-f0-9-]{36}", str(command.get("id", ""))) and
                                (command["id"] != last_command_id or now - last_command_sent >= 3)):
                                port.write(f"stop {command['stopId']} {command['id']}\n".encode())
                                last_command_id, last_command_sent = command["id"], now
                                print(f"USB: requested stop {command['stopId']}.", flush=True)
                        except (urllib.error.URLError, TimeoutError, OSError, ValueError):
                            pass  # Keep forwarding audio events; retry control on the next poll.
                    chunk = port.read_until(b"\n", size=2048)
                    if not chunk:
                        continue
                    pending += chunk
                    if len(pending) > 4096:
                        pending = b""
                        continue
                    if not pending.endswith(b"\n"):
                        continue
                    line, pending = pending.strip(), b""
                    marker = line.find(PREFIX)
                    if marker < 0:
                        print(line.decode(errors="replace"), flush=True)
                        continue
                    try:
                        event = json.loads(line[marker + len(PREFIX):])
                        if not isinstance(event, dict):
                            raise ValueError("Event must be an object")
                        supports_selection = event.get("supportsStopSelection") is True
                        body = json.dumps(event).encode()
                    except (ValueError, UnicodeError):
                        print("USB: discarded a malformed event.", flush=True)
                        continue
                    request = urllib.request.Request(args.url, data=body, headers={
                        "Content-Type": "application/json", "Authorization": f"Bearer {token}"})
                    for attempt in range(3):
                        try:
                            with urllib.request.urlopen(request, timeout=5) as response:
                                response.read()
                            if event.get("type") != "heartbeat":
                                print(f"WEB: {event.get('type')} {event.get('phase', '')}", flush=True)
                            break
                        except urllib.error.HTTPError as error:
                            if error.code < 500 or attempt == 2:
                                print(f"WEB: event rejected (HTTP {error.code}).", flush=True)
                                break
                            time.sleep(0.2)
                        except (urllib.error.URLError, TimeoutError, OSError):
                            if attempt == 2:
                                print("WEB: server unavailable; event could not be delivered.", flush=True)
                            else:
                                time.sleep(0.2)
                    if args.test_request and not requested and event.get("type") in ("ready", "heartbeat"):
                        port.write(b"schedule\n")
                        requested = True
                        print("TEST: requested one device announcement.", flush=True)
        except (serial.SerialException, OSError) as error:
            print(f"USB unavailable: {error}. Retrying in 3 seconds.", flush=True)
            time.sleep(3)


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        sys.exit(0)
