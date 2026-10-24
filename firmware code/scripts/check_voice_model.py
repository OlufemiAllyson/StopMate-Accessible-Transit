"""Replay held-out synthetic WAVs through the exported streaming classifier.

Requires CMake and a host C++ compiler. Does not contact the ESP32 or RTS API.
"""
from pathlib import Path
import json
import subprocess
import wave

ROOT = Path(__file__).resolve().parents[1]
EXPORT = ROOT / 'eeellls-project-1-cpp-mcu-v2-impulse-#5'
WORK = ROOT / '.pio/ei_host_v2'
WORK.mkdir(parents=True, exist_ok=True)
sources = [ROOT / 'test/test_voice_model.cpp']
for directory in ['tflite-model', 'edge-impulse-sdk/dsp', 'edge-impulse-sdk/tensorflow',
                  'edge-impulse-sdk/porting/posix']:
    sources.extend(p for p in (EXPORT / directory).rglob('*') if p.suffix in ['.c', '.cpp', '.cc'])
cmake = 'cmake_minimum_required(VERSION 3.16)\nproject(ei_test C CXX)\nset(CMAKE_CXX_STANDARD 17)\n'
cmake += 'add_executable(ei_test\n' + '\n'.join(f'"{p}"' for p in sources) + ')\n'
cmake += f'target_include_directories(ei_test PRIVATE "{EXPORT}")\n'
cmake += 'target_compile_options(ei_test PRIVATE -O2 -w -ffunction-sections -fdata-sections)\n'
cmake += 'target_link_options(ei_test PRIVATE -Wl,--gc-sections)\n'
cmake += 'target_compile_definitions(ei_test PRIVATE EI_CLASSIFIER_TFLITE_ENABLE_ESP_NN=0 EIDSP_USE_ESP_DSP=0 EI_CLASSIFIER_TFLITE_ENABLE_CMSIS_NN=0 EIDSP_USE_CMSIS_DSP=0)\n'
(WORK / 'CMakeLists.txt').write_text(cmake)
fixtures = []
for path in sorted((ROOT / 'datasets/hey_stopmate_200/testing').glob('*.wav')):
    with wave.open(str(path)) as wav:
        assert (wav.getframerate(), wav.getnchannels(), wav.getsampwidth()) == (16000, 1, 2)
        fixture = WORK / (path.stem + '.pcm')
        fixture.write_bytes(wav.readframes(wav.getnframes()))
        fixtures.append(fixture)
if not fixtures:
    raise RuntimeError('No held-out WAVs found')
silence = WORK / 'silence.pcm'
silence.write_bytes(bytes(64000))
fixtures.append(silence)
subprocess.run(['cmake', '-S', str(WORK), '-B', str(WORK / 'build')], check=True)
subprocess.run(['cmake', '--build', str(WORK / 'build'), '-j', '6'], check=True)
run = subprocess.run([str(WORK / 'build/ei_test'), *map(str, fixtures)], text=True, capture_output=True, check=True)
(WORK / 'results.log').write_text(run.stdout)
summary = {'positive_clips': 0, 'positive_triggers': 0, 'negative_clips': 0, 'false_triggers': 0}
for line in run.stdout.splitlines():
    if not line.startswith('RESULT '):
        continue
    _, name, detected, peak = line.split()
    positive = Path(name).name.startswith('hey_stopmate.')
    summary['positive_clips' if positive else 'negative_clips'] += 1
    summary['positive_triggers' if positive else 'false_triggers'] += int(detected)
assert summary['positive_clips'] + summary['negative_clips'] == len(fixtures)
(WORK / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print(json.dumps(summary, indent=2))
print('Synthetic replay only; this does not measure live microphone accuracy or MCU timing.')
