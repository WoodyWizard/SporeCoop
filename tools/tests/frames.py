"""Key frames: what both games showed at a key moment of a test (docs/06-testing.md#testing-key-frames).

twoinst.keyframe(tag, note) shoots every game of the test at once and stores small JPEGs plus one manifest line:

  <run>/frames/<test>/<nn>_<tag>_<role>.jpg      (~960 px wide)
  <run>/frames/<test>/manifest.jsonl             one JSON object per key frame:
      {"test", "nn", "tag", "note", "kind", "t", "stage", "games": {role: {"file", "mode", "pos", "ok"}}}

<run> is the lab run's folder (tools/lab.py sets SPORE_LAB_RUN and SPORE_LAB_TEST for each test); a test started by
hand writes into out/[laneN/]frames/manual/. tools/frames_review.py turns a run's key frames into contact sheets.

This module has no game code: the image helpers need Pillow and say so when it is missing (have_pil()).
"""
import json
import os
import re
import sys
import time

WIDTH = 960  # the stored frame's width (the lab's games draw 1440x1080)
QUALITY = 78
KINDS = ('key', 'phase', 'sameview')


def have_pil():
    try:
        import PIL.Image  # noqa: F401
        return True
    except ImportError:
        return False


def run_dir(root=None):
    """the lab run's folder of this test, or None outside the lab"""
    d = os.environ.get('SPORE_LAB_RUN', '').strip()
    return d or None


def test_name():
    """SPORE_LAB_TEST (the lab's test name, with a variant's suffix), else the script's name"""
    n = os.environ.get('SPORE_LAB_TEST', '').strip()
    if n:
        return n
    base = os.path.splitext(os.path.basename(sys.argv[0] or 'test'))[0]
    return re.sub(r'^coop_', '', base) or 'test'


def safe(s, n=40):
    """a file name part: letters, digits, '-' and '_' only"""
    s = re.sub(r'[^A-Za-z0-9_-]+', '-', str(s)).strip('-')
    return (s or 'x')[:n]


def frames_dir(base_out, test=None):
    """<run>/frames/<test>, or <base_out>/frames/manual/<test> outside the lab (created)"""
    test = safe(test or test_name(), 60)
    run = run_dir()
    d = os.path.join(run, 'frames', test) if run else os.path.join(base_out, 'frames', 'manual', test)
    os.makedirs(d, exist_ok=True)
    return d


def next_number(d):
    """the next key frame number of this folder (1, 2, ...): one more than the manifest's last"""
    nn = 0
    for e in read_manifest(os.path.join(d, 'manifest.jsonl')):
        nn = max(nn, int(e.get('nn') or 0))
    return nn + 1


def frame_file(nn, tag, role):
    return f'{nn:02d}_{safe(tag)}_{safe(role, 12)}.jpg'


def parse_pos(avatar_out):
    """the position in a coop.avatar answer ('pos=(x y z)'), None without one"""
    m = re.search(r'pos=\(\s*(-?[\d.]+)\s+(-?[\d.]+)\s+(-?[\d.]+)\)', avatar_out or '')
    return [round(float(v), 2) for v in m.groups()] if m else None


def parse_mode(mode_out):
    """the mode name of a game.mode answer ('0x01654C03 Creature' -> 'Creature')"""
    words = [w for w in (mode_out or '').split() if not w.startswith('0x')]
    return words[0] if words else ''


def to_jpeg(src, dst, width=WIDTH, quality=QUALITY):
    """a screenshot (PNG) as a smaller JPEG; (w, h) of the JPEG, None when Pillow is missing or the file is bad"""
    try:
        from PIL import Image
    except ImportError:
        return None
    try:
        with Image.open(src) as im:
            im = im.convert('RGB')
            if im.width > width:
                im = im.resize((width, max(1, round(im.height * width / im.width))), Image.BILINEAR)
            im.save(dst, 'JPEG', quality=quality, optimize=True)
            return im.size
    except (OSError, ValueError):
        return None


def record(d, entry):
    """one manifest line (appended; each test has its own folder, so parallel lanes never share a file)"""
    with open(os.path.join(d, 'manifest.jsonl'), 'a') as f:
        f.write(json.dumps(entry, sort_keys=True) + '\n')


def read_manifest(path):
    out = []
    try:
        with open(path) as f:
            for ln in f:
                ln = ln.strip()
                if ln:
                    try:
                        out.append(json.loads(ln))
                    except ValueError:
                        pass
    except OSError:
        pass
    return out


def load_run(run):
    """every key frame of a run folder (out/lab/<run>), in test and number order, with 'dir' set"""
    base = os.path.join(run, 'frames')
    out = []
    try:
        tests = sorted(os.listdir(base))
    except OSError:
        return out
    for t in tests:
        d = os.path.join(base, t)
        if not os.path.isdir(d) or t in ('sheets', 'manual'):
            continue
        for e in read_manifest(os.path.join(d, 'manifest.jsonl')):
            e['dir'] = d
            out.append(e)
    out.sort(key=lambda e: (e.get('test', ''), int(e.get('nn') or 0)))
    return out


