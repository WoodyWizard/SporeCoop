#!/usr/bin/env python3
"""Draw the Social Menu's icon into data/ui/ (decision 6L A, owner 2026-11-18: "We'll name it the Social Menu and it'll
have the two person silhouette icon").

Two people, the one in front white, the one behind a step to the right and a little smaller in a light blue-grey,
each a round head over rounded shoulders, drawn at 4x and scaled down (smooth edges), with a soft dark outline so it
reads on any stage's tab art:
  lineages_social_icon.png       the HUD button next to the Party tab (48 x 48)
  lineages_social_icon_menu.png  the galaxy menu's Social item (62 x 62, as coop_icon.png)

usage: py tools/social_ui_assets.py
"""
from pathlib import Path

from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parent.parent
OUT = ROOT / 'data' / 'ui'
S = 4

FRONT = (255, 255, 255, 255)
BACK = (196, 214, 228, 255)
OUTLINE = (12, 24, 34, 200)


def person_mask(big, cx, top, scale):
    """one person (a round head, a gap, domed shoulders) as an alpha mask"""
    m = Image.new('L', (big, big), 0)
    d = ImageDraw.Draw(m)
    head_r = 8.6 * scale
    d.ellipse((cx - head_r, top, cx + head_r, top + 2 * head_r), fill=255)
    y = top + 2 * head_r + 3.2 * scale
    w, h = 16.5 * scale, 17 * scale
    d.chord((cx - w, y, cx + w, y + 2 * h), 180, 360, fill=255)  # the upper half: shoulders
    d.rectangle((cx - w, y + h - 0.5, cx + w, y + h + 1.2 * scale), fill=255)
    return m


def icon(size):
    big = size * S
    k = big / 64.0
    back = person_mask(big, 41 * k, 8 * k, 0.86 * k)
    front = person_mask(big, 25 * k, 14 * k, 1.0 * k)
    # the front one cuts a thin gap out of the one behind
    gap = front.filter(ImageFilter.MaxFilter(int(3.2 * k) | 1))
    back = Image.composite(Image.new('L', (big, big), 0), back, gap)
    shapes = Image.new('RGBA', (big, big), (0, 0, 0, 0))
    shapes.paste(Image.new('RGBA', (big, big), BACK), (0, 0), back)
    shapes.paste(Image.new('RGBA', (big, big), FRONT), (0, 0), front)
    # the outline: the shapes' alpha grown and blurred, in dark, under them
    alpha = shapes.split()[3]
    grown = alpha.filter(ImageFilter.MaxFilter(int(4 * k) | 1)).filter(ImageFilter.GaussianBlur(1.0 * k))
    out = Image.new('RGBA', (big, big), OUTLINE)
    out.putalpha(grown.point(lambda a: a * OUTLINE[3] // 255))
    out.alpha_composite(shapes)
    return out.resize((size, size), Image.LANCZOS)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    icon(48).save(OUT / 'lineages_social_icon.png')
    icon(62).save(OUT / 'lineages_social_icon_menu.png')
    print('wrote', OUT / 'lineages_social_icon.png', OUT / 'lineages_social_icon_menu.png')


if __name__ == '__main__':
    main()
