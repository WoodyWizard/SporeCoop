#!/usr/bin/env python3
"""Draw the images of the Interface Size slider and the Frame Rate field (Spore's Settings, Graphic page) into data/ui/.

Spore's own sliders (the Audio page's) are vertical: a groove with ticks (the slider drawable's "thumb container"
image, 38x130) and a gold knob (27x13). The owner's layout (images/uiscale_mockup_owner.png, decision 3I) has a
horizontal one, so the groove lies on its side and the knob stands up. Colours sampled from the Audio page at 1280x960
(2026-11-13: the knob's rows, the groove's columns); the UI draws at 1:1, so these are drawn at their final size.

usage: py tools/settings_ui_assets.py
"""
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'data' / 'ui'

TRACK_W, TRACK_H = 260, 30      # the slider window's size in coop_ui.cpp (kSliderW, kSliderH)
KNOB_W, KNOB_H = 13, 27
TICKS = 7                       # 50, 75, ... 200 %

# the native knob, top to bottom (its middle column), between the 2-pixel black outline
KNOB_ROWS = [(0xde, 0xdb, 0xca), (0xe3, 0xdf, 0xcf), (0xaf, 0xa2, 0x6c), (0x97, 0x84, 0x39), (0x9b, 0x88, 0x3a),
             (0x99, 0x86, 0x39), (0x85, 0x75, 0x32), (0x7f, 0x6f, 0x30), (0x83, 0x73, 0x31)]
GROOVE_EDGE, GROOVE_CORE, GROOVE_LIGHT = (0x1a, 0x29, 0x34, 255), (0x1d, 0x2d, 0x3c, 255), (0x3a, 0x4d, 0x5c, 255)
TICK_DARK, TICK_LIGHT = (0x1a, 0x29, 0x34, 230), (0x3a, 0x4d, 0x5c, 200)


def knob():
    """the native knob turned upright: its light top becomes the left side"""
    w, h = KNOB_H, KNOB_W  # drawn lying (as the native one), turned at the end
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    p = im.load()
    for y in range(h):
        for x in range(w):
            corner = (x in (0, w - 1)) and (y in (0, h - 1))
            if corner:
                continue
            if x < 2 or x >= w - 2 or y < 2 or y >= h - 2:
                p[x, y] = (0, 0, 0, 255)
                continue
            c = KNOB_ROWS[min(len(KNOB_ROWS) - 1, y - 2)]
            if x >= w - 5:  # the native knob's darker right end
                c = tuple(int(v * 0.8) for v in c)
            elif x < 4:
                c = tuple(min(255, int(v * 1.08)) for v in c)
            p[x, y] = c + (255,)
    return im.rotate(90, expand=True)


