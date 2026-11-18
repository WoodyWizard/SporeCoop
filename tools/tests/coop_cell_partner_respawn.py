#!/usr/bin/env python3
"""BUG-1243 (players' video, second look): the GUEST fights far away; the HOST's own cell, alone in empty water, is
swept into a whirl ("and I fought the air"). Cell fixture.

The Cell stage's respawn hatch (cell_egg_L0_autohatch, 0xB4B06F12) is a picture for the player whose cell hatches: it
blacks out the screen and draws the egg and its whirl over the view. A knocked-out guest that nobody revives respawns
(cell.respawn_small), and that effect came to the host twice: the guest's world effect relayed and the host's own play
of it on the guest's puppet (docs/re/cell.md#cell-partner-hatch).

1. The guest goes 70 m from the host (`coop.pool tp`), the host does nothing.
2. The guest's cell takes a lethal bite (`coop.pool selfhit 20`, as a creature's): knocked out, nobody near to revive
   it, it respawns after the knockout time.
3. The host's game is watched meanwhile: how many own-cell hatch effects it made (`coop.effects`: the game's effect
   objects of that id, `made=`) and its screen (`shot` every ~0.3 s from the respawn on: the mean brightness of the
   picture and key frames).
PASS: the host's game made no hatch effect and its screen never went black (darker than 35 % of its own brightness
before), while the guest's own game did hatch (control).

  coop_cell_partner_respawn.py   (SPORE_FIX_OFF=effects.cell_own_only must FAIL)
"""
import math
import os
import re
import sys
import time

from twoinst import Checks, keyframe, lane, start

ANIM_POS = re.compile(r'^anim avatar \S+ anim=\S+ head=\S+ angv=\S+ pos=\(([-\d.]+) ([-\d.]+)\)', re.M)


def own_pos(con):
    m = ANIM_POS.search(con.call('coop.pool anims')['out'])
    return (float(m.group(1)), float(m.group(2))) if m else None


def apart(host, guest):
    h, g = own_pos(host), own_pos(guest)
    return math.hypot(h[0] - g[0], h[1] - g[1]) if h and g else -1


def made(con):
    m = re.search(r'effects cell own only: fix=\d made=(\d+)', con.call('coop.effects')['out'])
    return int(m.group(1)) if m else -1


def ko_state(con):
    """the own cell's knockout as the game holds it: downed (hp pinned 1), else its hp"""
    return con.call('coop.pool selfstate')['out'].strip()[:120]


def brightness(con, path, keep=False):
    """the mean brightness (0..255) of the game's picture now, None when no shot (keep: the shot stays, as a small JPEG)"""
    try:
        from PIL import Image, ImageStat
    except ImportError:
        return None
    if not con.call('shot ' + path).get('ok'):
        return None
    try:
        with Image.open(path) as im:
            small = im.convert('RGB').resize((480, 360))
            v = ImageStat.Stat(small.convert('L')).mean[0]
            if keep:
                small.save(path[:-4] + '.jpg', quality=80)
        os.remove(path)
        return v
    except OSError:
        return None


def main():
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)
    d, t0 = -1, time.time()
    while time.time() - t0 < 50:  # (the join after a load puts a new own cell back next to the host for 30 s, BUG-0352)
        guest.call('coop.pool tp 70 0')
        time.sleep(4)
        d = apart(host, guest)
        if d > 50:
            time.sleep(4)
            d = apart(host, guest)
            if d > 50:
                break
    ck(d > 50, f'the guest is far from the host ({d:.1f} m)')
    shots = lane.out_dir('shots', 'partner_respawn')
    os.makedirs(shots, exist_ok=True)
    if '--probe' in sys.argv:  # RE: which effect ids black out the host's screen, played where the guest is
        g = own_pos(guest)
        for fid in sys.argv[sys.argv.index('--probe') + 1].split(','):
            host.call(f'coop.fix effects.cell_own_only off')
            host.call(f'coop.effects inject 1 1 0 5 {fid} 0 {g[0]:.2f} {g[1]:.2f} 0')
            bs = []
            for i in range(6):
                b = brightness(host, os.path.join(shots, f'probe_{fid}_{i}.png'), keep=True)
                bs.append(round(b, 1) if b is not None else None)
                time.sleep(0.15)
            print(f'  probe {fid}: host brightness {bs}', flush=True)
            time.sleep(4)
        ck.faults(host=host, guest=guest)
        return ck.result()
    base = [b for b in (brightness(host, os.path.join(shots, f'base{i}.png')) for i in range(3)) if b is not None]
    b0 = sum(base) / len(base) if base else None
    h0, g0 = made(host), made(guest)
    print(f'  before: host brightness {b0}, hatch effects made: host {h0} guest {g0}', flush=True)
    print('  guest: ' + guest.call('coop.pool selfhit 20')['out'].strip(), flush=True)
    time.sleep(1)
    print(f'  guest knocked out: {ko_state(guest)}', flush=True)
    lows, t1, shot_n, hatched_at = [], time.time(), 0, None
    while time.time() - t1 < 20:
        if hatched_at is None and made(guest) > g0:
            hatched_at = time.time()
        # (no key frame in this loop: a second shot of the same game at the same moment can come back black)
        b = brightness(host, os.path.join(shots, f's{shot_n}.png'), keep=True)
        shot_n += 1
        if b is not None:
            lows.append((round(time.time() - t1, 1), round(b, 1), shot_n - 1))
        if hatched_at and time.time() - hatched_at > 4:
            break
        time.sleep(0.2)
    h1, g1 = made(host), made(guest)
    print(f'  after: hatch effects made: host {h0} -> {h1}, guest {g0} -> {g1}; guest {ko_state(guest)}; '
          f'{apart(host, guest):.1f} m apart', flush=True)
    keyframe('after_respawn', "Cell: the guest respawned next to the host; the host's screen shows its own cell in the water: "
             "no black screen, no egg", settle=0)
    darkest = min(lows, key=lambda x: x[1]) if lows else None
    k = lows.index(darkest) if darkest else 0
    print(f'  host brightness over {len(lows)} shots: darkest {darkest} (shot s{darkest[2] if darkest else -1}.jpg in {shots}), around it {lows[max(0, k - 3):k + 4]}',
          flush=True)
    print('  host: ' + ' | '.join(l for l in host.call('coop.effects')['out'].splitlines() if 'own only' in l), flush=True)
    rec = [l for l in host.call('coop.effects recent')['out'].splitlines() if l.startswith('recent')]
    near = [l for l in rec if float(re.search(r'age=([\d.]+)', l).group(1)) < time.time() - t1 + 1]
    print('  host world effects since the knockout (m: made by the host\'s game, p: a partner\'s played):', flush=True)
    for l in near[-40:]:
        print('    ' + l, flush=True)
    ck(hatched_at is not None, "the guest's own game hatched its respawn (control)")
    ck(h1 == h0, f"the host's game made no hatch effect of its own cell's ({h0} -> {h1})")
    ck(b0 is not None and darkest is not None and darkest[1] >= 0.35 * b0,
       f"the host's screen never went black (darkest {darkest}, before {b0})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
