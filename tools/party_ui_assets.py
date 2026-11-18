#!/usr/bin/env python3
"""Draw the Party panel's background images into data/ui/ after the owner's mockup (images/partymockup.psd).

The owner drew the panel after the game's own Pack cards (2026-11-13): a "Party" header pill, one name plate per
player (the own one with a gold edge) and a gold-framed parchment card for the creature's portrait with a health bar.
The PSD's layer pixels are compressed in a way Pillow cannot read (only the merged image), so the backgrounds are
drawn here with the mockup's own colours (sampled from the merged image, 1280x960 canvas); the live parts (the name,
the location, the portrait, the health) are drawn by the mod (coop_ui.cpp, set_party_hud).

usage: py tools/party_ui_assets.py
"""
import random
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'data' / 'ui'
S = 4  # drawn at 4x, scaled down (smooth edges)

PLATE = (102, 102, 102, 153)  # the name plates: 60% grey
HEADER = (99, 99, 99, 156)    # the "Party" pill
GOLD_EDGE = (253, 216, 109, 255)
# the native Pack pill's thin light edge (the owner 2026-11-13: the other plates and the header lacked it)
WHITE_EDGE = (235, 240, 245, 200)
RIM_DARK, RIM_MID, RIM_LIGHT = (105, 86, 35), (175, 144, 59), (255, 236, 186)
PARCHMENT = (188, 182, 164)


def rounded(size, radius, fill, edge=None, edge_w=1):
    w, h = size
    im = Image.new('RGBA', (w * S, h * S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    if edge:
        d.rounded_rectangle((0, 0, w * S - 1, h * S - 1), radius * S, fill=edge)
        e = edge_w * S
        d.rounded_rectangle((e, e, w * S - 1 - e, h * S - 1 - e), max(1, radius * S - e), fill=fill)
    else:
        d.rounded_rectangle((0, 0, w * S - 1, h * S - 1), radius * S, fill=fill)
    return im.resize(size, Image.LANCZOS)


def card(size=53, radius=8):
    """the Pack card: a gold rim (dark outside, light along the top) round a parchment square"""
    n = size * S
    im = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, n - 1, n - 1), radius * S, fill=RIM_DARK + (255,))
    d.rounded_rectangle((S, S, n - 1 - S, n - 1 - S), radius * S - S, fill=RIM_MID + (255,))
    # the light upper edge of the rim
    hi = Image.new('RGBA', (n, n), (0, 0, 0, 0))
    ImageDraw.Draw(hi).rounded_rectangle((S, S, n - 1 - S, n // 2), radius * S - S, fill=RIM_LIGHT + (200,))
    mask = Image.new('L', (n, n), 0)
    ImageDraw.Draw(mask).rectangle((0, 0, n, 3 * S), fill=255)
    im.paste(hi, (0, 0), Image.composite(hi, Image.new('RGBA', (n, n)), mask).split()[3])
    # the parchment, a little grain
    inset = 3 * S
    rng = random.Random(9)
    paper = Image.new('RGB', (n - 2 * inset, n - 2 * inset), PARCHMENT)
    px = paper.load()
    for y in range(paper.height):
        for x in range(paper.width):
            v = rng.randint(-9, 9)
            px[x, y] = tuple(max(0, min(255, c + v)) for c in PARCHMENT)
    paper = paper.filter(ImageFilter.GaussianBlur(S * 0.6))
    pmask = Image.new('L', paper.size, 0)
    ImageDraw.Draw(pmask).rounded_rectangle((0, 0, paper.width - 1, paper.height - 1), radius * S - inset, fill=255)
    im.paste(paper, (inset, inset), pmask)
    return im.resize((size, size), Image.LANCZOS)


#: the game's own health bar (the Creature HUD, layout 5ED10964, read 1:1 from a 1024x768 screenshot 2026-11-13): a
#: pill of 58x11 with a 1-pixel dark green edge and round ends; the fill a vertical green gradient (light top row)
BAR = (47, 11)
BAR_EDGE = (0, 35, 0, 255)
BAR_EMPTY = (6, 26, 6, 255)
BAR_GREEN = [(0, 141, 0), (0, 252, 0), (0, 251, 0), (0, 247, 0), (0, 235, 0), (0, 201, 0), (0, 184, 0), (0, 120, 0),
             (0, 59, 0)]


def bar(kind):
    """the native health bar: 'back' the empty pill, 'fill' the full green pill, 'ring' its edge alone (drawn on top
    of the clipped fill, so the fill's cut end sits inside the edge as on the game's own bar)"""
    w, h = BAR
    n_w, n_h = w * S, h * S
    im = Image.new('RGBA', (n_w, n_h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    r = (h // 2) * S
    if kind == 'ring':
        d.rounded_rectangle((0, 0, n_w - 1, n_h - 1), r, outline=BAR_EDGE, width=S)
        return im.resize(BAR, Image.LANCZOS)
    d.rounded_rectangle((0, 0, n_w - 1, n_h - 1), r, fill=BAR_EDGE)
    inner = (S, S, n_w - 1 - S, n_h - 1 - S)
    if kind == 'back':
        d.rounded_rectangle(inner, r - S, fill=BAR_EMPTY)
        return im.resize(BAR, Image.LANCZOS)
    grad = Image.new('RGBA', (n_w, n_h), (0, 0, 0, 0))
    gd = ImageDraw.Draw(grad)
    rows = len(BAR_GREEN)
    for y in range(n_h):
        t = min(rows - 1, max(0, (y - S) * rows // max(1, n_h - 2 * S)))
        gd.line((0, y, n_w, y), fill=BAR_GREEN[t] + (255,))
    mask = Image.new('L', (n_w, n_h), 0)
    ImageDraw.Draw(mask).rounded_rectangle(inner, r - S, fill=255)
    im.paste(grad, (0, 0), mask)
    return im.resize(BAR, Image.LANCZOS)


def main():
    images = {
        'lineages_party_bar_back': bar('back'),
        'lineages_party_bar_fill': bar('fill'),
        'lineages_party_bar_ring': bar('ring'),
        'lineages_party_header': rounded((91, 35), 12, HEADER, WHITE_EDGE, 1),
        'lineages_party_plate': rounded((170, 35), 17, PLATE, WHITE_EDGE, 1),
        'lineages_party_plate_me': rounded((170, 35), 17, PLATE, GOLD_EDGE, 1),
        'lineages_party_card': card(),
    }
    for stem, im in images.items():
        out = OUT / f'{stem}.png'
        im.save(out)
        print(f'{out.relative_to(ROOT)} {im.size}')


if __name__ == '__main__':
    main()
