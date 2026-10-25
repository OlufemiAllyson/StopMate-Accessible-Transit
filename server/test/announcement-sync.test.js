const { test } = require('node:test');
const assert = require('node:assert/strict');
const { once } = require('node:events');
const { createApp } = require('../index');
const { createAnnouncementStore } = require('../announcement-store');

function event(sequence, type, extra = {}) {
  return { version: 1, deviceId: 'stopmate-0440', stopId: '0440', bootId: '0123456789abcdef',
    sequence, requestId: 1, type, uptimeMs: sequence * 1000, ...extra };
}
function arrival(sequence, phase, route = '8', minutesAway = 6) {
  const destination = route === '8' ? 'UF Health Shands' : 'Cottage Grove';
  return event(sequence, 'announcement', { phase, kind: 'arrival', route, minutesAway, destination,
    color: route === '8' ? '#ff7b00' : '#49535a',
    text: `Bus ${route} for route ${destination} will arrive in ${minutesAway} minutes.` });
}

test('website uses the spoken minutes, accumulates both routes, and never counts them down', () => {
  let now = 100000;
  const store = createAnnouncementStore({ now: () => now });
  store.ingest(event(1, 'request_started'));
  store.ingest(arrival(2, 'started'));
  assert.equal(store.snapshot().status, 'playing');
  assert.equal(store.snapshot().nextBuses[0].minutesAway, 6);
  store.ingest(arrival(3, 'completed'));
  store.ingest(arrival(4, 'started', '13', 24));
  store.ingest(arrival(5, 'completed', '13', 24));
  store.ingest(event(6, 'request_finished', { success: true }));
  const snapshot = store.snapshot();
  assert.deepEqual(snapshot.nextBuses.map(bus => [bus.route, bus.minutesAway, bus.playback]),
    [['8', 6, 'completed'], ['13', 24, 'completed']]);
  now += 120000;
  assert.equal(store.snapshot().deviceConnected, false);
  assert.equal(store.snapshot().nextBuses[0].minutesAway, 6);
  assert.equal(store.snapshot().updatedAt, snapshot.updatedAt);
  store.ingest(event(7, 'heartbeat'));
  assert.equal(store.snapshot().deviceConnected, true);
  assert.equal(store.snapshot().updatedAt, snapshot.updatedAt);
  snapshot.nextBuses[0].minutesAway = 99;
  assert.equal(store.snapshot().nextBuses[0].minutesAway, 6);
});

test('no-buses fallback replaces previous routes and preserves exact device text', () => {
  const store = createAnnouncementStore();
  store.ingest(arrival(1, 'completed'));
  store.ingest(event(2, 'request_started', { requestId: 2 }));
  assert.deepEqual(store.snapshot().nextBuses, []);
  const fallback = { requestId: 2, kind: 'no_buses', text: 'No buses available.' };
  store.ingest(event(3, 'announcement', { ...fallback, phase: 'started' }));
  store.ingest(event(4, 'announcement', { ...fallback, phase: 'completed' }));
  store.ingest(event(5, 'request_finished', { requestId: 2, success: true }));
  assert.equal(store.snapshot().status, 'complete');
  assert.equal(store.snapshot().announcements[0].text, 'No buses available.');
  assert.deepEqual(store.snapshot().nextBuses, []);
});

test('duplicates, older events, and old boots cannot roll the website back', () => {
  const store = createAnnouncementStore();
  store.ingest(arrival(2, 'completed'));
  assert.equal(store.ingest(arrival(2, 'completed')), false);
  assert.equal(store.ingest(arrival(1, 'started')), false);
  assert.equal(store.snapshot().announcements.length, 1);
  store.ingest(event(1, 'ready', { bootId: 'abcdef0123456789', requestId: 0 }));
  assert.equal(store.snapshot().status, 'waiting');
  assert.equal(store.ingest(arrival(3, 'completed')), false);
  assert.deepEqual(store.snapshot().announcements, []);
});

