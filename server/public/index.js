let controlsRequireKey = false;
let stopsLoaded = false;
let selectedStopShown = null;
let switchingStop = false;
let selectionError = "";
let currentSnapshot = null;
let currentData = [];
let serverAvailable = false;
let lastSpokenId = null;
let fetching = false;

function setTheme(theme) {
  document.documentElement.setAttribute('data-theme', theme);
  document.querySelectorAll('[id^="theme-"]').forEach(btn => {
    btn.setAttribute('aria-pressed', 'false');
    btn.classList.remove('bg-slate-800', 'bg-slate-300', 'text-slate-900', 'text-cyan-400', 'text-yellow-400');
    btn.classList.add('text-slate-600', 'dark:text-slate-400');
  });
  const active = document.getElementById(`theme-${theme === 'high-contrast' ? 'hc' : theme}`);
  active?.setAttribute('aria-pressed', 'true');
  active?.classList.add(theme === 'light' ? 'bg-slate-300' : 'bg-slate-800', theme === 'high-contrast' ? 'text-yellow-400' : 'text-cyan-400');
  announceToSR(`Theme switched to ${theme}`);
}

function announceToSR(message) {
  document.getElementById('sr-announcer').textContent = message;
}

async function fetchStopData(manual = false) {
  if (fetching) return;
  fetching = true;
  try {
    const response = await fetch('/api/state', { cache: 'no-store' });
    if (!response.ok) throw new Error('Server unavailable');
    render(await response.json());
    if (manual) announceToSR('Showing the latest announcement received from StopMate.');
  } catch {
    serverAvailable = false;
    updateConnection();
    updateStopControls();
    if (currentSnapshot?.announcements.length) {
      document.getElementById('announcement-heading').textContent = 'Last received announcement';
      filterRoutes();
    }
    if (!currentSnapshot) showEmpty('Waiting for the StopMate server. No device announcements received.');
    if (manual) announceToSR('Cannot reach the server. Previous announcements have not been refreshed.');
  } finally {
    fetching = false;
  }
}

function updateConnection() {
  const connected = serverAvailable && currentSnapshot?.deviceConnected;
  const label = !serverAvailable ? 'Server disconnected' : connected ? 'Device connected' : 'Waiting for device';
  document.getElementById('feed-status').textContent = label;
  document.getElementById('connection-badge').setAttribute('aria-label', label);
  document.getElementById('connection-dot').classList.toggle('animate-pulse', Boolean(connected));
  document.getElementById('connection-note').textContent = !serverAvailable
    ? 'The server is unavailable. Any announcement below is the last one received.'
    : !currentSnapshot?.deviceConnected
      ? 'Keep StopMate connected to its USB bridge. Any announcement below is the last one received.'
      : 'Mirroring StopMate. Wave, press the button, or say “Hey StopMate” to request arrivals.';
}

