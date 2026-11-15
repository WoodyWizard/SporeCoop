#!/usr/bin/env python3
"""Tribe: a member over the ground, thrown by a blow or staying up, as the log and the monitor see it (BUG-0945).

A campaign of the 0.7.12 final check failed on the monitor's "the tribe members 5.62 m over the ground (Tribe) on 2
lines" and nothing in its logs said which member or why. The game throws a member that takes a hard blow up (the
fight node 0x05218BB3; 60 hp and more: about 5 m high, over 3 m for a second or two), on the host only: a guest's
mirror keeps its own height. Two trace samples 10 s apart can both fall into such throws. The mod now times every
member's stay over 3 m (`tribe: member #.. is .. m over the ground ...` and `... is down again after .. s` in the log,
`agl_held=<s>` in the trace line next to agl_hi) and the monitor's rule is about a member that STAYS up (5 s and more).

2. Blows the host's leader lives through (`coop.tribe pincer`, the combatant's TakeDamage as a real blow: 60, 100,
   140 hp): each throws it over 3 m; the log has the member, its node, and "down again" after less than 5 s.
   BUG-0946: the guest's mirror of the leader is thrown up too: over 2.5 m over the guest's own ground in the 3 s
   after at least two of the three blows (the host sends its member's height over its ground while it is up,
   tribe.mirror_air; before, the mirror stayed within 0.06 m of its ground). SPORE_FIX_OFF=tribe.mirror_air fails.
1. A member put into deep water while it idles (node 0x04601D70; one with somewhere to go, node 0x05524115 or just
   out of it, swims back to the land within 2-15 s: the test waits for the idle node and tries again, up to three
   times, when the member left) floats on it, 10 m and more over the sea bed (the terrain). BUG-1088: that is a member
   swimming, on every game and also with no session (the full campaign's ground gate failed on one): the mod's
   heights of members count over the water's surface where the planet's water is over the terrain
   (monitor.tribe_water). Checks: the member swims at the water's surface (coop.ground's `water`, 0.6-3.1 m under it by its size), and in
   two trace marks with it there the host's trace has agl_hi under 3 m and the log names no member over the ground.
   `SPORE_FIX_OFF=monitor.tribe_water` fails (the old reading: 10 m and more, the monitor's ground finding).
"""
import math
import re
import time

import lane
from play_tribe import tribe_centres
from coop_tribe_looks import view_both
from twoinst import Checks, game_runs, keyframe, start

OVER = re.compile(r'\[s ([\d.]+)\].*tribe: member #(\d+) of player (\d+) is ([\d.]+) m over the ground.*?: (bt=\w+ .*)$')
DOWN = re.compile(r'\[s ([\d.]+)\].*tribe: member #(\d+) of player (\d+) is down again \(or gone\) after ([\d.]+) s over 3 m, '
                  r'highest ([\d.]+) m')
IDLE = '04601D70'  # the idle node (docs/re/tribe.md#tribe-member-toss)
TRACE = re.compile(r'\[s ([\d.]+)\].*coop: trace: host .* agl=(-?[\d.]+) agl_hi=(-?[\d.]+)(?: agl_held=([\d.]+))?')


def members(con):
    out, tribe = {}, None
    for line in con.call('coop.tribe')['out'].splitlines():
        if line.startswith('tribe '):
            tribe = 'p' if '(player)' in line else 'n'
        m = re.match(r'\s+#(\d+) \S+ owner=(-?\d+) .*hp=([\d.]+) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\).*leader=(\d)', line)
        if m and tribe == 'p' and m.group(1) != '0':
            bt = re.search(r' bt=(\w+)', line)
            out[int(m.group(1))] = dict(owner=int(m.group(2)), hp=float(m.group(3)), bt=bt.group(1) if bt else '',
                                        pos=tuple(float(m.group(i)) for i in (4, 5, 6)), leader=m.group(7) == '1')
    return out


def ground(con, p):
    m = re.search(r'ground ([-\d.]+) point ([-\d.]+) above ([-\d.]+)', con.call('coop.ground %.2f %.2f %.2f' % tuple(p))['out'])
    return (float(m.group(1)), float(m.group(3))) if m else (0.0, 0.0)