test('a completion event can recover a missing start; failed or absent audio is explicit', () => {
  const store = createAnnouncementStore();
  store.ingest(arrival(3, 'completed'));
  assert.equal(store.snapshot().announcements[0].playback, 'completed');
  store.ingest(arrival(4, 'failed', '13'));
  store.ingest(event(5, 'request_finished', { success: false }));
  assert.equal(store.snapshot().status, 'playback_error');
  store.ingest(event(6, 'request_started', { requestId: 2 }));
  store.ingest(event(7, 'request_finished', { requestId: 2, success: true }));
  assert.equal(store.snapshot().status, 'no_audio');
  assert.deepEqual(store.snapshot().announcements, []);
});

test('HTTP ingest requires authentication and valid data; GET and SSE expose device events', async t => {
  const server = createApp({ token: 'test-only-token' }).listen(0, '127.0.0.1');
  await once(server, 'listening');
  t.after(() => { server.closeAllConnections(); server.close(); });
  const base = `http://127.0.0.1:${server.address().port}`;
  const post = (data, auth = 'Bearer test-only-token') => fetch(`${base}/api/device/events`, {
    method: 'POST', headers: { 'Content-Type': 'application/json', Authorization: auth }, body: JSON.stringify(data)
  });
  assert.equal((await post(arrival(1, 'started'), '')).status, 401);
  assert.equal((await post(arrival(1, 'started'), 'Bearer wrong-token')).status, 401);
  assert.equal((await post({ ...arrival(1, 'started'), minutesAway: '6' })).status, 400);
  assert.equal((await post({ ...arrival(1, 'started'), stopId: '1024' })).status, 400);
  assert.equal((await post({ ...arrival(1, 'started'), color: 'red; background:url(x)' })).status, 400);
  assert.equal((await post({ ...arrival(1, 'started'), destination: 'x'.repeat(5000) })).status, 413);
  assert.equal((await fetch(`${base}/stop/demo-stop-1`)).status, 404);
  assert.deepEqual((await (await fetch(`${base}/stop/0440`)).json()).announcements, []);
  const stream = await fetch(`${base}/events`);
  const reader = stream.body.getReader();
  assert.match(new TextDecoder().decode((await reader.read()).value), /"status":"waiting"/);
  assert.deepEqual(await (await post(arrival(1, 'started'))).json(), { accepted: true });
  assert.match(new TextDecoder().decode((await reader.read()).value), /Bus 8 for route UF Health Shands/);
  await reader.cancel();
  await post(arrival(2, 'completed'));
  await post(event(3, 'request_finished', { success: true }));
  const response = await fetch(`${base}/stop/0440`);
  assert.equal(response.headers.get('cache-control'), 'no-store');
  const state = await response.json();
  assert.equal(state.status, 'complete');
  assert.equal(state.nextBuses[0].minutesAway, 6);
  assert.equal(state.announcements[0].playback, 'completed');
  assert.equal(JSON.stringify(state).includes('test-only-token'), false);
});

test('stop changes wait for device confirmation, clear old routes, and retain the reported stop after reboot', () => {
  const store = createAnnouncementStore();
  store.ingest(arrival(1, 'completed'));
  store.ingest(event(2, 'request_finished', { success: true, supportsStopSelection: true }));
  const pending = store.selectStop('0441');
  assert.equal(pending.stopId, '0440');
  assert.equal(pending.nextBuses[0].route, '8');
  assert.equal(pending.stopSwitch.stopId, '0441');
  assert.equal(store.command().id, pending.stopSwitch.id);
  store.ingest(event(3, 'stop_selected', { stopId: '0441', supportsStopSelection: true, commandId: pending.stopSwitch.id }));
  assert.equal(store.snapshot().stopId, '0441');
  assert.equal(store.snapshot().stopName, 'Tigert Hall');
  assert.deepEqual(store.snapshot().announcements, []);
  assert.equal(store.command(), null);
  store.ingest({ ...arrival(4, 'completed'), stopId: '0441', supportsStopSelection: true });
  // An acknowledgment retried after audio must not erase the announcement.
  store.ingest(event(5, 'stop_selected', { stopId: '0441', supportsStopSelection: true, commandId: pending.stopSwitch.id }));
  assert.equal(store.snapshot().announcements.length, 1);
  store.ingest(event(1, 'ready', { stopId: '0441', bootId: 'aaaaaaaaaaaaaaaa', requestId: 0, supportsStopSelection: true }));
  assert.equal(store.snapshot().stopId, '0441');
  assert.equal(store.snapshot().stopName, 'Tigert Hall');
});

