#!/usr/bin/env python3
"""Draw the co-op UI images: data/ui/*.png
- coop_icon: main menu item icon (white shape, the game tints it)
- toast_<kind>: notification panels (Spore-like dark blue glass, a coloured edge and icon)
- lineages_downed_panel: the downed screen's SPECTATING box (the Co-op window's panel colour, see-through, its inner
  boxes' edge, rounded; drawn cut in thirds by sui::std_drawable_tooltip, so the corners keep their size)
- lineages_button_<gold|red|grey|blue>_<normal|disabled|hover|pressed>: every button of the mod (the owner 2026-11-13: the game's
  gold and its shaded red "look a bit dim and grimy", brighter but not max brightness or saturation); the game's pill
  shape (a dark rim, a light top edge), drawn cut 40% from each side like the game's button (sui::std_drawable_states)
Names as arguments write only those images."""
import os
import sys
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


def downed_panel(third=12, radius=10, fill=(0x20, 0x3A, 0x4A, 0x66), edge=(0x78, 0x91, 0xA0, 0xFF)):
    """the owner (2026-11-12): like the menus, "Transparency at like 60%", rounded; the Co-op window's panel is
    #203A4A (its frame's content window), its inner boxes' edge #7891A0"""
    size, s = third * 3, SS
    img = Image.new('RGBA', (size * s, size * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, size * s - 1, size * s - 1], radius=radius * s, fill=edge)
    d.rounded_rectangle([s, s, (size - 1) * s - 1, (size - 1) * s - 1], radius=(radius - 1) * s, fill=(0, 0, 0, 0))
    inner = Image.new('RGBA', img.size, (0, 0, 0, 0))
    ImageDraw.Draw(inner).rounded_rectangle([s, s, (size - 1) * s - 1, (size - 1) * s - 1], radius=(radius - 1) * s,
                                            fill=fill)
    img.alpha_composite(inner)
    return img.resize((size, size), Image.LANCZOS)


# the body's top / middle / bottom colours of each look (normal); the other states derive from them
BUTTON_LOOKS = {
    'gold': ((246, 214, 98), (224, 184, 58), (184, 146, 34)),
    'red': ((226, 92, 74), (198, 60, 46), (150, 38, 30)),
    # BUG-9220 (the owner 2026-11-15): grey around #2B2F36 (the helpers), a Spore-like blue (the page buttons)
    'grey': ((70, 76, 86), (43, 47, 54), (30, 33, 38)),
    'blue': ((92, 160, 232), (52, 118, 204), (32, 82, 156)),
}
BUTTON_STATES = ('normal', 'disabled', 'hover', 'pressed')  # StdDrawable images[0..3]


def _shade(c, k):
    return tuple(max(0, min(255, round(v * k))) for v in c)


def _grey(c, amount):
    g = sum(c) / 3
    return tuple(round(v + (g - v) * amount) for v in c)


def button(look, state, w=50, h=30):
    """a pill: a 1 px dark rim, a vertical gradient body, a light line under the top rim (as the game's buttons)"""
    top, mid, bot = BUTTON_LOOKS[look]
    k = {'normal': 1.0, 'hover': 1.12, 'pressed': 0.82, 'disabled': 0.72}[state]
    if look == 'grey' and state == 'hover':
        k = 1.35  # a dark body needs a bigger step to show the hover
    top, mid, bot = (_shade(c, k) for c in (top, mid, bot))
    if state == 'disabled':
        top, mid, bot = (_grey(c, 0.65) for c in (top, mid, bot))
    s = SS
    W, H = w * s, h * s
    r = H // 2
    rim = Image.new('L', (W, H), 0)
    ImageDraw.Draw(rim).rounded_rectangle([0, 0, W - 1, H - 1], radius=r, fill=255)
    body = Image.new('L', (W, H), 0)
    ImageDraw.Draw(body).rounded_rectangle([s, s, W - 1 - s, H - 1 - s], radius=r - s, fill=255)
    grad = Image.new('RGBA', (W, H))
    gd = ImageDraw.Draw(grad)
    for y in range(H):
        t = y / (H - 1)
        a, b, u = (top, mid, t / 0.5) if t < 0.5 else (mid, bot, (t - 0.5) / 0.5)
        gd.line([(0, y), (W, y)], fill=tuple(round(a[i] + (b[i] - a[i]) * u) for i in range(3)) + (255,))
    img = Image.new('RGBA', (W, H), (17, 17, 17, 0))
    img.paste((17, 17, 17, 255), (0, 0), rim)
    img.paste(grad, (0, 0), body)
    shine = Image.new('L', (W, H), 0)  # the light edge under the top rim
    ImageDraw.Draw(shine).rounded_rectangle([s, s, W - 1 - s, H - 1 - s], radius=r - s, outline=255, width=s)
    cut = Image.new('L', (W, H), 0)
    ImageDraw.Draw(cut).rectangle([0, 0, W, int(H * 0.45)], fill=255)
    shine = Image.composite(shine, Image.new('L', (W, H), 0), cut)
    light = tuple(min(255, round(v + (255 - v) * 0.7)) for v in top) + (255,)
    img.paste(light, (0, 0), shine)
    return img.resize((w, h), Image.LANCZOS)


if __name__ == '__main__':
    os.makedirs(OUT, exist_ok=True)
    want = set(sys.argv[1:])

    def write(name, img, **kw):
        if want and name not in want:
            return
        p = os.path.join(OUT, name + '.png')
        img.save(p, **kw)
        print('wrote', p)

    write('coop_icon', coop_icon())
    for k in TOASTS:
        write('toast_%s' % k, toast(k), optimize=True)
    write('lineages_downed_panel', downed_panel(), optimize=True)
    for look in BUTTON_LOOKS:
        for state in BUTTON_STATES:
            write('lineages_button_%s_%s' % (look, state), button(look, state), optimize=True)
