#!/usr/bin/env python3
"""The invariant monitor in Tribe: quiet where the game is right, firing on a planted problem (BUG-0416, BUG-0415).

coop_monitor_plant.py plants one problem per monitor rule in Creature. This test does the Tribe cases of two rules
whose findings in chaos_tribe were the monitor's misjudgement, not a bug:

  jump    (drawn, BUG-0416) the host's pointer jumps ~170 m (`coop.npc marker`, as a click or a minimap jump does)
          0.06 s before two 10 s marks of the session clock in a row: the guest's copy is ~0.1 s behind, so at the
          mark it still rests at the old point (drawn_v 0) 170 m from the host's own pointer. The pointer just
          moved (the host's line: ptr_still=0.0): no violation. With SPORE_MONITOR='{"drawn_still": -1}' (the rule
          before the fix) this phase FAILS (the 164 m of chaos_tribe 20261101-140615).
  plant   (drawn) the guest's game stops driving puppets (`coop.puppet hold on`) and the host's pointer moves once
          and then stands: the guest shows it 170 m off a resting pointer: the monitor must report drawn.
  shares  (link, BUG-0415) the host sends its world twice, 15 s apart (`coop.share`, as two rejoins of a guest do):
          the transfers fill the link with resends (50-99 % of the packets on loopback, BUG-0405) and are no link
          violation; printed: the host's resends / sent per trace line. With SPORE_MONITOR='{"share_after": -1}' the
          rule judges them as before.

The lab's monitor and sync-check gates are off for it (suite.py): the test judges the monitor's findings itself."""
import math
import os
import re
import time

import twoinst
from twoinst import Checks, start


def seen(inv):
    w = twoinst._watch
    return [t for i, _, t in w.violations if i == inv] if w else []


def session_offset(con):
    """time.time() - the session clock, from `coop.diag sync` (the reply with the shortest round trip)"""
    best = None
    for _ in range(6):
        t0 = time.time()
        out = con.call('coop.diag sync')['out']
        t1 = time.time()
        m = re.search(r'clock: on, session time ([\d.]+)', out)
        if m and (best is None or t1 - t0 < best[0]):
            best = (t1 - t0, (t0 + t1) / 2 - float(m.group(1)))
        time.sleep(0.05)
    return best[1] if best else None


def until_before_mark(off, before, margin=1.5):
    """sleep until `before` s ahead of the next 10 s mark of the session clock; the mark"""
    now = time.time() - off
    mark = math.ceil(now / 10.0) * 10.0
    if mark - now < before + margin:
        mark += 10.0
    time.sleep(max(0.0, mark - before - (time.time() - off)))
    return mark


def host_pointer(con):
    for line in con.call('coop.sync')['out'].splitlines():
        f = line.split()
        if len(f) >= 4 and f[0] == 'p0':
            return tuple(float(x) for x in f[1:4])
    return None


def rotate(p, deg):
    """p turned by deg about the axis through the planet's centre perpendicular to p and to +y (stays on the sphere)"""
    x, y, z = p
    ax = (z, 0.0, -x)
    n = math.hypot(ax[0], ax[2]) or 1.0
    k = (ax[0] / n, 0.0, ax[2] / n)
    a = math.radians(deg)
    c, s = math.cos(a), math.sin(a)
    kxp = (k[1] * z - k[2] * y, k[2] * x - k[0] * z, k[0] * y - k[1] * x)
    kd = k[0] * x + k[1] * y + k[2] * z
    return tuple(p[i] * c + kxp[i] * s + k[i] * kd * (1 - c) for i in range(3))


def lines_at(who, kind, t, pid=None):
    """the monitor's record of `who` at session time t (within 0.5 s): own or partner pid"""
    tl = twoinst._watch.tl
    lst = tl.own.get(who, []) if kind == 'own' else tl.partner.get((who, pid), [])
    got = [r for r in lst if r['t'] is not None and abs(r['t'] - t) < 0.5]
    return got[0] if got else None