test('stop switching rejects unsupported, offline, old-firmware, busy, and concurrent requests; timeout keeps actual stop', () => {
  let now = 1000;
  const store = createAnnouncementStore({ now: () => now });
  assert.throws(() => store.selectStop('9999'), /supported/);
  assert.throws(() => store.selectStop('0441'), /Connect/);
  store.ingest(event(1, 'ready'));
  assert.throws(() => store.selectStop('0441'), /firmware/);
  store.ingest(event(2, 'request_started', { supportsStopSelection: true }));
  assert.throws(() => store.selectStop('0441'), /finish/);
  store.ingest(event(3, 'request_finished', { success: true, supportsStopSelection: true }));
  assert.throws(() => store.selectStop('0440'), /already selected/);
  store.selectStop('0441');
  assert.throws(() => store.selectStop('0439'), /pending/);
  now += 90001;
  assert.equal(store.command(), null);
  assert.equal(store.snapshot().stopId, '0440');
  assert.match(store.snapshot().stopSelectionError, /did not confirm/);
  // A late device confirmation must still report its actual stop.
  store.ingest(event(4, 'heartbeat', { stopId: '0441', supportsStopSelection: true }));
  assert.equal(store.snapshot().stopId, '0441');
});

test('stop controls validate origin and IDs, authenticate command reads, and expose current selection', async t => {
  const store = createAnnouncementStore();
  const server = createApp({ token: 'test-only-token', store }).listen(0, '127.0.0.1');
  await once(server, 'listening');
  t.after(() => { server.closeAllConnections(); server.close(); });
  const base = `http://127.0.0.1:${server.address().port}`;
  const select = (stopId, origin = base) => fetch(`${base}/api/stop`, {
    method: 'POST', headers: { 'Content-Type': 'application/json', Origin: origin }, body: JSON.stringify({ stopId })
  });
  const list = await (await fetch(`${base}/api/stops`)).json();
  assert.equal(list.stops.length, 60);
  assert.ok(list.stops.some(stop => stop.id === '0441'));
  assert.equal((await select('0441', 'https://example.com')).status, 403);
  assert.equal((await select('9999')).status, 400);
  assert.equal((await select('0441')).status, 409);
  assert.equal((await fetch(`${base}/api/device/command`)).status, 401);
  store.ingest(event(1, 'ready', { supportsStopSelection: true }));
  const response = await select('0441');
  assert.equal(response.status, 202);
  const state = await response.json();
  const command = await (await fetch(`${base}/api/device/command`, { headers: { Authorization: 'Bearer test-only-token' } })).json();
  assert.equal(command.command.id, state.stopSwitch.id);
  assert.equal((await (await fetch(`${base}/api/state`)).json()).stopId, '0440');
  store.ingest(event(2, 'stop_selected', { stopId: '0441', commandId: state.stopSwitch.id, supportsStopSelection: true }));
  assert.equal((await (await fetch(`${base}/api/state`)).json()).stopId, '0441');
  assert.equal((await fetch(`${base}/stop/0440`)).status, 404);
  assert.equal((await fetch(`${base}/stop/0441`)).status, 200);
});