def select(entries, limit=80):
    """at most `limit` key frames: every sameview frame first (they are few and the point of the experiment),
    then the others spread evenly over the tests (each test's first and last frame before its middle ones)"""
    if len(entries) <= limit:
        return list(entries)
    same = [e for e in entries if e.get('kind') == 'sameview']
    rest = [e for e in entries if e.get('kind') != 'sameview']
    if len(same) >= limit:
        return same[:limit]
    by_test = {}
    for e in rest:
        by_test.setdefault(e.get('test', ''), []).append(e)

    def order(lst):  # first, last, then the middle ones from the centre out
        if len(lst) <= 2:
            return list(lst)
        mid = lst[1:-1]
        c = len(mid) // 2
        rank = sorted(range(len(mid)), key=lambda i: (abs(i - c), i))
        return [lst[0], lst[-1]] + [mid[i] for i in rank]

    queues = {t: order(v) for t, v in by_test.items()}
    picked, room = [], limit - len(same)
    while room > 0 and any(queues.values()):
        for t in sorted(queues):
            if queues[t] and room > 0:
                picked.append(queues[t].pop(0))
                room -= 1
    keep = {id(e) for e in same + picked}
    return [e for e in entries if id(e) in keep]


def caption_lines(e):
    """the text of a sheet's label: test, tag, note, then one line per game"""
    head = f'{e.get("test", "?")}  #{int(e.get("nn") or 0):02d}  {e.get("tag", "")}  [{e.get("kind", "key")}]'
    if e.get('stage'):
        head += f'  stage {e["stage"]}'
    lines = [head, 'should show: ' + (e.get('note') or '(no note)')]
    for role, g in sorted((e.get('games') or {}).items(), key=lambda kv: role_order(kv[0])):
        pos = g.get('pos')
        p = '(' + ' '.join(f'{v:.1f}' for v in pos) + ')' if pos else '-'
        bad = '' if g.get('ok', True) else '  NO SHOT'
        lines.append(f'{role}: {g.get("mode") or "?"} at {p}{bad}')
    for ln in e.get('extra') or []:
        lines.append(str(ln))
    return lines


def role_order(role):
    """the host first, then the guests ('host', 'guest', or 'host-Alice', 'guest-Bob' in a party)"""
    r = str(role)
    return (0 if r.startswith('host') else 1 if r.startswith('guest') else 2), r


def wrap(text, n):
    """plain word wrap to n characters"""
    out, cur = [], ''
    for w in text.split():
        if cur and len(cur) + 1 + len(w) > n:
            out.append(cur)
            cur = w
        else:
            cur = (cur + ' ' + w) if cur else w
    if cur:
        out.append(cur)
    return out or ['']


def _font(size):
    from PIL import ImageFont
    for name in ('DejaVuSans.ttf', '/usr/share/fonts/dejavu-sans-fonts/DejaVuSans.ttf',
                 '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    try:
        return ImageFont.load_default(size)  # Pillow >= 10.1
    except TypeError:
        return ImageFont.load_default()


def make_sheet(e, out_path, tile_w=None):
    """one contact sheet of a key frame: each game's frame side by side (host first), the label above;
    a game without a frame is a grey tile saying so. Returns the sheet's size, None without Pillow."""
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return None
    games = sorted((e.get('games') or {}).items(), key=lambda kv: role_order(kv[0]))
    tile_w = tile_w or (720 if len(games) <= 2 else 480)  # four players: a sheet stays under 2000 px wide
    tiles = []
    for role, g in games:
        im = None
        p = os.path.join(e.get('dir', ''), g.get('file') or '')
        if g.get('file') and os.path.exists(p):
            try:
                with Image.open(p) as src:
                    im = src.convert('RGB')
            except OSError:
                im = None
        if im is None:
            im = Image.new('RGB', (tile_w, tile_w * 3 // 4), (90, 90, 90))
            ImageDraw.Draw(im).text((20, 20), f'{role}: no frame', fill=(255, 255, 255), font=_font(22))
        elif im.width != tile_w:
            im = im.resize((tile_w, max(1, round(im.height * tile_w / im.width))), Image.BILINEAR)
        tiles.append((role, im))
    if not tiles:
        tiles = [('none', Image.new('RGB', (tile_w, tile_w * 3 // 4), (90, 90, 90)))]
    font, small = _font(20), _font(17)
    chars = max(40, tile_w * len(tiles) // 10)
    lines = []
    for i, ln in enumerate(caption_lines(e)):
        lines += [(w, i == 0) for w in wrap(ln, chars)]
    head_h = 12 + 24 * len(lines)
    tile_h = max(t.height for _, t in tiles)
    gap = 6
    W = tile_w * len(tiles) + gap * (len(tiles) - 1)
    sheet = Image.new('RGB', (W, head_h + 26 + tile_h), (24, 24, 28))
    d = ImageDraw.Draw(sheet)
    y = 6
    for text, bold in lines:
        d.text((10, y), text, fill=(255, 230, 120) if bold else (235, 235, 235), font=font if bold else small)
        y += 24
    for i, (role, im) in enumerate(tiles):
        x = i * (tile_w + gap)
        d.text((x + 8, head_h + 2), role, fill=(150, 210, 255), font=font)
        sheet.paste(im, (x, head_h + 26))
    sheet.save(out_path, 'JPEG', quality=82, optimize=True)
    return sheet.size


def sheet_name(i, e):
    return f'{i:03d}_{safe(e.get("test", "t"), 40)}_{int(e.get("nn") or 0):02d}_{safe(e.get("tag", ""), 30)}.jpg'


def now_entry(test, nn, tag, note, kind, stage, games, extra=None):
    e = dict(test=test, nn=nn, tag=str(tag), note=str(note), kind=kind if kind in KINDS else 'key',
             t=round(time.time(), 2), stage=stage or '', games=games)
    if extra:
        e['extra'] = [str(x) for x in extra]
    return e
