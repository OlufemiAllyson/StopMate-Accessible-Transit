"""Generate 200 synthetic Edge Impulse clips. Run in .pio/kokoro-venv."""
import hashlib
import json
from pathlib import Path
import random
import zipfile

import numpy as np
import soundfile as sf
import torch
from huggingface_hub import hf_hub_download
from kokoro import KModel, KPipeline
from scipy.signal import resample_poly, butter, sosfilt

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'datasets' / 'hey_stopmate_200'
REPO = 'hexgrad/Kokoro-82M'
REVISION = 'f3ff3571791e39611d31c381e3a41a3af07b4987'
RATE = 16000
LENGTH = 2 * RATE
SEED = 4402026
VOICES = {
    'training': ['af_heart', 'af_bella', 'af_nicole', 'af_sarah',
                 'af_sky', 'am_adam', 'am_michael', 'am_echo'],
    'testing': ['af_nova', 'am_fenrir'],
}
NEGATIVES = [
    'Hey, stop there.', 'Hey, stop that.', 'Hey, stop me.',
    'Hey, shop mate.', 'Hey, stock market.', 'Hey, start now.',
    'Stop mate.', 'Stopmate.', 'Hey there.', 'Stop the bus.',
    'Next stop.', 'Wait a minute.', 'Bus eight is coming.',
    'Route thirteen.', 'Arriving now.', 'In five minutes.',
    'Where is the bus?', 'The Standard apartments.', 'Cottage Grove.',
    'Health Shands.', 'Hey, good morning.', 'Please wait here.',
    'Start the timer.', 'Hey, check the time.', 'Can you help me?',
    'Hey, what time?', 'Stop talking.', 'My class starts soon.',
    'Catch the next bus.', 'Thank you, goodbye.',
]


def noise(rng, kind, size=LENGTH):
    x = rng.normal(size=size)
    t = np.arange(size) / RATE
    if kind == 'pink':
        f = np.fft.rfftfreq(size, 1 / RATE)
        x = np.fft.irfft(np.fft.rfft(x) / np.sqrt(np.maximum(f, 20)), n=size)
    elif kind == 'rumble':
        x = sosfilt(butter(3, 250, fs=RATE, output='sos'), x)
    elif kind == 'hum':
        hz = rng.uniform(48, 65)
        x = 0.7*np.sin(2*np.pi*hz*t) + 0.2*np.sin(2*np.pi*hz*3*t) + 0.1*x
    elif kind == 'wind':
        x = sosfilt(butter(2, 600, fs=RATE, output='sos'), x)
        x *= 0.2 + np.sin(2*np.pi*rng.uniform(.3, 1.5)*t)**2
    elif kind == 'clicks':
        x *= 0.02
        for _ in range(int(rng.integers(2, 12))):
            p = int(rng.integers(0, size-300))
            x[p:p+300] += rng.normal(size=300)*np.exp(-np.arange(300)/40)
    elif kind == 'tones':
        hz = rng.uniform(250, 2000)
        x = np.sin(2*np.pi*hz*t) * np.sin(2*np.pi*rng.uniform(1, 5)*t)**8 + .03*x
    return x / max(np.sqrt(np.mean(x*x)), 1e-9)


