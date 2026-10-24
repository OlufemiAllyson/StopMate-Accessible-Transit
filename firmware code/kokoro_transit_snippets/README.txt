KOKORO TRANSIT ANNOUNCEMENT SNIPPETS
Voice: American English Heart (af_heart). Speed: 1.0.
Generated locally on CPU using Kokoro-82M.
Format: 24,000 Hz, mono, 16-bit PCM WAV throughout.
The ESP32 build converts these source clips to 16,000 Hz in .pio/audio_data.
phrases/arriving_now.wav says "is arriving now" for API DUE/zero-minute predictions.

ASSEMBLY ORDER
phrases/bus.wav
numbers/NNN.wav
phrases/for_route.wav
routes/ROUTE.wav
phrases/will_arrive_in.wav
numbers/NNN.wav
phrases/minutes.wav (use minute.wav for 1)

Use the SAME number files for bus numbers and arrival times.
Numbers are zero-padded filenames, e.g. 005.wav says "five".
Includes 1-60, plus 75, 76, 118 and 126 for remaining RTS routes.
Routes: Butler Plaza to the Hub; Reitz Union to Liv Apartments.
Additional destination clips for The Standard Apartments stop 0440:
- routes/uf_health_shands.wav: "UF Health Shands" (bus 8, southbound).
- routes/cottage_grove.wav: "Cottage Grove" (bus 13, southbound).
These are destinations from stop 0440, rather than full endpoint-to-endpoint route names.
Generated with the same af_heart voice, speed 1.0, and PCM WAV format.
Recreate them using scripts/generate_stop_0440_audio.py in the project root.
Reitz is pronounced "rights"; Liv is pronounced "liv"; Apts. is spoken "Apartments".

The examples are assembled from these exact clips with 40 ms added between them.
Change the added gaps to taste; clips retain roughly 40 ms at each edge.
To splice WAVs, concatenate decoded PCM samples, not entire files with WAV headers.
manifest.json lists text, duration, synthesis pronunciation, and example sequences.

Kokoro model source: https://huggingface.co/hexgrad/Kokoro-82M
Model weights license: Apache-2.0.