function render(data) {
  if (!data || typeof data.stopId !== "string" || !Array.isArray(data.announcements)) throw new Error('Invalid snapshot');
  if (currentSnapshot?.serverId === data.serverId && data.revision < currentSnapshot.revision) return;
  currentSnapshot = data;
  document.getElementById('stop-title').textContent = data.stopName;
  document.getElementById('stop-number').textContent = `Stop #${data.stopId}`;
  if (selectedStopShown !== data.stopId) {
    document.getElementById('stop-select').value = data.stopId;
    document.getElementById('route-search').value = '';
    selectedStopShown = data.stopId;
    selectionError = '';
  }
  serverAvailable = true;
  currentData = data.announcements.filter(item => item.kind === 'arrival');
  updateConnection();
  updateStopControls();
  document.getElementById('last-updated').textContent = data.updatedAt
    ? `Device update: ${new Date(data.updatedAt).toLocaleTimeString()}` : 'No announcement yet';
  document.getElementById('last-updated').title = data.updatedAt ? new Date(data.updatedAt).toLocaleString() : '';
  const titles = {
    waiting: 'Ready when you are', checking: 'Checking bus arrivals', playing: 'Device announcement',
    complete: 'Last device announcement', playback_error: 'Device audio could not finish', no_audio: 'No announcement played'
  };
  document.getElementById('announcement-heading').textContent = !data.deviceConnected && data.announcements.length
    ? 'Last received announcement' : titles[data.status] || 'Device announcement';
  const transcript = document.getElementById('announcement-text');
  transcript.replaceChildren();
  for (const item of data.announcements) {
    const paragraph = document.createElement('p');
    paragraph.textContent = `${item.text}${item.playback === 'failed' ? ' (Playback interrupted.)' : ''}`;
    transcript.appendChild(paragraph);
  }
  if (!data.announcements.length) {
    transcript.textContent = data.status === 'checking' ? 'StopMate is requesting the schedule.'
      : data.status === 'playback_error' ? 'The device could not play the audio.'
      : data.status === 'no_audio' ? 'This request produced no playable announcements.'
      : 'Your next device announcement will appear here.';
  }
  const latest = data.announcements.at(-1);
  if (latest && latest.playback !== 'failed' && latest.id !== lastSpokenId) {
    announceToSR(latest.text);
    lastSpokenId = latest.id;
  }
  filterRoutes();
}

function showEmpty(message) {
  const list = document.getElementById('arrivals-list');
  const item = document.createElement('li');
  item.className = 'glass-card rounded-2xl p-8 text-center text-lg font-bold';
  item.textContent = message;
  list.replaceChildren(item);
  document.getElementById('visible-count').textContent = '0';
}

function filterRoutes() {
  if (!currentData.length) {
    const noBuses = currentSnapshot?.announcements.some(item => item.kind === 'no_buses' && item.playback !== 'failed');
    showEmpty(noBuses ? 'No buses available.' : currentSnapshot?.status === 'checking'
      ? 'Waiting for the device announcement…' : 'No bus routes have been announced for this request.');
    return;
  }
  const query = document.getElementById('route-search').value.toLowerCase().trim();
  const filtered = currentData.filter(bus => bus.route.toLowerCase().includes(query) || bus.destination.toLowerCase().includes(query));
  if (!filtered.length) { showEmpty(`No announced routes matching “${query}”.`); return; }
  document.getElementById('visible-count').textContent = String(filtered.length);
  document.getElementById('arrivals-list').innerHTML = filtered.map(bus => {
    const color = /^#[0-9a-f]{6}$/i.test(bus.color) ? bus.color : '#64748b';
    const channels = [1, 3, 5].map(offset => parseInt(color.slice(offset, offset + 2), 16) / 255)
      .map(value => value <= 0.04045 ? value / 12.92 : ((value + 0.055) / 1.055) ** 2.4);
    const foreground = channels[0] * 0.2126 + channels[1] * 0.7152 + channels[2] * 0.0722 > 0.179 ? '#000000' : '#ffffff';
    const eta = bus.minutesAway === 0 ? 'DUE' : `${Number(bus.minutesAway)} MIN`;
    const playback = bus.playback === 'started'
      ? serverAvailable && currentSnapshot.deviceConnected ? 'Announcing now' : 'Playback not confirmed'
      : bus.playback === 'failed' ? 'Playback interrupted' : 'Announced';
    return `<li class="arrival-card glass-card rounded-2xl p-4 md:p-6 flex flex-col md:flex-row items-start md:items-center justify-between gap-4 border-l-8 shadow-lg" style="border-left-color:${color}">
      <div class="arrival-route flex items-center gap-4">
        <div class="w-16 h-16 rounded-2xl font-black text-3xl flex items-center justify-center shrink-0" style="background-color:${color};color:${foreground}" aria-label="Route ${escapeHtml(bus.route)}">${escapeHtml(bus.route)}</div>
        <div class="arrival-destination"><p class="text-xs uppercase font-bold text-slate-600 dark:text-slate-400">${playback}</p>
          <h4 class="text-xl md:text-2xl font-black text-slate-900 dark:text-white">${escapeHtml(bus.destination)}</h4></div>
      </div>
      <div class="text-left md:text-right"><p class="text-3xl font-black font-mono">${eta}</p>
        <p class="text-xs text-slate-600 dark:text-slate-400">Time spoken by StopMate</p></div>
    </li>`;
  }).join('');
}

