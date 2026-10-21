#!/usr/bin/env python3
"""Joins the rendered clips and the Kokoro narration into the final mp4.
usage: assemble.py <workdir> <out.mp4>   workdir holds anim_1/4/9/10.mp4 (animated scenes), app_silent.mp4 + app/timeline.json (real app), narr/s00..s09.wav"""
import json, os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
W, OUT = sys.argv[1], sys.argv[2]
def dur(p): return float(subprocess.check_output(['ffprobe', '-v', 'error', '-show_entries', 'format=duration', '-of', 'csv=p=0', p]).decode())
tl = json.load(open(f'{W}/app/timeline.json'))
clips = [(f'{W}/anim_1.mp4', [('narr/s00.wav', 0.8)]), (f'{W}/anim_4.mp4', [('narr/s03.wav', 0.8)]), (f'{W}/app_silent.mp4', [(f'app/{a["file"]}', a['at']) for a in tl['audio']]),
         (f'{W}/anim_8.mp4', [('narr/s07.wav', 0.8)]), (f'{W}/anim_9.mp4', [('narr/s08.wav', 0.8)]), (f'{W}/anim_10.mp4', [('narr/s09.wav', 0.8)])]
texts = json.load(open(f'{HERE}/narration.json')); segs = json.load(open(f'{HERE}/app_script.json'))
caption = {'narr/s00.wav': texts[0], 'narr/s03.wav': texts[3], 'narr/s07.wav': texts[7], 'narr/s08.wav': texts[8], 'narr/s09.wav': texts[9], **{f'app/a{i:02d}.wav': g['text'] for i, g in enumerate(segs)}}
srt, sn = [], 0
def ts(x): return f'{int(x // 3600):02d}:{int(x % 3600 // 60):02d}:{int(x % 60):02d},{int(x % 1 * 1000):03d}'
args, vf, af, vl, al, t, ai = [], [], [], '', '', 0.0, 0
for ci, (v, auds) in enumerate(clips):
    args += ['-i', v]
for ci, (v, auds) in enumerate(clips):
    d = dur(v)
    vf.append(f'[{ci}:v]fade=t=in:st=0:d=0.4,fade=t=out:st={d - 0.4:.2f}:d=0.4[v{ci}]' if 'app' in v else f'[{ci}:v]null[v{ci}]'); vl += f'[v{ci}]'
    for f, at in auds:
        args += ['-i', f'{W}/{f}']; ms = int((t + at) * 1000)
        import re
        sents = [x.strip() for x in re.split(r'(?<=[.?!])\s+', caption[f].replace(' I D s', ' IDs').replace('R C A Eval', 'RCAEval').replace(' dot com slash Actually Anson slash ', '.com/ActuallyAnson/').replace('github.com/ActuallyAnson/Traceback', 'github.com/ActuallyAnson/TraceBack')) if x.strip()]
        ad = dur(f'{W}/{f}'); tot = sum(len(x) for x in sents); u = t + at
        for x in sents:
            d1 = ad * len(x) / tot; sn += 1; srt.append(f'{sn}\n{ts(u)} --> {ts(u + d1)}\n{x}\n'); u += d1
        af.append(f'[{len(clips) + ai}:a]aresample=48000,adelay={ms}|{ms}[a{ai}]'); al += f'[a{ai}]'; ai += 1
    t += d
fc = ';'.join(vf) + f';{vl}concat=n={len(clips)}:v=1:a=0[V];' + ';'.join(af) + f';{al}amix=inputs={ai}:normalize=0,loudnorm=I=-16:TP=-1.5,apad=whole_dur={t:.2f}[A]'
r = subprocess.run(['ffmpeg', '-y', *args, '-filter_complex', fc, '-map', '[V]', '-map', '[A]', '-c:v', 'libx264', '-crf', '17', '-pix_fmt', 'yuv420p', '-c:a', 'aac', '-b:a', '160k', '-movflags', '+faststart', '-shortest', OUT], capture_output=True, text=True)
open(OUT.replace('.mp4', '.srt'), 'w').write('\n'.join(srt))
print(r.returncode, r.stderr[-200:], 'total', round(t, 1))
