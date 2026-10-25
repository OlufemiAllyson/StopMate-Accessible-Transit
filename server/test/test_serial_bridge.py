import http.server
import json
import os
from pathlib import Path
import pty
import selectors
import subprocess
import sys
import threading
import time
import unittest
import urllib.request


class SerialBridgeTest(unittest.TestCase):
    def test_split_events_and_transient_server_failure(self):
        received = []
        delivered = threading.Event()

        class Handler(http.server.BaseHTTPRequestHandler):
            def do_POST(self):
                body = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
                received.append((self.headers.get('Authorization'), body))
                self.send_response(503 if len(received) == 1 else 200)
                self.end_headers()
                self.wfile.write(b'{}')
                if len(received) == 2:
                    delivered.set()

            def log_message(self, *args):
                pass

        server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        master, slave = pty.openpty()
        script = Path(__file__).resolve().parents[1] / 'scripts' / 'serial_bridge.py'
        process = subprocess.Popen([sys.executable, str(script), '--port', os.ttyname(slave),
            '--url', f'http://127.0.0.1:{server.server_port}/api/device/events'],
            env={**os.environ, 'DEVICE_INGEST_TOKEN': 'bridge-test-only'},
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(process.stdout, selectors.EVENT_READ)
                self.assertTrue(selector.select(5), 'Bridge did not open the serial port')
                self.assertIn(b'USB: listening', process.stdout.readline())
            event = {'version': 1, 'type': 'heartbeat', 'sequence': 1}
            payload = b'STOPMATE_EVENT ' + json.dumps(event).encode() + b'\n'
            os.write(master, b'ordinary log\nSTOPMATE_EVENT {bad}\nSTOPMATE_EVENT []\n' + payload[:25])
            time.sleep(0.3)  # Longer than the serial timeout: exercise partial-line buffering.
            os.write(master, payload[25:])
            self.assertTrue(delivered.wait(5), 'Event was not delivered/retried')
            self.assertEqual(received, [('Bearer bridge-test-only', event)] * 2)
        finally:
            process.terminate()
            output, _ = process.communicate(timeout=5)
            self.assertNotIn(b'bridge-test-only', output)
            os.close(master)
            os.close(slave)
            server.shutdown()
            server.server_close()


    def test_stop_control_round_trip_with_real_server(self):
        root = Path(__file__).resolve().parents[1]
        env = {**os.environ, 'DEVICE_INGEST_TOKEN': 'bridge-control-test-only'}
        app = subprocess.Popen(['node', '-e',
            "const {createApp}=require('./index'); const s=createApp().listen(0,'127.0.0.1',()=>console.log(s.address().port));"],
            cwd=root, env=env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        bridge = None
        master, slave = pty.openpty()
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(app.stdout, selectors.EVENT_READ)
                self.assertTrue(selector.select(5))
                port = int(app.stdout.readline())
            url = f'http://127.0.0.1:{port}'
            bridge = subprocess.Popen([sys.executable, str(root / 'scripts/serial_bridge.py'),
                '--port', os.ttyname(slave), '--url', url + '/api/device/events'],
                env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            with selectors.DefaultSelector() as selector:
                selector.register(bridge.stdout, selectors.EVENT_READ)
                self.assertTrue(selector.select(5))
                self.assertIn(b'USB: listening', bridge.stdout.readline())
            def emit(sequence, event_type, **fields):
                event = dict(version=1, deviceId='stopmate-0440', stopId='0440', bootId='0123456789abcdef',
                    sequence=sequence, requestId=0, uptimeMs=sequence*1000, type=event_type, supportsStopSelection=True)
                event.update(fields)
                os.write(master, b'STOPMATE_EVENT ' + json.dumps(event).encode() + b'\n')
            def state():
                with urllib.request.urlopen(url + '/api/state', timeout=2) as response:
                    return json.load(response)
            emit(1, 'ready')
            deadline = time.monotonic() + 5
            while not state()['deviceConnected'] and time.monotonic() < deadline:
                time.sleep(0.05)
            request = urllib.request.Request(url + '/api/stop', data=b'{"stopId":"0441"}',
                headers={'Content-Type': 'application/json'})
            with urllib.request.urlopen(request, timeout=2) as response:
                pending = json.load(response)
            self.assertEqual(pending['stopId'], '0440')
            with selectors.DefaultSelector() as selector:
                selector.register(master, selectors.EVENT_READ)
                self.assertTrue(selector.select(5), 'Stop command never reached USB')
                command = os.read(master, 1024).decode().strip()
            self.assertEqual(command, f"stop 0441 {pending['stopSwitch']['id']}")
            emit(2, 'stop_selected', stopId='0441', commandId=pending['stopSwitch']['id'])
            emit(3, 'request_started', stopId='0441', requestId=1)
            emit(4, 'announcement', stopId='0441', requestId=1, kind='no_buses',
                phase='completed', text='No buses available.')
            emit(5, 'request_finished', stopId='0441', requestId=1, success=True)
            deadline = time.monotonic() + 5
            while state()['status'] != 'complete' and time.monotonic() < deadline:
                time.sleep(0.05)
            final = state()
            self.assertEqual(final['stopId'], '0441')
            self.assertEqual(final['stopName'], 'Tigert Hall')
            self.assertIsNone(final['stopSwitch'])
            self.assertEqual(final['announcements'][0]['text'], 'No buses available.')
        finally:
            if bridge:
                bridge.terminate()
                output, _ = bridge.communicate(timeout=5)
                self.assertNotIn(b'bridge-control-test-only', output)
            app.terminate()
            app.communicate(timeout=5)
            os.close(master)
            os.close(slave)


if __name__ == '__main__':
    unittest.main()