def main():
    host, guest = start('tribe')
    ck = Checks()
    ck(twoinst._watch is not None, 'the invariant monitor runs')
    cfg = twoinst._watch.cfg
    print(f'  monitor: drawn_still={cfg["drawn_still"]} share_after={cfg["share_after"]}', flush=True)
    off = session_offset(host)
    ck(off is not None, 'the session clock (coop.diag sync)')
    p1 = None
    t0 = time.time()
    while time.time() - t0 < 20 and p1 is None:
        p1 = host_pointer(host)
        time.sleep(1)
    ck(p1 is not None, f'the host\'s pointer (coop.sync p0) {p1}')
    if off is None or p1 is None:
        ck.faults(host=host, guest=guest)
        return ck.result()
    p2 = rotate(p1, 20)
    print(f'  pointer points {tuple(round(v, 1) for v in p1)} and {tuple(round(v, 1) for v in p2)}: '
          f'{math.dist(p1, p2):.0f} m apart', flush=True)

    # jump: two marks in a row with a pointer that jumped just before
    print('  ' + host.call('coop.npc marker %.1f %.1f %.1f' % p1)['out'].strip(), flush=True)
    time.sleep(12)
    marks = []
    for k in range(2):
        mark = until_before_mark(off, 0.06)
        host.call('coop.npc marker %.1f %.1f %.1f' % (p2 if k % 2 == 0 else p1))
        marks.append(mark)
    time.sleep(3)
    twoinst._watch.step()
    jumped = 0
    for mark in marks:
        h, g = lines_at('host', 'own', mark), lines_at('guest', 'partner', mark, 0)
        if not h or not g or 'drawn' not in g:
            print(f'  mark {mark:.0f}: no trace lines (host {bool(h)}, guest {bool(g)})', flush=True)
            continue
        d = math.dist(g['drawn'], h['pos'])
        print(f'  mark {mark:.0f}: the guest draws the host\'s pointer {d:.1f} m off (drawn_v {g["drawn_v"]:.1f}, rtt '
              f'{g["rtt"]} ms), the host\'s ptr_still={h.get("ptr_still")}', flush=True)
        jumped += d > 20 and g['drawn_v'] <= 0.5 and h.get('ptr_still', 99) < 1.0
    ck(jumped == 2, f'the plant landed: at both marks the copy rests at the old point of a pointer that just jumped '
                    f'({jumped} of 2)')
    ck(not seen('drawn'), f'no drawn violation for a pointer that just jumped ({seen("drawn")[:1]})')

    # plant: the copy stays while the pointer rests elsewhere
    print('  ' + guest.call('coop.puppet hold on')['out'].strip(), flush=True)
    time.sleep(0.5)
    host.call('coop.npc marker %.1f %.1f %.1f' % p2)
    t1 = time.time()
    while time.time() - t1 < 40 and not seen('drawn'):
        time.sleep(1)
    print(f'  plant: {seen("drawn")[:1]} after {time.time() - t1:.0f} s', flush=True)
    ck(bool(seen('drawn')), 'the monitor reports the copy of a resting pointer drawn 170 m off (drawn)')
    guest.call('coop.puppet hold off')
    host.call('coop.npc marker off')

    # shares: two world transfers 15 s apart
    n_link = len(seen('link'))
    until_before_mark(off, -1.0)  # 1 s after a mark
    print('  share 1: ' + host.call('coop.share')['out'].strip()[:100], flush=True)
    time.sleep(15)
    print('  share 2: ' + host.call('coop.share')['out'].strip()[:100], flush=True)
    time.sleep(40)
    twoinst._watch.step()
    for p in twoinst._watch.tl.partner.get(('host', 1), [])[-8:]:
        if p['link']:
            print(f'    host -> 1 at s {p["t"]:.0f}: resends={p["link"][0]}/{p["link"][1]} pending={p["link"][3]} '
                  f'state_age={p["age"]}', flush=True)
    ck(len(seen('link')) == n_link, f'no link violation for two world transfers ({seen("link")[n_link:][:1]})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
