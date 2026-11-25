#!/usr/bin/env python3
"""Draw the co-op UI images: data/ui/*.png
- coop_icon: main menu item icon (white shape, the game tints it)
- toast_<kind>: notification panels (Spore-like dark blue glass, a coloured edge and icon)"""
import os
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(os.path.dirname(HERE), 'data', 'ui')
SS = 8  # supersampling


def person(d, cx, head_y, r, body_w, body_top, body_bottom, s, grow=0):
    g = grow
    d.ellipse([(cx - r - g) * s, (head_y - r - g) * s, (cx + r + g) * s, (head_y + r + g) * s], fill=255)
    d.rounded_rectangle([(cx - body_w - g) * s, (body_top - g) * s, (cx + body_w + g) * s, (body_bottom + g) * s],
                        radius=(body_w * 0.9 + g) * s, fill=255)


def coop_icon(size=62):
    s = SS
    mask = Image.new('L', (size * s, size * s), 0)
    d = ImageDraw.Draw(mask)
    person(d, 41, 15, 7, 11, 25, 46, s)                 # friend (back)
    front = (23, 22, 8.5, 14, 34, 58)
    person(d, *front, s, grow=3.5)                      # gap around the player...
    d2 = ImageDraw.Draw(mask)
    cut = Image.new('L', mask.size, 0)
    person(ImageDraw.Draw(cut), *front, s, grow=3.5)
    mask.paste(0, (0, 0), cut)
    person(d2, *front, s)                               # ...and the player (front)
    mask = mask.resize((size, size), Image.LANCZOS)
    img = Image.new('RGBA', (size, size), (255, 255, 255, 0))
    img.putalpha(mask)
    return img


TOAST_W, TOAST_H = 560, 58
# kind -> (accent colour, glyph)
TOASTS = {
    'info': ((90, 170, 255), 'i'),
    'join': ((96, 214, 120), 'plus'),
    'leave': ((240, 160, 64), 'minus'),
    'danger': ((255, 76, 76), 'bang'),
    'good': ((80, 226, 178), 'heart'),
    'vote': ((245, 197, 66), 'star'),
    'chat': ((150, 200, 255), 'bubble'),
}


def glyph(d, kind, cx, cy, r, s, fill):
    import math
    if kind == 'plus':
        w = r * 0.22
        d.rectangle([(cx - r * 0.6) * s, (cy - w) * s, (cx + r * 0.6) * s, (cy + w) * s], fill=fill)
        d.rectangle([(cx - w) * s, (cy - r * 0.6) * s, (cx + w) * s, (cy + r * 0.6) * s], fill=fill)
    elif kind == 'minus':
        w = r * 0.22
        d.rectangle([(cx - r * 0.6) * s, (cy - w) * s, (cx + r * 0.6) * s, (cy + w) * s], fill=fill)
    elif kind == 'bang':
        w = r * 0.17
        d.rounded_rectangle([(cx - w) * s, (cy - r * 0.62) * s, (cx + w) * s, (cy + r * 0.2) * s], radius=w * s, fill=fill)
        d.ellipse([(cx - w * 1.2) * s, (cy + r * 0.38) * s, (cx + w * 1.2) * s, (cy + r * 0.62) * s], fill=fill)
    elif kind == 'i':
        w = r * 0.16
        d.ellipse([(cx - w * 1.2) * s, (cy - r * 0.64) * s, (cx + w * 1.2) * s, (cy - r * 0.38) * s], fill=fill)
        d.rounded_rectangle([(cx - w) * s, (cy - r * 0.2) * s, (cx + w) * s, (cy + r * 0.62) * s], radius=w * s, fill=fill)
    elif kind == 'heart':
        pts = []
        for i in range(120):
            t = math.pi * 2 * i / 120
            x = 16 * math.sin(t) ** 3
            y = -(13 * math.cos(t) - 5 * math.cos(2 * t) - 2 * math.cos(3 * t) - math.cos(4 * t))
            pts.append(((cx + x * r / 26) * s, (cy + y * r / 26 + r * 0.05) * s))
        d.polygon(pts, fill=fill)
    elif kind == 'star':
        pts = []
        for i in range(10):
            a = -math.pi / 2 + i * math.pi / 5
            rr = r * (0.68 if i % 2 == 0 else 0.29)
            pts.append(((cx + math.cos(a) * rr) * s, (cy + math.sin(a) * rr) * s))
        d.polygon(pts, fill=fill)
    elif kind == 'bubble':
        d.rounded_rectangle([(cx - r * 0.62) * s, (cy - r * 0.48) * s, (cx + r * 0.62) * s, (cy + r * 0.3) * s],
                            radius=r * 0.25 * s, fill=fill)
        d.polygon([((cx - r * 0.3) * s, (cy + r * 0.2) * s), ((cx - r * 0.42) * s, (cy + r * 0.66) * s),
                   ((cx + r * 0.05) * s, (cy + r * 0.25) * s)], fill=fill)