def track():
    im = Image.new('RGBA', (TRACK_W, TRACK_H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    cy = TRACK_H // 2
    # the groove: as long as the knob travels (its middle from KNOB_W / 2 to the far end)
    x0, x1 = KNOB_W // 2 - 3, TRACK_W - KNOB_W // 2 + 2
    d.rounded_rectangle((x0, cy - 4, x1, cy + 4), 4, fill=GROOVE_EDGE)
    d.rounded_rectangle((x0 + 1, cy - 3, x1 - 1, cy + 2), 3, fill=GROOVE_CORE)
    d.line((x0 + 3, cy + 4, x1 - 3, cy + 4), fill=GROOVE_LIGHT)
    for i in range(TICKS):
        x = KNOB_W // 2 + round((TRACK_W - KNOB_W) * i / (TICKS - 1))
        for y0 in (cy - 12, cy + 8):
            d.line((x, y0, x, y0 + 3), fill=TICK_DARK)
            d.line((x + 1, y0, x + 1, y0 + 3), fill=TICK_LIGHT)
    return im


def knob_states():
    """the slider's thumb image is a strip of 4 states side by side (normal, disabled, hover, pressed), as the
    native one (160x27: four 40x27 frames; WinSlider sizes the thumb by a quarter of the image's width, seen live
    2026-11-13 when a single 13x27 knob drew 3 pixels wide)"""
    k = knob()
    w, h = k.size
    strip = Image.new('RGBA', (w * 4, h), (0, 0, 0, 0))
    def tint(im, f, grey=False):
        out = im.copy()
        p = out.load()
        for y in range(h):
            for x in range(w):
                r, g, b, a = p[x, y]
                if grey:
                    v = (r + g + b) // 3
                    r, g, b = v, v, v
                p[x, y] = (min(255, int(r * f)), min(255, int(g * f)), min(255, int(b * f)), a)
        return out
    # the order of the frames is not known: the idle knob drew the second frame (2026-11-13), so the first two are the
    # plain knob, then a brighter (hover) and a darker (pressed) one
    for i, im in enumerate((k, k, tint(k, 1.18), tint(k, 0.85))):
        strip.paste(im, (i * w, 0))
    return strip


FIELD_W, FIELD_H = 56, 27        # the Frame Rate field (coop_ui.cpp field_area)
# the Graphic Cache Size field, sampled at 1280x960: a grey 1-pixel rounded edge, a glossy top half, a black bottom
FIELD_EDGE = (0x82, 0x8a, 0x90, 255)
FIELD_TOP = [(0x31, 0x3b, 0x42), (0x38, 0x43, 0x4b)]
FIELD_BOTTOM = [(0x19, 0x20, 0x26), (0x10, 0x17, 0x1c)]
FIELD_FOOT = (0x23, 0x27, 0x2c)


def field():
    im = Image.new('RGBA', (FIELD_W, FIELD_H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, FIELD_W - 1, FIELD_H - 1), 3, fill=FIELD_EDGE)
    inner = Image.new('RGBA', (FIELD_W - 2, FIELD_H - 2), (0, 0, 0, 0))
    p = inner.load()
    h = inner.height
    for y in range(h):
        if y < h // 2:
            a, b = FIELD_TOP
            t = y / max(1, h // 2 - 1)
        elif y < h - 3:
            a, b = FIELD_BOTTOM
            t = (y - h // 2) / max(1, h - 3 - h // 2)
        else:
            a = b = FIELD_FOOT
            t = 0
        c = tuple(int(a[i] * (1 - t) + b[i] * t) for i in range(3)) + (255,)
        for x in range(inner.width):
            p[x, y] = c
    m = Image.new('L', inner.size, 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, inner.width - 1, inner.height - 1), 2, fill=255)
    im.paste(inner, (1, 1), m)
    return im


def arrows():
    """the cache field's up / down arrows: two silver triangles with a dark rim, 27x28 as its spinner window"""
    w, h = 27, 28
    S = 4
    big = Image.new('RGBA', (w * S, h * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(big)
    for tri, top, bottom in (([(13.5, 3), (6.5, 12), (20.5, 12)], 0xe6, 0x9a),
                             ([(6.5, 16), (20.5, 16), (13.5, 25)], 0xc8, 0x78)):
        pts = [(x * S, y * S) for x, y in tri]
        d.polygon([(x, y + S) for x, y in pts], fill=(0x0c, 0x12, 0x17, 170))  # its shadow
        d.polygon(pts, fill=(0x10, 0x17, 0x1c, 255))
        inner = [((x - 13.5 * S) * 0.78 + 13.5 * S, (y - sum(q[1] for q in pts) / 3) * 0.78 + sum(q[1] for q in pts) / 3)
                 for x, y in pts]
        ys = [q[1] for q in inner]
        y0, y1 = min(ys), max(ys)
        mask = Image.new('L', big.size, 0)
        ImageDraw.Draw(mask).polygon(inner, fill=255)
        grad = Image.new('RGBA', big.size, (0, 0, 0, 0))
        gp = ImageDraw.Draw(grad)
        for y in range(int(y0), int(y1) + 1):
            t = (y - y0) / max(1, y1 - y0)
            v = int(top * (1 - t) + bottom * t)
            gp.line((0, y, big.width, y), fill=(v, v, min(255, v + 6), 255))
        big.paste(grad, (0, 0), mask)
    return big.resize((w, h), Image.LANCZOS)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    knob_states().save(OUT / 'lineages_slider_knob_h.png')
    track().save(OUT / 'lineages_slider_track_h.png')
    field().save(OUT / 'lineages_field_box.png')
    arrows().save(OUT / 'lineages_field_arrows.png')
    print('wrote the slider, field and arrow images into', OUT)


if __name__ == '__main__':
    main()
