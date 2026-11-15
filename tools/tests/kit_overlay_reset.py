#!/usr/bin/env python3
"""BUG-1026: one failed CreateStateBlock turned the overlay shapes (the partner arrows of BUG-0764) off until the game
restarted, also after a device Reset.

Creature fixture (two games), the host:
1. `kit.test.tri 900`: SporeKit draws a magenta triangle at the top left of its overlay; the screenshot shows it.
2. `kit.test.tri fail` (the next CreateStateBlock fails, as when the device is lost at that moment), the triangle
   again: no shapes now (failed=1), the screenshot shows no magenta.
3. Alt+Enter twice (fullscreen and back: two device Resets), the triangle again: PASS when the shapes are back
   (failed=0) and the screenshot shows the magenta triangle. SPORE_FIX_OFF=kit.trireset: the shapes stay off: FAIL.

  SPORE_LANE=6 python3 tools/tests/kit_overlay_reset.py
"""
import time

from PIL import Image

from twoinst import Checks, shot, start


def tri(con, arg=''):
    return con.call(('kit.test.tri ' + arg).strip())['out'].strip()


def magenta(path):
    """share of magenta pixels in a small square inside the test triangle (back buffer 40..100 px, the shot's scale)"""
    im = Image.open(path).convert('RGB')
    n = hit = 0
    for x in range(40, 100, 6):
        for y in range(40, 100, 6):
            px, py = int(x * 0.5), int(y * 0.5)
            if px < im.width and py < im.height:
                r, g, b = im.getpixel((px, py))
                n += 1
                hit += r > 180 and b > 180 and g < 90
    return hit / n if n else 0.0


def keys_alt_enter(con):
    con.call('key alt down')
    time.sleep(0.2)
    con.call('key enter')
    time.sleep(0.5)
    con.call('key alt up')


def look(ck, con, name, want):
    tri(con, '900')
    con.call('wait.frames 10 5000')
    p = shot(con, f'overlay_reset_{name}.png')
    m = magenta(p)
    st = tri(con)
    print(f'  {name}: {st}; magenta {m:.0%} ({p})', flush=True)
    ck((m > 0.6) == want, f'{name}: the test triangle is {"on" if want else "not on"} the screen ({m:.0%} magenta)')
    return st


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(2)
    st = look(ck, host, '1_before', True)
    ck('failed=0' in st, 'the shapes work before')
    print('  ' + tri(host, 'fail'), flush=True)
    st = look(ck, host, '2_failed', False)
    ck('failed=1' in st, 'the made-to-fail CreateStateBlock turned the shapes off')
    resets = 0
    for i in range(2):
        keys_alt_enter(host)
        time.sleep(4)
        host.call('wait.frames 30 20000')
        resets += 1
    log = host.call('frame')['out']
    print(f'  after {resets} Alt+Enter: {log.strip()}', flush=True)
    st = look(ck, host, '3_after_reset', True)
    ck('failed=0' in st, 'after the device Reset the shapes are tried again')
    tri(host, '0')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
