"""Golden images of the co-op window (docs/06-testing.md#testing-ui-golden): a crop of the window compared with a
stored reference (tools/tests/golden/<name>.png) with a tolerance. No game code here (visual_selftest.py tests it).

  img = crop(screenshot, window_rect)             the window alone, at half size (SCALE)
  masks = [rect relative to the window, ...]      regions that vary between runs (addresses, pings, the version)
  r = compare(img, ref, masks)                    r['ok'], the fraction of differing pixels, their bounding box
  diff_image(img, ref, masks, path)               the differences in red over a dim copy, masks in blue

A pixel differs when one of its channels differs by more than PIX (after both are blurred by one pixel: the
font's anti-aliasing); the image fails when more than FRAC of the unmasked pixels differ, or when one 12x12 block
holds more than BLOCK of them (a small control that moved or vanished is a dense spot, not a spread of noise).
"""
import os

SCALE = 0.5   # references are stored at half the game's size (small files in git)
PIX = 48      # per channel, 0-255
FRAC = 0.001  # differing pixels allowed in the whole crop (noise of the lab's runs: up to 0.05 %; a button 4 px aside: 0.18 %)
BLOCK = 0.35  # differing pixels allowed in any 12x12 block
CELL = 12
HERE = os.path.dirname(os.path.abspath(__file__))
REF_DIR = os.path.join(HERE, 'golden')


def have_pil():
    try:
        import PIL.Image  # noqa: F401
        return True
    except ImportError:
        return False


def ref_path(name):
    return os.path.join(REF_DIR, name + '.png')


def crop(path_or_img, rect, scale=SCALE):
    """the window's rect (screen pixels x1 y1 x2 y2) out of a screenshot, scaled; an RGB image"""
    from PIL import Image
    im = Image.open(path_or_img) if isinstance(path_or_img, str) else path_or_img
    im = im.convert('RGB').crop(tuple(int(round(v)) for v in rect))
    if scale != 1:
        im = im.resize((max(1, round(im.width * scale)), max(1, round(im.height * scale))), Image.BILINEAR)
    return im


def to_local(rect, window, scale=SCALE, pad=2):
    """a screen rect as a rect inside the cropped window (scaled), padded by `pad` pixels"""
    return (round((rect[0] - window[0]) * scale) - pad, round((rect[1] - window[1]) * scale) - pad,
            round((rect[2] - window[0]) * scale) + pad, round((rect[3] - window[1]) * scale) + pad)


BLANK = (30, 44, 58)  # the colour of the blanked regions of a reference (close to the window's background)


def save_ref(img, path, blank=()):
    """the reference as an RGB PNG (~15-45 KB at half size). Not a palette PNG: 256 colours lost the small colour
    swatches (the pink of a player turned grey) and moved other pixels by up to 99 levels. `blank`: rects (local)
    painted flat first: the masked texts (the PC's addresses, ports) never reach the stored file in git; compare()
    skips them anyway"""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    img = img.convert('RGB')
    if blank:
        from PIL import ImageDraw
        d = ImageDraw.Draw(img)
        for x1, y1, x2, y2 in blank:
            if x2 > x1 and y2 > y1:
                d.rectangle((x1, y1, x2 - 1, y2 - 1), fill=BLANK)
    img.save(path, optimize=True)


def load_ref(path):
    from PIL import Image
    with Image.open(path) as im:
        return im.convert('RGB')


def _mask_grid(w, h, masks):
    m = bytearray(w * h)
    for x1, y1, x2, y2 in masks:
        for y in range(max(0, y1), min(h, y2)):
            a, b = max(0, x1), min(w, x2)
            if b > a:
                m[y * w + a:y * w + b] = b'\x01' * (b - a)
    return m


def compare(img, ref, masks=(), pix=PIX, frac=FRAC, block=BLOCK):
    """img against ref (the same size, else a failure): {'ok', 'size_ok', 'n', 'total', 'frac', 'bbox',
    'worst_block', 'why'}"""
    from PIL import ImageChops, ImageFilter
    if img.size != ref.size:
        return {'ok': False, 'size_ok': False, 'n': 0, 'total': 0, 'frac': 1.0, 'bbox': None, 'worst_block': 1.0,
                'why': f'size {img.size} against the reference {ref.size}'}
    a = img.convert('RGB').filter(ImageFilter.BoxBlur(1))
    b = ref.convert('RGB').filter(ImageFilter.BoxBlur(1))
    d = ImageChops.difference(a, b)
    r, g, bl = d.split()
    mx = ImageChops.lighter(ImageChops.lighter(r, g), bl)
    bad = mx.point(lambda v: 255 if v > pix else 0)
    w, h = img.size
    mask = _mask_grid(w, h, masks)
    px = bad.tobytes()
    n, total = 0, 0
    x1 = y1 = 10 ** 9
    x2 = y2 = -1
    cells = {}
    for y in range(h):
        row = y * w
        for x in range(w):
            if mask[row + x]:
                continue
            total += 1
            if px[row + x]:
                n += 1
                x1, y1, x2, y2 = min(x1, x), min(y1, y), max(x2, x), max(y2, y)
                k = (x // CELL, y // CELL)
                cells[k] = cells.get(k, 0) + 1
    worst = max(cells.values()) / (CELL * CELL) if cells else 0.0
    f = n / total if total else 0.0
    ok = f <= frac and worst <= block
    why = '' if ok else (f'{f:.2%} of the pixels differ (allowed {frac:.2%})' if f > frac else
                         f'a dense spot: {worst:.0%} of a {CELL}x{CELL} block differs (allowed {block:.0%})')
    return {'ok': ok, 'size_ok': True, 'n': n, 'total': total, 'frac': round(f, 5),
            'bbox': (x1, y1, x2 + 1, y2 + 1) if n else None, 'worst_block': round(worst, 3), 'why': why}


def diff_image(img, ref, masks, path, pix=PIX):
    """the differing pixels in red over a dim grey copy of the image, the masks in blue, the reference beside it"""
    from PIL import Image, ImageChops, ImageDraw, ImageFilter
    if img.size != ref.size:
        ref = ref.resize(img.size)
    a = img.convert('RGB').filter(ImageFilter.BoxBlur(1))
    b = ref.convert('RGB').filter(ImageFilter.BoxBlur(1))
    d = ImageChops.difference(a, b)
    r, g, bl = d.split()
    mx = ImageChops.lighter(ImageChops.lighter(r, g), bl).point(lambda v: 255 if v > pix else 0)
    base = img.convert('L').point(lambda v: v // 3).convert('RGB')
    red = Image.new('RGB', img.size, (255, 40, 40))
    out = Image.composite(red, base, mx)
    dr = ImageDraw.Draw(out)
    for m in masks:
        dr.rectangle(m, outline=(60, 120, 255))
    sheet = Image.new('RGB', (img.width * 3 + 8, img.height), (30, 30, 30))
    sheet.paste(img.convert('RGB'), (0, 0))
    sheet.paste(ref.convert('RGB'), (img.width + 4, 0))
    sheet.paste(out, (2 * img.width + 8, 0))
    sheet.save(path)
    return path
