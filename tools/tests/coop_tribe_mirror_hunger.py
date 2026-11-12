#!/usr/bin/env python3
"""Tribe: the guest's roster shows its members as hungry as the host's (BUG-0935).

The 0.7.12 final check's key frames: after minutes of play the guest's roster had every member card red with an empty
bar while the host's cards were well. A member's hunger is the game's cCreatureBase +0xBBC (0..100, down 0.28 a second,
starving below 1: its update 0xC0D5A0 takes health off it every frame); a member eats in its behaviour tree. A guest's
mirror runs no tree: it never ate, its hunger ran to 0 in about 6 minutes (probe, lane 4: host 99.8, guest 30.3 after
4 min) and its game showed it starving.

1. Two games on the tribe fixture. The guest sets every mirror of the player tribe to the hunger that 6 minutes of its
   own update leave (`coop.tribe hunger <id> 0`: the game's own setter on the guest's game). Within 5 s every mirror
   must have the host's member's hunger again (within 3), and its health the host's.
2. The host's member eats or starves: the host sets one member's hunger to 40 (the game's setter); the guest's mirror
   reads 40 within 5 s.
Key frame `roster`: the Tribe panel's member cards on both games (with the fix: the same cards; without: red cards on
the guest).

SPORE_FIX_OFF=tribe.mirror_hunger: the mirrors keep their own hunger (FAIL).

  SPORE_LANE=4 python3 tools/tests/coop_tribe_mirror_hunger.py
"""
import re
import time

from twoinst import Checks, game_runs, keyframe, start

ROW = re.compile(r'\s+#(\d+) \S+ owner=(-?\d+) age=\d+ sel=\d+ hp=([\d.]+) .* hunger=([-\d.]+)/')


def rows(con):
    """{id: (hp, hunger)} of the player tribe"""
    out, player = {}, False
    for line in con.call('coop.tribe')['out'].splitlines():
        if line.startswith('tribe '):
            player = '(player)' in line
            continue
        m = ROW.match(line)
        if m and player and m.group(1) != '0':
            out[int(m.group(1))] = (float(m.group(3)), float(m.group(4)))
    return out


def agree(host, guest, ids, secs):
    """wait up to secs for every id's hunger to agree within 3; returns the last (host, guest) rows"""
    t0 = time.time()
    while True:
        h, g = rows(host), rows(guest)
        if all(i in g and abs(g[i][1] - h[i][1]) <= 3 for i in ids if i in h) or time.time() - t0 > secs:
            return h, g
        time.sleep(0.5)


def main():
    host, guest = start('tribe')
    ck = Checks()
    game_runs(guest, 90)
    time.sleep(3)
    h = rows(host)
    ids = sorted(h)
    if not ck(len(ids) >= 2, f'the player tribe has members on the host ({ids})'):
        return ck.result()

    # 1. the guest's mirrors as its own update leaves them after minutes without eating
    for i in ids:
        print(f'  guest #{i}:', guest.call(f'coop.tribe hunger {i} 0')['out'].strip(), flush=True)
    time.sleep(1.0)
    h, g = agree(host, guest, ids, 5)
    for i in ids:
        print(f'  #{i}: host hp {h[i][0]:.0f} hunger {h[i][1]:.1f} | guest {g.get(i, (-1, -1))}', flush=True)
    keyframe('roster', 'the Tribe panel (right edge): the member cards look the same on both games, none red with an '
             'empty bar on the guest')
    bad = [i for i in ids if i not in g or abs(g[i][1] - h[i][1]) > 3]
    ck(not bad, f'every mirror is as hungry as the host\'s member 5 s after it starved on the guest (off: {bad})')
    hurt = [i for i in ids if i in g and abs(g[i][0] - h[i][0]) > 1]
    ck(not hurt, f'and has the host\'s member\'s health (differs: {hurt})')

    # 2. the host's member gets hungry
    i = ids[-1]
    print(f'  host #{i}:', host.call(f'coop.tribe hunger {i} 40')['out'].strip(), flush=True)
    h, g = agree(host, guest, [i], 5)
    ck(i in g and abs(g[i][1] - h[i][1]) <= 3,
       f'the host\'s member #{i} at hunger {h[i][1]:.1f}: the guest\'s mirror {g.get(i, (0, -1))[1]:.1f}')
    print('  guest:', guest.call('coop.tribe anims')['out'].split('\n')[-6:-4], flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
