#!/usr/bin/env python3
"""Self-tests of the visual test tools (make test, no game; docs/06-testing.md#testing-key-frames):

  frames    key frame names and manifest, the selection cap, contact sheets of a fake run (tools/frames_review.py)

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


CHECKS = {'frames': test_frames}

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