def water(con, p):
    """(the planet's water radius, the point's radius) or None (BUG-1088: coop.ground's `water`)"""
    out = con.call('coop.ground %.2f %.2f %.2f' % tuple(p))['out']
    m, r = re.search(r'water ([-\d.]+)', out), re.search(r'point ([-\d.]+)', out)
    return (float(m.group(1)), float(r.group(1))) if m and r else None


def log_since(con, mark):
    with open(lane.game_log(con.port - 47800 + 1), errors='replace') as f:
        return f.read()[mark:].splitlines()


def log_mark(con):
    with open(lane.game_log(con.port - 47800 + 1), errors='replace') as f:
        return len(f.read())


def deep_water(host, c):
    """a point near the village whose ground is 12 m and more under the village's (the sea bed), nearest first"""
    r = math.sqrt(sum(x * x for x in c))
    n = [x / r for x in c]
    ax = (0, 0, 1) if abs(n[2]) < 0.9 else (1, 0, 0)
    t1 = [n[1] * ax[2] - n[2] * ax[1], n[2] * ax[0] - n[0] * ax[2], n[0] * ax[1] - n[1] * ax[0]]
    ln = math.sqrt(sum(x * x for x in t1))
    t1 = [x / ln for x in t1]
    t2 = [n[1] * t1[2] - n[2] * t1[1], n[2] * t1[0] - n[0] * t1[2], n[0] * t1[1] - n[1] * t1[0]]
    home = ground(host, c)[0]
    for d in range(60, 400, 15):
        for k in range(16):
            a = k * math.pi / 8
            p = [c[i] + d * (math.cos(a) * t1[i] + math.sin(a) * t2[i]) for i in range(3)]
            g = ground(host, p)[0]
            if 0 < g < home - 12:
                return p, g, d
    return None, 0.0, 0


