#!/usr/bin/env python3
"""Burns the .srt next to a video into it (this ffmpeg build has no subtitles filter, so captions are drawn with Pillow and overlaid).
usage: burn_captions.py <in.mp4> <in.srt> <out.mp4>   (system python3 with Pillow)"""
import re, subprocess, sys, tempfile, os
from PIL import Image, ImageDraw, ImageFont
vid, srt, out = sys.argv[1:4]
def sec(x): h, m, s = x.replace(',', '.').split(':'); return int(h) * 3600 + int(m) * 60 + float(s)
cues = [(sec(m[1]), sec(m[2]), m[3].strip()) for m in re.finditer(r'\d+\n(\S+) --> (\S+)\n(.*?)(?:\n\n|\Z)', open(srt).read(), re.S)]
font = ImageFont.truetype('/System/Library/Fonts/Supplemental/Arial Bold.ttf', 46)
tmp = tempfile.mkdtemp(); args, fc, last = [], [], '[0:v]'
def wrap(t, w=1480):
    lines, cur = [], ''
    for word in t.split():
        if font.getlength((cur + ' ' + word).strip()) > w and cur: lines.append(cur); cur = word
        else: cur = (cur + ' ' + word).strip()
    return lines + [cur]
for i, (a, b, t) in enumerate(cues):
    ls = wrap(t); h = 30 + 62 * len(ls); w = int(max(font.getlength(l) for l in ls)) + 70
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0)); d = ImageDraw.Draw(im); d.rounded_rectangle((0, 0, w - 1, h - 1), 18, fill=(6, 10, 12, 215))
    for k, l in enumerate(ls): d.text((w // 2, 16 + 62 * k + 28), l, font=font, fill=(255, 255, 255, 255), anchor='mm')
    p = f'{tmp}/c{i}.png'; im.save(p); args += ['-i', p]
    nxt = f'[o{i}]'; fc.append(f'{last}[{i + 1}:v]overlay=x=(W-w)/2:y=H-h-70:enable=\'between(t,{a:.2f},{b:.2f})\'{nxt}'); last = nxt
r = subprocess.run(['ffmpeg', '-y', '-i', vid, *args, '-filter_complex', ';'.join(fc), '-map', last, '-map', '0:a', '-c:v', 'libx264', '-crf', '18', '-pix_fmt', 'yuv420p', '-c:a', 'copy', '-movflags', '+faststart', out], capture_output=True, text=True)
print(r.returncode, r.stderr[-150:])
