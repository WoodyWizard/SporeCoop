#!/usr/bin/env python3
"""Draw the Party cards' images per stage into data/ui/ (decision 3G: a native-style card per partner, in the art of
the stage that partner plays).

A card: a plate (the name, the place, the health bar and a second bar) and a picture frame to its right; a "Party" tab
over the top card. Each stage has the look of its own HUD (sampled from 1:1 screenshots at 1024x768, 2026-11-14):
Cell the light blue-grey metal card with a purple-blue well, Creature the dark stone avatar card, Tribe and Civ the
gold-framed member squares and the glossy blue tab ("Tribe", "Military"), Space the captain card's blue scanlines. The
images are drawn here from those colours (no game pixels); the mod draws the text, the bars and the picture.

usage: py tools/party_card_assets.py                      (the game's images, data/ui)
       py tools/party_card_assets.py --layers <folder>   (every layer at 1x and 4x, per stage: for designing)
"""
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'data' / 'ui'
S = 4  # drawn at 4x, scaled down (smooth edges)

PLATE = (128, 67)   # coop_ui.cpp CardLayout: plate_w, card_h - plate_dy - plate_cut
FRAME = (71, 71)    # frame_w, card_h
TAB = (92, 30)      # tab_w, tab_h
BAR = (97, 8)       # bar_w, bar_h: the second bars' fill (the health bar uses lineages_party_bar_*)

STYLES = {
    'cell': dict(plate=(0x97, 0xA5, 0xC5, 240), plate_edge=(0x5E, 0x6E, 0x94, 255), plate_hi=(0xDD, 0xE4, 0xF0, 255),
                 frame=(0x8A, 0x98, 0xBA), frame_edge=(0x5E, 0x6E, 0x94), frame_hi=(0xDD, 0xE4, 0xF0),
                 well=((0x58, 0x66, 0xA8), (0x38, 0x36, 0x6E)), well_rim=(0x5F, 0xD8, 0xF0), scan=False,
                 tab=((0x8E, 0xA8, 0xD8), (0x3E, 0x5E, 0x9C)), tab_edge=(0xDD, 0xE4, 0xF0)),
    'creature': dict(plate=(0x2C, 0x2C, 0x2A, 235), plate_edge=(0x76, 0x7A, 0x78, 255), plate_hi=(0x9C, 0xA6, 0xA6, 255),
                     frame=(0x6E, 0x6A, 0x5E), frame_edge=(0x34, 0x32, 0x2C), frame_hi=(0xB0, 0xAB, 0x98),
                     well=((0x4A, 0x45, 0x3A), (0x2A, 0x27, 0x21)), well_rim=(0x22, 0x20, 0x1C), scan=False,
                     tab=((0x88, 0x88, 0x86), (0x52, 0x52, 0x50)), tab_edge=(0xD8, 0xDC, 0xDC)),
    'tribe': dict(plate=(0x1E, 0x22, 0x26, 225), plate_edge=(0xDC, 0xD5, 0xC3, 255), plate_hi=(0xF0, 0xEA, 0xD8, 255),
                  frame=(0xDC, 0xD5, 0xC3), frame_edge=(0x6E, 0x5A, 0x2C), frame_hi=(0xE8, 0xC8, 0x70),
                  well=((0x6A, 0x6C, 0x72), (0x40, 0x42, 0x46)), well_rim=(0x2A, 0x2C, 0x30), scan=False,
                  tab=((0xA7, 0xD3, 0xFF), (0x0E, 0x4A, 0x78)), tab_edge=(0xC8, 0xE4, 0xF4)),
    'space': dict(plate=(0x34, 0x36, 0x34, 240), plate_edge=(0x76, 0x82, 0x83, 255), plate_hi=(0x96, 0xA3, 0xA4, 255),
                  frame=(0x66, 0x68, 0x66), frame_edge=(0x2A, 0x2C, 0x2A), frame_hi=(0xA8, 0xAE, 0xAC),
                  well=((0x30, 0x52, 0x7A), (0x14, 0x24, 0x38)), well_rim=(0x0A, 0x12, 0x1C), scan=True,
                  tab=((0x3C, 0x78, 0xD0), (0x12, 0x36, 0x78)), tab_edge=(0x9C, 0xC8, 0xF0)),
}
STYLES['civ'] = STYLES['tribe']  # the Civ HUD's member squares and tab are the Tribe ones

