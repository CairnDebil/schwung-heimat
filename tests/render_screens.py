#!/usr/bin/env python3
"""Turn tests/screens/ops.json (drawing commands recorded by ui_test.mjs) into PNG screenshots.

Shapes are exact; text is drawn with Pillow's built-in font at a 6 px advance, so the real
Move font will look slightly different. Usage: python3 tests/render_screens.py [out_dir]
"""
import json, os, sys
from PIL import Image, ImageDraw, ImageFont

here = os.path.dirname(os.path.abspath(__file__))
out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, '..', 'docs', 'screens')
os.makedirs(out, exist_ok=True)
shots = json.load(open(os.path.join(here, 'screens', 'ops.json')))
W, H, S = 128, 64, 6
FONT_PATHS = ['/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf', '/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf']
font = None
for fp in FONT_PATHS:                       # a monospaced font whose advance at size 10 is 6 px, like the Move's
    if os.path.exists(fp):
        font = ImageFont.truetype(fp, 10)
        break
if font is None:
    font = ImageFont.load_default()

def glyph(ch):
    im = Image.new('L', (8, 12), 0)
    ImageDraw.Draw(im).text((0, -2), ch, fill=255, font=font)
    return im

def render(ops):
    fb = Image.new('1', (W, H), 0)
    d = ImageDraw.Draw(fb)
    for op in ops:
        k = op[0]
        if k == 'f':
            _, x, y, w, h, v = op
            if w > 0 and h > 0: d.rectangle([x, y, x + w - 1, y + h - 1], fill=1 if v else 0)
        elif k == 'r':
            _, x, y, w, h, v = op
            if w > 0 and h > 0: d.rectangle([x, y, x + w - 1, y + h - 1], outline=1 if v else 0)
        elif k == 'p':
            _, x, y, v = op
            if 0 <= x < W and 0 <= y < H: fb.putpixel((int(x), int(y)), 1 if v else 0)
        elif k == 't':
            _, x, y, s, c = op
            for i, ch in enumerate(s):
                g = glyph(ch)
                for gy in range(9):
                    for gx in range(6):
                        if g.getpixel((gx, gy)) > 100:
                            X, Y = int(x) + 6 * i + gx, int(y) + gy
                            if 0 <= X < W and 0 <= Y < H: fb.putpixel((X, Y), 1 if c else 0)
    return fb

def oled(fb):
    im = Image.new('RGB', (W * S + 8, H * S + 8), (6, 8, 14))
    d = ImageDraw.Draw(im)
    for y in range(H):
        for x in range(W):
            on = fb.getpixel((x, y))
            col = (226, 238, 255) if on else (14, 18, 28)
            d.rectangle([4 + x * S, 4 + y * S, 4 + x * S + S - 2, 4 + y * S + S - 2], fill=col)
    return im

names = list(shots.keys())
imgs = {}
for n in names:
    imgs[n] = oled(render(shots[n]))
    imgs[n].save(os.path.join(out, n + '.png'))
print('schermate:', ', '.join(names))

# contact sheet
cols = 2
cw, ch = imgs[names[0]].size
rows = (len(names) + cols - 1) // cols
sheet = Image.new('RGB', (cols * (cw + 20) + 20, rows * (ch + 44) + 20), (30, 32, 40))
sd = ImageDraw.Draw(sheet)
for i, n in enumerate(names):
    x = 20 + (i % cols) * (cw + 20); y = 20 + (i // cols) * (ch + 44)
    sd.text((x, y), n, fill=(200, 210, 230), font=ImageFont.load_default(size=14) if hasattr(ImageFont, 'load_default') else None)
    sheet.paste(imgs[n], (x, y + 24))
sheet.save(os.path.join(out, 'overview.png'))
print('panoramica: overview.png', sheet.size)
