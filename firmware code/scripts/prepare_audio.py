"""Stage reusable WAV clips to copy to the root of the ESP32's SD card."""
from pathlib import Path
import shutil
import subprocess
import wave

Import("env")

project = Path(env.subst("$PROJECT_DIR"))
source = project / "kokoro_transit_snippets"
destination = project / ".pio" / "audio_data"
if not shutil.which("ffmpeg"):
    raise RuntimeError("Install ffmpeg to convert the source speech to 16 kHz for playback")
# Keep the established filenames used by the firmware and existing SD cards.
aliases = {
    "butler_plaza_to_the_hub.wav": "butler_hub.wav",
    "reitz_union_to_liv_apartments.wav": "reitz_liv.wav",
}

# This is a generated directory, never the source audio directory.
if destination.exists():
    shutil.rmtree(destination)

count = 0
total = 0
for group in ("numbers", "phrases", "routes"):
    clips = sorted((source / group).glob("*.wav"))
    if not clips:
        raise RuntimeError(f"Missing audio clips in {source / group}")
    (destination / group).mkdir(parents=True, exist_ok=True)
    for clip in clips:
        with wave.open(str(clip), "rb") as wav:
            if (wav.getnchannels(), wav.getsampwidth(), wav.getframerate(),
                    wav.getcomptype()) != (1, 2, 24000, "NONE"):
                raise RuntimeError(f"Expected 24 kHz mono PCM16 WAV: {clip}")
        name = aliases.get(clip.name, clip.name)
        if len(name.encode("utf-8")) > 31:
            raise RuntimeError(f"Staged filename exceeds the project's 31-character limit: {clip.name}")
        target = destination / group / name
        subprocess.run([
            "ffmpeg", "-v", "error", "-nostdin", "-y", "-i", str(clip),
            "-map_metadata", "-1", "-ar", "16000", "-ac", "1", "-c:a", "pcm_s16le",
            str(target),
        ], check=True)
        count += 1
        total += target.stat().st_size

print(f"Staged {count} audio snippets at 16 kHz ({total:,} bytes); examples excluded.")

# Carry this small new clip with firmware so an already-installed SD card can
# receive it without a separate card-reader transfer. Playback still uses SD.
generated = project / ".pio" / "generated"
generated.mkdir(parents=True, exist_ok=True)
audio = (destination / "phrases" / "no_buses_available.wav").read_bytes()
header = "#pragma once\n#include <stdint.h>\nstatic const uint8_t NO_BUSES_WAV[] = {\n"
header += "\n".join(
    ",".join(str(byte) for byte in audio[i:i + 24]) + ","
    for i in range(0, len(audio), 24)
)
header += "\n};\n"
header_path = generated / "no_buses_audio.h"
if not header_path.exists() or header_path.read_text() != header:
    header_path.write_text(header)
env.Append(CPPPATH=[str(generated)])