SECOND_BARS = {'orange': ((0xFF, 0xB4, 0x3C), (0xC8, 0x64, 0x08)), 'blue': ((0x6C, 0xD8, 0xFF), (0x10, 0x8C, 0xCC))}


def big(size):
    return Image.new('RGBA', (size[0] * S, size[1] * S), (0, 0, 0, 0))


def small(im, size):
    return im.resize(size, Image.LANCZOS)


def gradient(size, top, bottom, alpha=255):
    im = Image.new('RGBA', size, (0, 0, 0, 0))
    p = ImageDraw.Draw(im)
    for y in range(size[1]):
        t = y / max(1, size[1] - 1)
        c = tuple(int(top[i] * (1 - t) + bottom[i] * t) for i in range(3)) + (alpha,)
        p.line((0, y, size[0], y), fill=c)
    return im


def plate(st):
    w, h = PLATE
    im = big(PLATE)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, w * S - 1, h * S - 1), 9 * S, fill=st['plate_edge'])
    d.rounded_rectangle((S, S, w * S - 1 - S, h * S - 1 - S), 8 * S, fill=st['plate'])
    d.line((10 * S, int(1.5 * S), (w - 14) * S, int(1.5 * S)), fill=st['plate_hi'], width=S)
    return small(im, PLATE)


def frame(st, hover=False):
    """the picture frame; `hover`: its edge lit (the card the pointer is over)"""
    if hover:
        st = dict(st, frame_edge=(0xF4, 0xF0, 0xD8), frame_hi=(0xFF, 0xFF, 0xFF),
                  frame=tuple(min(255, int(v * 1.18) + 12) for v in st['frame']))
    w, h = FRAME
    im = big(FRAME)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, w * S - 1, h * S - 1), 9 * S, fill=st['frame_edge'])
    d.rounded_rectangle((S, S, w * S - 1 - S, h * S - 1 - S), 8 * S, fill=st['frame'])
    d.line((9 * S, int(1.5 * S), (w - 9) * S, int(1.5 * S)), fill=st['frame_hi'], width=S)
    # the well (the picture goes over it)
    x0, y0, x1, y1 = 5 * S, 5 * S, (w - 5) * S, (h - 5) * S
    well = gradient((x1 - x0, y1 - y0), *st['well'])
    if st['scan']:  # the Space captain card's scanlines
        wp = ImageDraw.Draw(well)
        for y in range(0, well.height, 2 * S):
            wp.line((0, y, well.width, y), fill=st['well'][1] + (255,), width=S)
    m = Image.new('L', well.size, 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, well.width - 1, well.height - 1), 6 * S, fill=255)
    d.rounded_rectangle((x0 - S, y0 - S, x1 + S, y1 + S), 7 * S, fill=st['well_rim'])
    im.paste(well, (x0, y0), m)
    return small(im, FRAME)


