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
   Printed, not asserted: where the guest's mirror is meanwhile (BUG-0946: the guests do not see the throw).
1. A member put into deep water while it idles (node 0x04601D70; one with somewhere to go, node 0x05524115 or just
   out of it, swims back to the land within 2-15 s: the test waits for the idle node and tries again, up to three
   times, when the member left) floats on it, 10 m and
   more over the sea bed (the terrain), and stays: the host's
   trace lines then carry agl_hi over 3 m with agl_held of 5 s and more, twice in a row. That is what the monitor's
   ground rule fails on: the suite allows that finding for this test (allow monitor BUG-0945), so the lab's gate
   line "allowed gate monitor: ... over the ground" in the run is part of the proof.
"""
import math
import re
import time

import lane
from play_tribe import tribe_centres
from twoinst import Checks, game_runs, start

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
    up, held, stays = 0.0, [], []
    for attempt in range(3):
        t0, node = time.time(), ''
        while time.time() - t0 < 30:
            node = members(host).get(lead, {}).get('bt', '')
            if node == IDLE:
                break
            time.sleep(1)
        mark = log_mark(host)
        print(f'  #{lead} (node {node}, after {time.time() - t0:.0f} s) onto the water:',
              host.call('coop.tribe put %d %.2f %.2f %.2f' % (lead, *p))['out'][:60], flush=True)
        up, t0 = 0.0, time.time()
        while up <= 3 and time.time() - t0 < 10:  # it rises from the sea bed to the surface
            time.sleep(1)
            h = members(host).get(lead)
            up = ground(host, h['pos'])[1] if h else 0.0
        gm = members(guest).get(lead)
        print(f'    the member floats {up:.1f} m over the sea bed on the host; the guest\'s mirror: '
              f'{ground(guest, gm["pos"])[1] if gm else -1:.1f} m over its sea bed', flush=True)
        t0, now = time.time(), up
        while now > 3 and time.time() - t0 < 32:  # two trace marks (10 s apart) with 5 s and more behind them
            time.sleep(1)
            lines = log_since(host, mark)
            held = [(float(m.group(3)), float(m.group(4))) for m in map(TRACE.search, lines) if m and m.group(4) is not None]
            stays = [x for x in held if x[0] > 3 and x[1] >= 5.0]
            if len(stays) >= 2:
                break
            h = members(host).get(lead)
            now = ground(host, h['pos'])[1] if h else 0.0
        if len(stays) >= 2:
            break
        h = members(host).get(lead, {})
        print(f'    it left the water after {time.time() - t0:.0f} s (node {h.get("bt")}, {now:.1f} m over the ground; '
              f'trace {held}): home and again', flush=True)
        host.call('coop.tribe put %d %.2f %.2f %.2f' % (lead, *ms[lead]['pos']))
        time.sleep(5)
    ck(up > 3, f'the member floats {up:.1f} m over the sea bed on the host')
    ck(len(stays) >= 2, f'the host\'s trace lines say a member stays up: agl_hi / agl_held {held}')
    host.call('coop.tribe put %d %.2f %.2f %.2f' % (lead, *ms[lead]['pos']))  # home again
    time.sleep(12)  # past the next trace mark: the lines of part 2 start on the ground

    # ---- 2. blows throw the leader up
    mark = log_mark(host)
    full = ms[lead]['hp']
    for dmg in (60, 100, 140):
        host.call(f'coop.tribe sethp {lead} {full:.0f}')
        print(f'  blow of {dmg} on #{lead}:', host.call(f'coop.tribe pincer {lead} {rest[0]} {dmg}')['out'][:60], flush=True)
        low = 0.0
        for _ in range(12):  # the guest's mirror meanwhile (printed: BUG-0946)
            g = members(guest).get(lead)
            low = max(low, ground(guest, g['pos'])[1] if g else 0.0)
            time.sleep(0.25)
        print(f'    the guest\'s mirror of #{lead} was at most {low:.2f} m over its ground in the 3 s after', flush=True)
        time.sleep(5)
    host.call(f'coop.tribe sethp {lead} {full:.0f}')
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
