#!/usr/bin/env python3
"""Turns app_script.json into a timeline for app_clip.mjs: synthesizes each segment with Kokoro (run inside the kokoro venv, cwd = model dir),
lays the segments end to end, and writes <out>/timeline.json with absolute action times, audio offsets and total length.
usage: build_app.py <app_script.json> <outdir>"""
import json, os, sys
import soundfile as sf
from kokoro_onnx import EspeakConfig, Kokoro

src, out = sys.argv[1], sys.argv[2]
os.makedirs(out, exist_ok=True)
cfg = EspeakConfig(lib_path="/opt/homebrew/lib/libespeak-ng.1.dylib", data_path="/opt/homebrew/share/espeak-ng-data")
k = Kokoro("kokoro-v1.0.onnx", "voices-v1.0.bin", espeak_config=cfg)
t, actions, audio = 0.0, [], []
for i, seg in enumerate(json.load(open(src))):
    s, sr = k.create(seg["text"], voice="af_heart", speed=1.0, lang="en-us")
    sf.write(f"{out}/a{i:02d}.wav", s, sr)
    d = len(s) / sr
    last = max([a["t"] for a in seg["actions"]] + [0])
    audio.append({"file": f"a{i:02d}.wav", "at": round(t + 0.3, 2)})
    for a in seg["actions"]:
        actions.append({**a, "t": round(t + a["t"], 2)})
    t += max(d + 0.6, last + 2.4)
    print(i, round(d, 2), round(t, 2), flush=True)
json.dump({"length": round(t, 2), "actions": actions, "audio": audio}, open(f"{out}/timeline.json", "w"), indent=1)