function escapeHtml(value) {
  return String(value).replace(/[&<>"']/g, character => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[character]);
}

async function loadStops() {
  try {
    const response = await fetch('/api/stops');
    if (!response.ok) throw new Error();
    const { stops, controlsRequireKey: requiresKey } = await response.json();
    controlsRequireKey = Boolean(requiresKey);
    document.getElementById('control-access').hidden = !controlsRequireKey;
    stopsLoaded = true;
    const select = document.getElementById('stop-select');
    const previous = select.value;
    select.replaceChildren(...stops.map(stop => {
      const option = document.createElement('option');
      option.value = stop.id;
      option.textContent = `${stop.id} — ${stop.name} (Route ${stop.routes.join(', ')})`;
      return option;
    }));
    select.value = currentSnapshot?.stopId || previous;
    updateStopControls();
  } catch {
    selectionError = 'Could not load the stop list. Reload the page to try again.';
    updateStopControls();
  }
}

function updateStopControls() {
  const state = currentSnapshot;
  const pending = switchingStop || Boolean(state?.stopSwitch);
  const busy = ['checking', 'playing'].includes(state?.status);
  document.getElementById('stop-select').disabled = pending;
  document.getElementById('switch-stop').disabled = pending || busy || !stopsLoaded || (controlsRequireKey && !document.getElementById('control-key').value) || !serverAvailable ||
    !state?.deviceConnected || !state?.supportsStopSelection || document.getElementById('stop-select').value === state?.stopId;
  const message = selectionError || state?.stopSelectionError || (state?.stopSwitch
    ? `Waiting for StopMate to confirm ${state.stopSwitch.stopId} — ${state.stopSwitch.stopName}…`
    : switchingStop ? 'Sending stop change…'
    : !serverAvailable || !state?.deviceConnected ? 'The device’s USB bridge must be connected to switch stops.'
    : !state.supportsStopSelection ? 'Upload the updated firmware to enable stop switching.'
    : busy ? 'Wait for the current announcement to finish.'
    : controlsRequireKey && !document.getElementById('control-key').value ? 'Enter the access code to change the device’s stop. Announcements are public.'
    : `Stop ${state.stopId} is active. Switching requests a new announcement and saves the stop on the device.`);
  document.getElementById('stop-change-status').textContent = message;
}

async function switchStop(event) {
  event.preventDefault();
  if (switchingStop) return;
  switchingStop = true;
  selectionError = '';
  const stopId = document.getElementById('stop-select').value;
  updateStopControls();
  try {
    const response = await fetch('/api/stop', { method: 'POST',
      headers: { 'Content-Type': 'application/json',
        ...(controlsRequireKey ? { 'X-StopMate-Control': document.getElementById('control-key').value } : {}) },
      body: JSON.stringify({ stopId }) });
    const result = await response.json();
    if (!response.ok) throw new Error(result.error || 'Could not change the stop.');
    render(result);
  } catch (error) {
    selectionError = error.message;
  } finally {
    switchingStop = false;
    updateStopControls();
  }
}

loadStops();
fetchStopData();
setInterval(fetchStopData, 5000);
if ('EventSource' in window) {
  const events = new EventSource('/events');
  events.onmessage = event => { try { render(JSON.parse(event.data)); } catch { fetchStopData(); } };
  events.onerror = () => { fetchStopData(); };
}
