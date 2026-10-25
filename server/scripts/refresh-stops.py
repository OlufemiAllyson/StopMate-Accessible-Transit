"""Refresh official RTS stop choices and the matching firmware allowlist."""
import argparse
import json
from pathlib import Path
import re
import urllib.parse
import urllib.request
from datetime import datetime
from zoneinfo import ZoneInfo

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--firmware', type=Path, default=Path(__file__).resolve().parents[2] / 'firmware code')
args = parser.parse_args()
server = Path(__file__).resolve().parents[1]
# Use the same routes/directions as the audio table; never write or print the key.
config = (args.firmware / 'include/route_config.h').read_text()
routes = re.findall(r'\{\s*"(\d+)"\s*,\s*"(INBOUND|OUTBOUND)"', config)
key_match = re.search(r'RTS_API_KEY[^\n"]*"([^"]+)"', (args.firmware / 'include/secrets.h').read_text())
if not routes or not key_match:
    raise SystemExit('Configure the firmware route table and local RTS_API_KEY first.')
stops = {}
try:
    for route, direction in routes:
        query = urllib.parse.urlencode(dict(key=key_match[1], format='json', rt=route, dir=direction))
        with urllib.request.urlopen('https://riderts.app/bustime/api/v3/getstops?' + query, timeout=20) as response:
            items = json.load(response)['bustime-response']['stops']
        if not items:
            raise ValueError('Empty stop list')
        for item in items:
            if not re.fullmatch(r'\d{4}', item['stpid']):
                raise ValueError('Unsupported stop ID')
            stop = stops.setdefault(item['stpid'], dict(id=item['stpid'], name=item['stpnm'], routes=[]))
            if route not in stop['routes']:
                stop['routes'].append(route)
except Exception:
    raise SystemExit('RTS stop lookup failed; existing catalog was not changed.') from None
catalog = dict(source='https://riderts.app/bustime/api/v3/getstops',
    verifiedOn=datetime.now(ZoneInfo('America/New_York')).date().isoformat(),
    direction=','.join(sorted({direction for _, direction in routes})),
    stops=sorted(stops.values(), key=lambda item: item['name'].lower()))
(server / 'stops.json').write_text(json.dumps(catalog, indent=2) + '\n')
(args.firmware / 'include/supported_stops.h').write_text(
    '#pragma once\n// Generated from server/stops.json; route/direction support comes from route_config.h.\n'
    'constexpr const char *SUPPORTED_STOP_IDS[] = {\n' +
    ''.join('    "' + stop + '",\n' for stop in sorted(stops)) + '};\n')
print(f'Updated {len(stops)} stops. Rebuild/upload the firmware and restart the server.')
