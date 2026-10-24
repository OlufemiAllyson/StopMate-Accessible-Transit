"""Generate destination clips for the southbound routes at The Standard.

Run with a Python environment containing kokoro==0.9.4, torch, and soundfile.
Then run `pio run -t buildfs` to stage the clips in .pio/audio_data.
"""
import json
from pathlib import Path

from kokoro import KPipeline
import numpy as np
import soundfile as sf
import torch


def main():
    root = Path(__file__).resolve().parents[1] / "kokoro_transit_snippets"
    manifest_path = root / "manifest.json"
    manifest = json.loads(manifest_path.read_text())
    torch.set_num_threads(4)
    pipeline = KPipeline(lang_code="a", device="cpu")
    destinations = [
        (8, "uf_health_shands.wav", "UF Health Shands", "U F Health Shands,"),
        (13, "cottage_grove.wav", "Cottage Grove", "Cottage Grove,"),
    ]
    for number, filename, text, synthesis_text in destinations:
        results = list(pipeline(synthesis_text, voice="af_heart", speed=1.0))
        samples = np.concatenate([result.audio.cpu().numpy() for result in results])
        # Match the existing clips' short margins instead of retaining TTS silence.
        voiced = np.flatnonzero(np.abs(samples) > 0.002)
        if not len(voiced):
            raise RuntimeError(f"No speech produced for {text}")
        samples = np.pad(samples[voiced[0]:voiced[-1] + 1], (960, 960))
        relative = f"routes/{filename}"
        sf.write(root / relative, samples, 24000, subtype="PCM_16")
        entry = {
            "file": relative,
            "text": text,
            "synthesis_text": synthesis_text,
            "seconds": round(len(samples) / 24000, 3),
            "phonemes": " ".join(result.phonemes for result in results),
            "route_number": number,
            "stop_id": "0440",
            "description": "Destination from The Standard Apartments southbound stop",
        }
        manifest["clips"] = [c for c in manifest["clips"] if c["file"] != relative]
        manifest["clips"].append(entry)
        print(f"{relative}: {entry['seconds']} seconds", flush=True)
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
