#!/usr/bin/env python3
"""Copy SD recordings over USB Serial; verify size, checksum, and WAV format."""
import argparse
import io
from pathlib import Path
import re
import time
import wave

import serial


def receive(port, prefix, timeout=120):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = port.readline().decode('ascii', errors='replace').strip()
        if line.startswith('DOWNLOAD_ERROR'):
            raise RuntimeError(line)
        if line.startswith(prefix):
            return line
    raise TimeoutError(f'No {prefix} response from ESP32')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='/dev/ttyUSB0')
    parser.add_argument('--output', type=Path, default=Path('recordings'))
    parser.add_argument('--file', help='Download one WAV filename; default: all recordings')
    args = parser.parse_args()
    if args.file and not re.fullmatch(r'[A-Za-z0-9_]+\.wav', args.file):
        parser.error('--file must be a simple WAV filename')
    args.output.mkdir(parents=True, exist_ok=True)
    with serial.Serial(args.port, 115200, timeout=1, exclusive=True) as port:
        # Reset so this works even if the board was in the middle of a command.
        port.dtr = False
        port.rts = True
        time.sleep(0.1)
        port.rts = False
        receive(port, 'List: recordings.')
        port.write(b'recordings\n')
        receive(port, 'RECORDINGS_BEGIN')
        entries = []
        while True:
            line = receive(port, 'RECORDING')
            if line == 'RECORDINGS_END':
                break
            _, name, size = line.split()
            name = name.rsplit('/', 1)[-1]
            if not re.fullmatch(r'[A-Za-z0-9_]+\.wav', name):
                raise ValueError(f'Unsafe filename: {name!r}')
            if not args.file or name == args.file:
                entries.append((name, int(size)))
        if args.file and not entries:
            raise FileNotFoundError(args.file)
        print(f'Found {len(entries)} recording(s).', flush=True)
        for name, size in entries:
            if size > 10 * 1024 * 1024:
                raise ValueError(f'Unexpectedly large recording: {name}')
            port.write(f'download {name}\n'.encode())
            begin = receive(port, 'DOWNLOAD_BEGIN').split()
            if begin[1:] != [name, str(size)]:
                raise ValueError(f'Transfer metadata mismatch: {begin}')
            data = bytearray()
            checksum = 2166136261
            while True:
                line = receive(port, 'DOWNLOAD_', timeout=15)
                if line.startswith('DOWNLOAD_END '):
                    expected = int(line.split()[1], 16)
                    break
                if not line.startswith('DOWNLOAD_DATA '):
                    raise ValueError(f'Unexpected transfer response: {line}')
                block = bytes.fromhex(line.split()[1])
                data.extend(block)
                if len(data) > size:
                    raise ValueError('Transfer exceeded advertised size')
                for byte in block:
                    checksum = ((checksum ^ byte) * 16777619) & 0xffffffff
            if len(data) != size or checksum != expected:
                raise ValueError(f'Incomplete/corrupt transfer: {name}')
            with wave.open(io.BytesIO(data), 'rb') as wav:
                if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate()) != (1, 2, 16000):
                    raise ValueError(f'Unexpected audio format: {name}')
                duration = wav.getnframes() / wav.getframerate()
                if len(wav.readframes(wav.getnframes())) != wav.getnframes() * 2:
                    raise ValueError(f'Truncated WAV: {name}')
            destination = args.output / name
            if destination.exists():
                if destination.read_bytes() != data:
                    raise FileExistsError(f'Refusing to overwrite different file: {destination}')
            else:
                with destination.open('xb') as output:
                    output.write(data)
            print(f'Saved {destination} ({duration:.2f} s, {size} bytes; verified)', flush=True)


if __name__ == '__main__':
    main()