def main():
    host, guest = start('tribe')
    ck = Checks()
    game_runs(guest, 90)
    time.sleep(4)
    ms = members(host)
    lead = next((i for i, m in ms.items() if m['leader']), None)
    rest = [i for i in ms if i != lead]
    if not ck(lead is not None and rest, f'the host\'s tribe has a leader and other members ({lead}, {rest})'):
        return ck.result()

    # ---- 1. a member that stays up: on deep water (first: a member that was hit swims home at once)
    c = tribe_centres(host).get('p')
    p, g, d = deep_water(host, c) if c else (None, 0.0, 0)
    if not ck(p is not None, f'deep water near the village ({d} m off, the sea bed at {g:.1f} m)'):
        return ck.result()
    # The member stays on the water when it has nowhere to go: from the node 0x05524115 (after a delivery or a rest)
    # it swims back to the land within 7-15 s, before two trace marks (4 runs of 4, lab 20261111-204713-2, -185533),
    # and right after that node has turned into the idle one (0x04601D70) it still left within 2 s (7 runs of 15,
    # lab 20261111-210637); idle for a while it floats for 27 s and more. So: put it there when it idles, watch
    # that it is still up, and bring it home and try again when it left.
    up, marks, named, wat = 0.0, [], [], None
    for attempt in range(3):
        t0, node = time.time(), ''
        while time.time() - t0 < 30:
            node = members(host).get(lead, {}).get('bt', '')
            if node == IDLE:
                break
            time.sleep(1)
        print(f'  #{lead} (node {node}, after {time.time() - t0:.0f} s) onto the water:',
              host.call('coop.tribe put %d %.2f %.2f %.2f' % (lead, *p))['out'][:60], flush=True)
        up, t0 = 0.0, time.time()
        while up <= 3 and time.time() - t0 < 10:  # it rises from the sea bed to the surface
            time.sleep(1)
            h = members(host).get(lead)
            up = ground(host, h['pos'])[1] if h else 0.0
        h = members(host).get(lead)
        wat = water(host, h['pos']) if h else None
        gm = members(guest).get(lead)
        print(f'    the member floats {up:.1f} m over the sea bed on the host (water {wat}); the guest\'s mirror: '
              f'{ground(guest, gm["pos"])[1] if gm else -1:.1f} m over its sea bed', flush=True)
        mark = log_mark(host)  # the trace marks from now on: the member is on the water
        t0, now = time.time(), up
        while now > 3 and time.time() - t0 < 32:  # two trace marks (10 s apart) while it floats
            time.sleep(1)
            lines = log_since(host, mark)
            marks = [float(m.group(3)) for m in map(TRACE.search, lines) if m]
            named = [m for m in map(OVER.search, lines) if m and int(m.group(2)) == lead]
            h = members(host).get(lead)
            now = ground(host, h['pos'])[1] if h else 0.0
            if len(marks) >= 2:
                break
        if len(marks) >= 2 and now > 3:
            break
        h = members(host).get(lead, {})
        print(f'    it left the water after {time.time() - t0:.0f} s (node {h.get("bt")}, {now:.1f} m over the ground; '
              f'trace {marks}): home and again', flush=True)
        host.call('coop.tribe put %d %.2f %.2f %.2f' % (lead, *ms[lead]['pos']))
        time.sleep(5)
    ck(up > 3, f'the member floats {up:.1f} m over the sea bed on the host')
    ck(wat is not None and -1.0 < wat[0] - wat[1] < 5.0,  # (a swimmer's position is 0.6-3.1 m under it: its size)
       f'it swims at the water\'s surface (water radius, its radius: {wat})')
    ck(len(marks) >= 2 and all(x <= 3 for x in marks),
       f'the host\'s trace lines while it swims: agl_hi {marks} (a member swimming is on the ground, BUG-1088)')
    ck(not named, f'the host\'s log names no member over the ground for it ({[m.group(4) for m in named]})')
    host.call('coop.tribe put %d %.2f %.2f %.2f' % (lead, *ms[lead]['pos']))  # home again
    time.sleep(12)  # past the next trace mark: the lines of part 2 start on the ground

    # ---- 2. blows throw the leader up
    mark = log_mark(host)
    full = ms[lead]['hp']
    guest_tops = []
    for dmg in (60, 100, 140):
        host.call(f'coop.tribe sethp {lead} {full:.0f}')
        shoot = dmg == 100 and view_both(host, guest, members(host)[lead]['pos'], 16.0, 4.0)
        print(f'  blow of {dmg} on #{lead}:', host.call(f'coop.tribe pincer {lead} {rest[0]} {dmg}')['out'][:60], flush=True)
        low = 0.0
        for k in range(12):  # the guest's mirror meanwhile (BUG-0946)
            g = members(guest).get(lead)
            low = max(low, ground(guest, g['pos'])[1] if g else 0.0)
            if shoot and k == 2:
                keyframe('toss', f'the same camera on both games 16 m from the host\'s leader #{lead} a moment after a '
                         f'blow of {dmg}: it is in the air (about 3-5 m over the ground) on both screens', kind='sameview')
            time.sleep(0.25)
        if shoot:
            for c in (host, guest):
                c.call('coop.view off')
        print(f'    the guest\'s mirror of #{lead} was at most {low:.2f} m over its ground in the 3 s after', flush=True)
        guest_tops.append(low)
        time.sleep(5)
    host.call(f'coop.tribe sethp {lead} {full:.0f}')
    ck(sum(1 for x in guest_tops if x > 2.5) >= 2,
       f'the guest sees the throws: its mirror over 2.5 m over its ground after {sum(1 for x in guest_tops if x > 2.5)} '
       f'of 3 blows (highest {", ".join(f"{x:.2f}" for x in guest_tops)} m)')
    time.sleep(1.5)
    lines = log_since(host, mark)
    ups = [m for m in map(OVER.search, lines) if m and int(m.group(2)) == lead]
    downs = [m for m in map(DOWN.search, lines) if m and int(m.group(2)) == lead]
    ck(len(ups) >= 2, f'the host\'s log names the thrown member, {len(ups)} times of 3 blows'
                      + (f' (e.g. {ups[0].group(4)} m, {ups[0].group(5)[:60]})' if ups else ''))
    ck(len(downs) >= 2 and all(float(m.group(4)) < 5.0 for m in downs),
       'and each throw ends: down again after ' + ', '.join(f'{float(m.group(4)):.1f} s (highest {m.group(5)} m)' for m in downs))
    thrown_traces = [m for m in map(TRACE.search, lines) if m and float(m.group(3)) > 3]
    ck(all(m.group(4) is not None and float(m.group(4)) < 5.0 for m in thrown_traces),
       f'a trace line that caught a throw says so: agl_held under 5 s ({[(m.group(3), m.group(4)) for m in thrown_traces]})')

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
