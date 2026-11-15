#!/usr/bin/env python3
"""Self-tests of the visual test tools (make test, no game; docs/06-testing.md#testing-key-frames):

  frames    key frame names and manifest, the selection cap, contact sheets of a fake run (tools/frames_review.py)
  golden    the window crop's compare: the same picture passes, a button 2 px aside or gone fails, a masked
            change passes, the stored reference keeps a small colour swatch (golden.py)
  sameview  coop.cam / coop.census / coop.creations parsing, the projection, the match of two pictures (sameview.py)

  visual_selftest.py [check ...]   all checks by default; exit 1 when one fails
The image checks need Pillow: without it they print a SKIP line (the CI installs Pillow)."""
import json
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import frames  # noqa: E402

fails = []
TMP = ''


def check(cond, what):
    print(('  ok   ' if cond else '  FAIL ') + what)
    if not cond:
        fails.append(what)


def skip_without_pil(what):
    if frames.have_pil():
        return False
    print(f'  SKIP {what}: Pillow is missing (python3 -m pip install --user pillow)')
    return True


def fake_frame(path, colour, size=(1440, 1080)):
    from PIL import Image
    Image.new('RGB', size, colour).save(path)


def test_frames():
    check(frames.parse_pos('species=0 key=0x1\npos=(12.50 -3.00 401.25) |r|=401.6') == [12.5, -3.0, 401.25],
          'parse_pos reads coop.avatar')
    check(frames.parse_pos('nothing controlled') is None, 'parse_pos: no avatar, no position')
    check(frames.parse_mode('0x01654C03 Creature') == 'Creature', 'parse_mode: the name after the id')
    check(frames.frame_file(3, 'nest eggs!', 'guest') == '03_nest-eggs_guest.jpg', 'frame file name is safe')
    many = [dict(test=f't{i % 7}', nn=i // 7 + 1, tag='x', kind='key') for i in range(140)]
    many += [dict(test='same_view', nn=i + 1, tag='s', kind='sameview') for i in range(8)]
    sel = frames.select(many, 80)
    check(len(sel) == 80, f'select caps at 80 ({len(sel)})')
    check(sum(1 for e in sel if e['kind'] == 'sameview') == 8, 'select keeps every sameview frame')
    tests = {e['test'] for e in sel}
    check(len(tests) == 8, f'select keeps every test ({len(tests)})')
    firsts = {(e['test'], e['nn']) for e in sel}
    check(all((f't{i}', 1) in firsts for i in range(7)), 'select keeps the first frame of each test')
    check(frames.select(many[:10], 80) == many[:10], 'select keeps a short list as it is')
    if skip_without_pil('frames: sheets'):
        return
    run = os.path.join(TMP, 'run1')
    os.environ['SPORE_LAB_RUN'] = run
    os.environ['SPORE_LAB_TEST'] = 'play_creature'
    try:
        d = frames.frames_dir(os.path.join(TMP, 'out'))
        check(d == os.path.join(run, 'frames', 'play_creature'), 'frames_dir is the run folder in the lab')
        for nn in (1, 2):
            games = {}
            for role, col in (('host', (200, 40, 40)), ('guest', (40, 200, 40))):
                png = os.path.join(TMP, f'{role}.png')
                fake_frame(png, col)
                f = frames.frame_file(nn, 'walk', role)
                size = frames.to_jpeg(png, os.path.join(d, f))
                check(size == (960, 720), f'to_jpeg makes 960 px frames ({size})')
                games[role] = dict(file=f, ok=True, mode='Creature', pos=[1.0, 2.0, 3.0])
            check(frames.next_number(d) == nn, f'next_number {nn}')
            frames.record(d, frames.now_entry('play_creature', nn, 'walk', 'both creatures on the grass', 'key',
                                              'Creature', games))
        games = dict(host=dict(file='', ok=False, mode='', pos=None), guest=dict(file='03_x_guest.jpg', ok=True))
        frames.record(d, frames.now_entry('play_creature', 3, 'x', 'a missing host frame', 'key', '', games))
        import frames_review
        made, out = frames_review.build(run, 80)
        check(len(made) == 3, f'three sheets ({len(made)})')
        from PIL import Image
        with Image.open(made[0][0]) as im:
            check(im.width == 2 * 720 + 6 and im.height > 540, f'sheet: two tiles side by side ({im.size})')
            px = im.convert('RGB').getpixel((360, im.height - 100))
            check(px[0] > 150 and px[1] < 100, f'sheet: the host tile is on the left ({px})')
            px = im.convert('RGB').getpixel((726 + 360, im.height - 100))
            check(px[1] > 150 and px[0] < 100, f'sheet: the guest tile is on the right ({px})')
        md = open(os.path.join(out, 'sheets.md')).read()
        check(md.count('\n* `') == 3 and 'both creatures on the grass' in md, 'sheets.md lists each sheet with its note')
        check('<img' in open(os.path.join(out, 'index.html')).read(), 'index.html shows the sheets')
        made2, _ = frames_review.build(run, 2)
        check(len(made2) == 2 and len([f for f in os.listdir(out) if f.endswith('.jpg')]) == 2,
              'a smaller --max rebuilds the folder with fewer sheets')
        lines = [json.loads(ln) for ln in open(os.path.join(d, 'manifest.jsonl'))]
        check(all({'test', 'tag', 'note', 'stage', 't', 'games'} <= set(e) for e in lines), 'manifest lines are complete')
    finally:
        os.environ.pop('SPORE_LAB_RUN', None)
        os.environ.pop('SPORE_LAB_TEST', None)


CAM = """viewer=03CD7E90 camera=03CD8310
viewOffset=(0.000 0.000) viewWindow=(0.546 0.410) recip=(1.830 2.441) proj=1 near=1.000 far=10000.0
viewport x=0 y=0 w=1440 h=1080
  view[0] =    1.000    0.000    0.000    0.000
  view[1] =    0.000    1.000    0.000    0.000
  view[2] =    0.000    0.000    1.000    0.000
  view[3] =   10.000  -20.000    5.000    1.000
"""
CENSUS = """census v1 role=host me=0 mode=Creature modeid=01654C01 world=68AC222A t=1.0
obj c6 key=065CB8BD alive=1 hp=6.0 ai=1 own=npc owner=-1 pos=(10.0 5.0 5.0)
obj c7 key=065CB8BD alive=0 hp=0.0 ai=1 own=npc owner=-1 pos=(12.0 5.0 6.0)
obj c-1 key=290217AC alive=1 hp=9.0 ai=1 own=avatar owner=-1 pos=(8.0 0.0 5.0)
obj c8 key=065CB8BD alive=1 hp=6.0 ai=1 own=npc owner=-1 pos=(10.0 -30.0 5.0)
obj c9 key=065CB8BD alive=1 hp=6.0 ai=1 own=npc owner=-1 pos=(10.0 200.0 5.0)
end objects=5
"""
CREATIONS = """  mine creature: 290217AC 'Ms Spoffit' (5 own)
  player 0 creature: 065CBE70/2B978C46/40626200 ''
  player 1 creature: 7CBD75FC/2B978C46/40626200 'Ms Spoffit'
"""


def test_sameview():
    import sameview as sv
    cam = sv.parse_cam(CAM)
    check(cam is not None and cam['viewport'] == (0, 0, 1440, 1080) and cam['rows'][3] == [10.0, -20.0, 5.0],
          'parse_cam reads the rows, the window and the viewport')
    check(sv.parse_cam('no viewer') is None, 'parse_cam: no viewer, None')
    x, y, z = sv.project(cam, (10.0, 5.0, 5.0))
    check(abs(x - 720) < 0.01 and abs(y - 540) < 0.01 and abs(z - 25) < 0.01, f'a point ahead is in the middle ({x}, {y})')
    x2, y2, _ = sv.project(cam, (15.0, 5.0, 8.0))
    check(x2 > 720 and y2 < 540, f'right and up go right and up on the screen ({x2:.0f}, {y2:.0f})')
    check(sv.project(cam, (10.0, -40.0, 5.0)) is None, 'a point behind the camera has no screen place')
    rows = sv.parse_census(CENSUS)
    check(len(rows) == 5 and rows[1]['alive'] == 0 and rows[2]['own'] == 'avatar', f'parse_census ({len(rows)} rows)')
    al = sv.aliases(CREATIONS)
    check(al == {'290217AC': '7CBD75FC'}, f'aliases: the own creation under the session key ({al})')
    seen = sv.in_view(cam, rows, alias=al)
    keys = sorted(r['key'] for r in seen)
    check(keys == ['065CB8BD', '065CB8BD:dead', '7CBD75FC'], f'in_view: the picture, a body apart, the alias ({keys})')
    moved = [dict(r, x=r['x'] + 30) for r in seen]
    r = sv.compare(seen, moved)
    check(r['ok'] and len(r['matched']) == 3, f'compare: the same creatures 30 px apart match ({r["line"]})')
    far = [dict(r, x=r['x'] + 400) for r in seen]
    check(not sv.compare(seen, far)['ok'], 'compare: another view (400 px apart) does not match')
    other = [dict(r, key='0BADBEEF') for r in seen]
    check(not sv.compare(seen, other)['ok'], 'compare: other species at the same places do not match')
    check(not sv.compare([], [])['ok'], 'compare: two empty pictures are no match')
    eye, tg = sv.camera_at([0.0, 0.0, 500.0], 0, 4, 25.0, 8.0)
    d = sum((eye[i] - [0, 0, 500][i]) ** 2 for i in range(3)) ** 0.5
    check(abs(d - (25 ** 2 + 8 ** 2) ** 0.5) < 0.01 and abs(tg[2] - 501) < 0.01, f'camera_at: 25 m along, 8 m up ({d:.2f})')
    check(sv.view_cmd([1, 2, 3], [4, 5, 6], 50).startswith('coop.view set 1.000 2.000 3.000 4.000'), 'view_cmd')


def window_image(bx=60, by=80, show=True, text_at=None):
    """a fake co-op window: a dark panel, gold buttons with dark 'text' strokes, a pink swatch"""
    from PIL import Image, ImageDraw
    im = Image.new('RGB', (380, 330), (28, 52, 66))
    d = ImageDraw.Draw(im)
    if show:
        d.rounded_rectangle((bx, by, bx + 150, by + 24), 8, fill=(190, 160, 50))
        for k in range(6):
            d.line((bx + 30 + 15 * k, by + 7, bx + 38 + 15 * k, by + 17), fill=(30, 30, 30), width=2)
    d.rounded_rectangle((220, 80, 370, 104), 8, fill=(190, 160, 50))
    d.rectangle((150, 200, 158, 206), fill=(255, 95, 200))
    if text_at:
        d.text(text_at, '192.168.1.5:47810', fill=(230, 230, 230))
    return im


def test_golden():
    if skip_without_pil('golden'):
        return
    import golden
    ref = window_image()
    r = golden.compare(window_image(), ref)
    check(r['ok'] and r['n'] == 0, f'the same picture passes ({r["n"]} pixels)')
    r = golden.compare(window_image(bx=62), ref)
    check(not r['ok'] and r['bbox'] and 55 <= r['bbox'][0] <= 66, f'a button 2 px aside fails ({r["why"]}, {r["bbox"]})')
    r = golden.compare(window_image(show=False), ref)
    check(not r['ok'], f'a button gone fails ({r["why"]})')
    noisy = window_image(text_at=(20, 260))
    r = golden.compare(noisy, ref)
    check(not r['ok'], f'an unmasked new text fails ({r["frac"]:.3%})')
    r = golden.compare(noisy, ref, [(15, 255, 160, 275)])
    check(r['ok'], f'the same text under a mask passes ({r["frac"]:.3%})')
    r = golden.compare(window_image().resize((300, 330)), ref)
    check(not r['ok'] and not r['size_ok'], 'another size fails')
    p = os.path.join(TMP, 'ref.png')
    golden.save_ref(ref, p)
    back = golden.load_ref(p)
    check(back.getpixel((154, 203)) == (255, 95, 200), f'the stored reference keeps the pink swatch ({back.getpixel((154, 203))})')
    check(golden.compare(window_image(), back)['ok'], 'a stored reference compares as the picture')
    out = golden.diff_image(window_image(bx=62), ref, [(15, 255, 160, 275)], os.path.join(TMP, 'diff.png'))
    from PIL import Image
    with Image.open(out) as im:
        check(im.width == 3 * 380 + 8, f'diff image: the picture, the reference and the differences ({im.size})')
    check(golden.to_local((700, 400, 800, 420), (400, 200, 1100, 860)) == (148, 98, 202, 112), 'to_local scales and pads')


CHECKS = {'frames': test_frames, 'golden': test_golden, 'sameview': test_sameview}

if __name__ == '__main__':
    want = sys.argv[1:] or list(CHECKS)
    for c in want:
        TMP = tempfile.mkdtemp(prefix=f'sk-visual-{c}-')
        try:
            CHECKS[c]()
        finally:
            shutil.rmtree(TMP, ignore_errors=True)
    print(f'visual_selftest: {len(want)} check(s), {len(fails)} failure(s)')
    sys.exit(1 if fails else 0)