def tab(st):
    w, h = TAB
    im = big(TAB)
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, w * S - 1, h * S - 1), h * S // 2, fill=st['tab_edge'])
    inner = gradient((w * S - 4 * S, h * S - 4 * S), *st['tab'])
    m = Image.new('L', inner.size, 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, inner.width - 1, inner.height - 1), inner.height // 2, fill=255)
    im.paste(inner, (2 * S, 2 * S), m)
    gloss = Image.new('RGBA', (inner.width, inner.height // 2), (255, 255, 255, 60))
    gm = Image.new('L', gloss.size, 0)
    ImageDraw.Draw(gm).rounded_rectangle((S, S, gloss.width - S, gloss.height + gloss.height), gloss.height, fill=255)
    im.alpha_composite(Image.composite(gloss, Image.new('RGBA', gloss.size, (0, 0, 0, 0)), gm), (2 * S, 2 * S))
    return small(im, TAB)


def bar_fill(top, bottom):
    w, h = BAR
    im = big(BAR)
    g = gradient((w * S, h * S), top, bottom)
    m = Image.new('L', g.size, 0)
    ImageDraw.Draw(m).rounded_rectangle((0, 0, g.width - 1, g.height - 1), g.height // 2, fill=255)
    im.paste(g, (0, 0), m)
    return small(im, BAR)


def icon(kind):
    """the bars' icons: a white cross (health), a bolt (Space energy), a bite (Creature hunger)"""
    im = big((12, 12))
    d = ImageDraw.Draw(im)
    white, dark = (255, 255, 255, 255), (20, 24, 28, 200)
    if kind == 'health':
        for o, c in ((S, dark), (0, white)):
            d.rectangle((4 * S + o, 1 * S + o, 8 * S + o, 11 * S + o), fill=c)
            d.rectangle((1 * S + o, 4 * S + o, 11 * S + o, 8 * S + o), fill=c)
    elif kind == 'energy':
        pts = [(7, 0), (2, 7), (6, 7), (4, 12), (10, 4), (6, 4), (8, 0)]
        d.polygon([(x * S + S, y * S + S) for x, y in pts], fill=dark)
        d.polygon([(x * S, y * S) for x, y in pts], fill=white)
    else:  # hunger: a round meal with a bite out of it
        d.ellipse((1 * S + S, 1 * S + S, 11 * S + S, 11 * S + S), fill=dark)
        d.ellipse((1 * S, 1 * S, 11 * S, 11 * S), fill=white)
        d.ellipse((7 * S, -1 * S, 13 * S, 5 * S), fill=(0, 0, 0, 0))
    return small(im, (12, 12))


def export_layers(dest):
    """the owner's own designs (2026-11-14: "send me the layers ... so I can try and create them myself"): every
    image at the game's size and at 4x (drawn at 4x here, so not blown up), per stage in its own folder"""
    import shutil
    for name, st in STYLES.items():
        d1, d4 = dest / name / '1x', dest / name / '4x'
        d1.mkdir(parents=True, exist_ok=True)
        d4.mkdir(parents=True, exist_ok=True)
        global small
        keep = small
        for scale_dir, fn in ((d1, keep), (d4, lambda im, size: im)):
            small = fn
            plate(st).save(scale_dir / 'plate.png')
            frame(st).save(scale_dir / 'frame.png')
            frame(st, hover=True).save(scale_dir / 'frame_hover.png')
            tab(st).save(scale_dir / 'tab.png')
        small = keep
    shared1, shared4 = dest / 'shared' / '1x', dest / 'shared' / '4x'
    shared1.mkdir(parents=True, exist_ok=True)
    shared4.mkdir(parents=True, exist_ok=True)
    for name, (top, bottom) in SECOND_BARS.items():
        b = bar_fill(top, bottom)
        b.save(shared1 / f'bar_fill_{name}.png')
        b.resize((b.width * 4, b.height * 4), Image.LANCZOS).save(shared4 / f'bar_fill_{name}.png')
    for kind in ('health', 'energy', 'hunger'):
        i = icon(kind)
        i.save(shared1 / f'icon_{kind}.png')
        i.resize((48, 48), Image.LANCZOS).save(shared4 / f'icon_{kind}.png')
    for stem in ('bar_back', 'bar_fill', 'bar_ring'): # the health bar (tools/party_ui_assets.py: the native bar's pixels)
        src = OUT / f'lineages_party_{stem}.png'
        if src.exists():
            shutil.copy(src, shared1 / f'health_{stem}.png')
            im = Image.open(src)
            im.resize((im.width * 4, im.height * 4), Image.NEAREST).save(shared4 / f'health_{stem}.png')


def main():
    import sys
    if len(sys.argv) > 2 and sys.argv[1] == '--layers':
        export_layers(Path(sys.argv[2]))
        print('wrote the layers into', sys.argv[2])
        return
    OUT.mkdir(parents=True, exist_ok=True)
    for name, st in STYLES.items():
        plate(st).save(OUT / f'lineages_card_plate_{name}.png')
        frame(st).save(OUT / f'lineages_card_frame_{name}.png')
        frame(st, hover=True).save(OUT / f'lineages_card_frame_{name}_hover.png')
        tab(st).save(OUT / f'lineages_card_tab_{name}.png')
    for name, (top, bottom) in SECOND_BARS.items():
        bar_fill(top, bottom).save(OUT / f'lineages_card_bar_{name}.png')
    for kind in ('health', 'energy', 'hunger'):
        icon(kind).save(OUT / f'lineages_card_icon_{kind}.png')
    print('wrote the Party card images into', OUT)


if __name__ == '__main__':
    main()