def main():
    if OUT.exists():
        raise SystemExit(f'Refusing to overwrite existing dataset: {OUT}')
    torch.set_num_threads(4)
    torch.manual_seed(SEED)
    random.seed(SEED)
    rng = np.random.default_rng(SEED)
    for split in VOICES:
        (OUT / split).mkdir(parents=True)
    model = KModel(repo_id=REPO,
                   config=hf_hub_download(REPO, 'config.json', revision=REVISION),
                   model=hf_hub_download(REPO, 'kokoro-v1_0.pth', revision=REVISION))
    pipeline = KPipeline(lang_code='a', repo_id=REPO, model=model, device='cpu')
    entries = []

    def write(x, label, split, info):
        assert len(x) == LENGTH and np.isfinite(x).all()
        peak = np.max(np.abs(x))
        if peak > .95:
            x = x * (.95 / peak)
        filename = f'{label}.{len(entries):03d}.wav'
        relative = f'{split}/{filename}'
        sf.write(OUT / relative, x, RATE, subtype='PCM_16')
        entry = dict(file=relative, label=label, split=split, **info)
        entry['sha256'] = hashlib.sha256((OUT / relative).read_bytes()).hexdigest()
        entries.append(entry)
        (OUT / 'manifest.json').write_text(json.dumps({
            'seed': SEED, 'model': REPO, 'revision': REVISION,
            'sample_rate': RATE, 'seconds_per_clip': 2, 'synthetic': True,
            'clips': entries}, indent=2)+'\n')
        print(f'{len(entries):03d}/200 {relative}', flush=True)

    for split, voices in VOICES.items():
        for vi, voice in enumerate(voices):
            voice_file = hf_hub_download(REPO, f'voices/{voice}.pt', revision=REVISION)
            jobs = [('hey_stopmate', 'Hey, Stop Mate.', j) for j in range(10)]
            # Six distinct negatives per voice, including near misses and ordinary speech.
            for j in range(6):
                jobs.append(('unknown', NEGATIVES[(vi*6+j+(7 if split=='testing' else 0)) % len(NEGATIVES)], j))
            for label, text, j in jobs:
                speed = float(rng.uniform(.85, 1.2))
                for attempt in range(4):
                    results = list(pipeline(text, voice=voice_file, speed=speed))
                    x = np.concatenate([r.audio.cpu().numpy() for r in results])
                    x = resample_poly(x, 2, 3)
                    active = np.flatnonzero(np.abs(x) > max(.001, np.max(np.abs(x))*.008))
                    if not len(active):
                        raise RuntimeError(f'Empty synthesis for {text}')
                    # Retain 30 ms around speech; never cut the phrase to fit the window.
                    x = x[max(0,active[0]-480):min(len(x),active[-1]+481)]
                    if len(x) <= int(1.8*RATE):
                        break
                    speed *= 1.15
                else:
                    raise RuntimeError(f'Cannot fit full phrase: {voice}: {text}')
                x = x / max(np.max(np.abs(x)), 1e-9) * rng.uniform(.15, .65)
                clean = (j % 4 == 0)
                echo_ms = 0
                if not clean:
                    echo_ms = int(rng.integers(25, 100))
                    delay = echo_ms*RATE//1000
                    echoed = np.pad(x, (0, delay*2))
                    echoed[delay:delay+len(x)] += .12*x
                    echoed[2*delay:2*delay+len(x)] += .04*x
                    x = echoed
                assert len(x) < LENGTH
                offset = int(rng.integers(0, LENGTH-len(x)+1))
                framed = np.zeros(LENGTH)
                framed[offset:offset+len(x)] = x
                snr = None
                if not clean:
                    snr = float(rng.uniform(18, 35))
                    bg = noise(rng, ['white','pink','rumble','wind'][j%4])
                    framed += bg * np.sqrt(np.mean(x*x)) / (10**(snr/20))
                write(framed, label, split, dict(
                    text='Hey StopMate' if label=='hey_stopmate' else text,
                    synthesis_text=text, voice=voice, speed=round(speed,5),
                    phonemes=' '.join(r.phonemes for r in results),
                    speech_offset_ms=round(offset/RATE*1000),
                    speech_duration_ms=round(len(x)/RATE*1000),
                    noise_snr_db=snr, echo_delay_ms=echo_ms))
    kinds = ['white','pink','rumble','hum','wind','clicks','tones','quiet']
    for split, count in [('training',32), ('testing',8)]:
        for j in range(count):
            kind = kinds[j%len(kinds)]
            x = noise(rng, kind) * (rng.uniform(.00005,.0003) if kind=='quiet' else rng.uniform(.003,.07))
            write(x, 'noise', split, dict(generator='procedural', noise_kind=kind))
    assert len(entries) == 200
    assert len({e['sha256'] for e in entries}) == 200
    for e in entries:
        x, rate = sf.read(OUT/e['file'])
        assert rate == RATE and x.ndim == 1 and len(x) == LENGTH
        assert sf.info(OUT/e['file']).subtype == 'PCM_16'
        assert np.max(np.abs(x)) < 1
    for split in VOICES:
        with zipfile.ZipFile(OUT/f'{split}.zip','w',zipfile.ZIP_DEFLATED) as z:
            for p in sorted((OUT/split).glob('*.wav')):
                z.write(p, p.name)
    print('VALIDATED: 200 unique WAV files, 160 training / 40 testing.', flush=True)

if __name__ == '__main__':
    main()
