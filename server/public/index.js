// Configuration & Options
const STOP_ID = "The-Standard-Apartments";
const POLL_INTERVAL = 15000;

// Mock RTS Data fallback for offline/preview mode
const MOCK_DATA = {
  stopId: "The-Standard-Apartments",
  nextBuses: [
    { route: "1", destination: "Butler Plaza Transfer Station", minutesAway: 2, status: "On Time", color: "bg-blue-600" },
    { route: "38", destination: "UF Health / Oaks Mall", minutesAway: 5, status: "On Time", color: "bg-purple-600" },
    { route: "5", destination: "Gainesville Mall / Downtown", minutesAway: 11, status: "Delayed 2 min", color: "bg-emerald-600" },
    { route: "8", destination: "UF Campus Loop / Reitz Union", minutesAway: 18, status: "On Time", color: "bg-amber-600" },
    { route: "122", destination: "UF Cultural Plaza", minutesAway: 27, status: "On Time", color: "bg-rose-600" }
  ]
};

let currentData = [];

// Accessible Theme Switcher
function setTheme(theme) {
  document.documentElement.setAttribute('data-theme', theme);
  document.querySelectorAll('[id^="theme-"]').forEach(btn => {
    btn.classList.remove('bg-slate-800', 'text-cyan-400', 'text-yellow-400', 'bg-cyan-500', 'text-slate-950');
    btn.classList.add('text-slate-600', 'dark:text-slate-400');
  });
  const activeBtn = document.getElementById(`theme-${theme === 'high-contrast' ? 'hc' : theme}`);
  if (activeBtn) {
    if (theme === 'high-contrast') {
      activeBtn.classList.add('bg-slate-800', 'text-yellow-400');
    } else if (theme === 'light') {
      activeBtn.classList.add('bg-slate-300', 'text-slate-900');
    } else {
      activeBtn.classList.add('bg-slate-800', 'text-cyan-400');
    }
  }
  announceToSR(`Theme switched to ${theme}`);
}

// Screen Reader Announcer
function announceToSR(message) {
  const announcer = document.getElementById('sr-announcer');
  if (announcer) {
    announcer.textContent = message;
  }
}

// Fetch Stop Data with Fallback
async function fetchStopData(manual = false) {
  try {
    const res = await fetch(`/stop/${STOP_ID}`);
    if (!res.ok) throw new Error("Server Response Error");
    const data = await res.json();
    render(data);
    if (manual) announceToSR("Arrival schedule updated.");
  } catch (err) {
    // Fallback to robust RTS preview data
    render(MOCK_DATA);
    if (manual) announceToSR("Refreshed simulated transit updates.");
  }
}

// Render Function
function render(data) {
  const arrivalsList = document.getElementById("arrivals-list");
  const lastUpdated = document.getElementById("last-updated");
  
  const buses = data.nextBuses || [];
  currentData = buses;

  const now = new Date();
  lastUpdated.textContent = `Updated: ${now.toLocaleTimeString([], { hour: '2-digit', minute: '2-digit', second: '2-digit' })}`;

  if (buses.length === 0) {
    arrivalsList.innerHTML = `
      <li class="glass-card rounded-2xl p-8 text-center text-slate-700 dark:text-slate-400">
        <p class="text-lg font-bold text-slate-900 dark:text-white">No Upcoming Departures Scheduled</p>
        <p class="text-sm mt-1">Please check back later for live transit updates.</p>
      </li>`;
    document.getElementById("visible-count").textContent = "0";
    return;
  }

  filterRoutes();
}

// Filtering & Dynamic List Generation
function filterRoutes() {
  const query = document.getElementById("route-search").value.toLowerCase().trim();
  const filtered = currentData.filter(b => b.route.toLowerCase().includes(query) || b.destination.toLowerCase().includes(query));
  
  const arrivalsList = document.getElementById("arrivals-list");
  document.getElementById("visible-count").textContent = filtered.length;

  if (filtered.length === 0) {
    arrivalsList.innerHTML = `
      <li class="glass-card rounded-2xl p-8 text-center text-slate-700 dark:text-slate-400">
        No active routes matching "${escapeHtml(query)}"
      </li>`;
    return;
  }

  arrivalsList.innerHTML = filtered.map((bus) => {
    const isArriving = bus.minutesAway <= 2;
    const etaText = bus.minutesAway === 0 ? "DUE" : `${bus.minutesAway} <span class="text-xs font-black text-slate-600 dark:text-slate-400">MIN</span>`;
    const badgeBg = bus.color || 'bg-cyan-600';

    return `
      <li class="glass-card rounded-2xl p-4 md:p-6 flex flex-col md:flex-row items-start md:items-center justify-between gap-4 border-l-8 ${isArriving ? 'border-l-emerald-500' : 'border-l-cyan-500'} hover:border-black/30 dark:hover:border-white/40 transition-all shadow-lg">
        
        <div class="flex items-center gap-4 w-full md:w-auto">
          <!-- Route Badge -->
          <div class="w-16 h-16 rounded-2xl ${badgeBg} text-white font-black text-2xl md:text-3xl flex items-center justify-center shadow-lg shrink-0 border border-white/20" aria-label="Route ${escapeHtml(bus.route)}">
            ${escapeHtml(bus.route)}
          </div>

          <!-- Route Details -->
          <div class="flex-1 min-w-0">
            <div class="flex items-center gap-2">
              <span class="text-xs font-black uppercase tracking-widest text-slate-600 dark:text-slate-400">Destination</span>
              <span class="text-xs px-2.5 py-0.5 rounded-md bg-slate-200 dark:bg-slate-800/90 text-slate-800 dark:text-slate-200 font-mono font-bold border border-black/10 dark:border-white/10">${escapeHtml(bus.status || 'On Time')}</span>
            </div>
            <h4 class="text-xl md:text-2xl font-black text-slate-900 dark:text-white truncate mt-0.5">
              ${escapeHtml(bus.destination)}
            </h4>
          </div>
        </div>

        <!-- Departure Time & Status Indicator -->
        <div class="flex items-center justify-between md:justify-end w-full md:w-auto gap-6 pt-3 md:pt-0 border-t md:border-t-0 border-black/10 dark:border-white/10">
          <div class="text-left md:text-right">
            <div class="text-3xl md:text-4xl font-black font-mono tracking-tight text-slate-900 dark:text-white flex items-baseline gap-1">
              ${etaText}
        </div>

      </li>
    `;
  }).join("");
}

function escapeHtml(str) {
  return String(str).replace(/[&<>"']/g, m => ({
    '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;'
  })[m]);
}


// Initialize & Start Poll Loop
fetchStopData();
setInterval(fetchStopData, POLL_INTERVAL);