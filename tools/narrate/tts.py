#!/usr/bin/env python3
"""Synthesizes tools/narrate/narration.json with Kokoro (af_heart), one wav per scene, and writes timing.json.
Run inside a venv with kokoro-onnx + soundfile; cwd must hold kokoro-v1.0.onnx and voices-v1.0.bin. usage: tts.py <narration.json> <outdir>"""
import json, os, sys
import soundfile as sf
from kokoro_onnx import EspeakConfig, Kokoro

src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
cfg = EspeakConfig(lib_path="/opt/homebrew/lib/libespeak-ng.1.dylib", data_path="/opt/homebrew/share/espeak-ng-data")
k = Kokoro("kokoro-v1.0.onnx", "voices-v1.0.bin", espeak_config=cfg)
dur = []
for i, t in enumerate(json.load(open(src))):
    s, sr = k.create(t, voice="af_heart", speed=1.0, lang="en-us")
    sf.write(f"{out}/s{i:02d}.wav", s, sr)
    dur.append(round(len(s) / sr, 2))
    print(i, dur[-1], flush=True)
json.dump(dur, open(f"{out}/timing.json", "w"))
print("total", sum(dur))