def toast(kind):
    (ar, ag, ab), g = TOASTS[kind]
    s = SS
    W, H = TOAST_W, TOAST_H
    img = Image.new('RGBA', (W * s, H * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    rad = 14
    # glass panel with a soft vertical gradient
    panel = Image.new('RGBA', (W * s, H * s), (0, 0, 0, 0))
    grad = Image.new('RGBA', (1, H * s))
    for y in range(H * s):
        t = y / (H * s)
        grad.putpixel((0, y), (int(22 + 10 * (1 - t)), int(34 + 14 * (1 - t)), int(58 + 18 * (1 - t)), 222))
    grad = grad.resize((W * s, H * s))
    mask = Image.new('L', (W * s, H * s), 0)
    ImageDraw.Draw(mask).rounded_rectangle([0, 0, W * s - 1, H * s - 1], radius=rad * s, fill=255)
    panel.paste(grad, (0, 0), mask)
    img.alpha_composite(panel)
    # thin light rim and the coloured edge on the left
    d.rounded_rectangle([s, s, W * s - s - 1, H * s - s - 1], radius=(rad - 1) * s,
                        outline=(150, 190, 235, 110), width=int(1.5 * s))
    edge = Image.new('L', (W * s, H * s), 0)
    ImageDraw.Draw(edge).rounded_rectangle([0, 0, W * s - 1, H * s - 1], radius=rad * s, fill=255)
    bar = Image.new('L', (W * s, H * s), 0)
    ImageDraw.Draw(bar).rectangle([0, 0, 6 * s, H * s], fill=255)
    from PIL import ImageChops
    bar = ImageChops.multiply(bar, edge)
    img.paste(Image.new('RGBA', (W * s, H * s), (ar, ag, ab, 255)), (0, 0), bar)
    # icon: a glowing disc in the accent colour with a white glyph
    cx, cy, r = 36, H / 2, 19
    glow = Image.new('RGBA', (W * s, H * s), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse([(cx - r - 3) * s, (cy - r - 3) * s, (cx + r + 3) * s, (cy + r + 3) * s],
                                 fill=(ar, ag, ab, 70))
    img.alpha_composite(glow)
    d = ImageDraw.Draw(img)
    d.ellipse([(cx - r) * s, (cy - r) * s, (cx + r) * s, (cy + r) * s], fill=(ar, ag, ab, 255))
    d.ellipse([(cx - r + 2) * s, (cy - r + 2) * s, (cx + r - 2) * s, (cy - 1) * s],
              fill=(min(255, ar + 40), min(255, ag + 40), min(255, ab + 40), 90))  # highlight
    glyph(d, g, cx, cy, r, s, (255, 255, 255, 255))
    return img.resize((W, H), Image.LANCZOS)


def tooltip_bg(size=48):
    """9-slice Spore-style dark blue glass tooltip background (BUG-0874, StdDrawable 1/3 margins)."""
    s = SS
    W = H = size
    img = Image.new('RGBA', (W * s, H * s), (0, 0, 0, 0))
    rad = 10
    grad = Image.new('RGBA', (1, H * s))
    for y in range(H * s):
        t = y / (H * s)
        grad.putpixel((0, y), (int(18 + 14 * (1 - t)), int(32 + 18 * (1 - t)), int(62 + 24 * (1 - t)), 238))
    grad = grad.resize((W * s, H * s))
    mask = Image.new('L', (W * s, H * s), 0)
    ImageDraw.Draw(mask).rounded_rectangle([s, s, W * s - s - 1, H * s - s - 1], radius=rad * s, fill=255)
    img.paste(grad, (0, 0), mask)
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([s, s, W * s - s - 1, H * s - s - 1], radius=rad * s,
                        outline=(135, 200, 255, 200), width=int(1.6 * s))
    d.rounded_rectangle([int(2.6 * s), int(2.6 * s), W * s - int(2.6 * s) - 1, H * s - int(2.6 * s) - 1],
                        radius=(rad - 1.5) * s, outline=(200, 235, 255, 65), width=int(1.0 * s))
    return img.resize((W, H), Image.LANCZOS)


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    coop_icon().save(os.path.join(OUT, 'coop_icon.png'))
    print('wrote', os.path.join(OUT, 'coop_icon.png'))
    tooltip_bg().save(os.path.join(OUT, 'tooltip_bg.png'), optimize=True)
    print('wrote', os.path.join(OUT, 'tooltip_bg.png'))
    for k in TOASTS:
        p = os.path.join(OUT, 'toast_%s.png' % k)
        toast(k).save(p, optimize=True)
        print('wrote', p)
