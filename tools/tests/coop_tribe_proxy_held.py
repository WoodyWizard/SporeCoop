#!/usr/bin/env python3
"""Tribe: the copy of a host animal that the guest's game holds back is kept on the host animal's path (BUG-0440).

The monitor's proxies gate (one NPC proxy over 3 m off the host's NPC for 2 s) failed chaos_tribe, play_tribe and the
Tribe campaigns now and then: a walking animal (2.5 m/s, its herd's walk near its nest) whose proxy walk_place puts
with the teleport mark onto the host's path every frame (`held 1`) but moves at 0-3 m/s and falls 3-8 m behind, on
every guest at once. The case with the full probe (lab 20261113-191751 chaos_tribe#4, proxy 29, species 0680AA69):
`pushed back 6.9 m/s, off its placing after the call 0.00 m`: the teleport is taken and the game's own update then
moves the body back by 6.9 m/s x the frame, while the placing closes at most the host's speed + 5.5 m/s.

1. Two games on the tribe fixture. The host's animals that the guest has as proxies; the one with the nearest nest
   (3-25 m, `coop.npc raised 25 x y z` on the guest) is walked at 2.5 m/s through the nest's centre to 8 m past it and
   back (`coop.npc dashto`, written on the terrain) for SECONDS; it plays the herbivore idle animation 731EA3EA again
   every 1.5 s (`coop.npc bhv`, relayed as its BT's own starts), as the case's proxy did.
2. The hold, planted: on the guest the game's creature update moves the proxy back (away from its target) at PUSH m/s
   before it runs (`coop.npc pushback`: the measured 6.9 m/s), for the walk.
3. The guest's proxy against its own target (`coop.npc list` err) every 0.2 s.

PASS: the proxy is never more than 3 m off its target for 2 s in a row (the monitor's rule) and its p90 is under 2 m.
Switch: SPORE_FIX_OFF=npc.proxy_hold must FAIL (the case: held at 3-8 m).
NEST_PUSH=0: no planted hold (the walk alone, the control).

  SPORE_LANE=4 python3 tools/tests/coop_tribe_proxy_held.py
"""
import math
import os
import re
import time

from twoinst import Checks, start

SPEED = float(os.environ.get('NEST_SPEED', '2.5'))  # m/s: the herd walk of the reported cases
# the herbivores' idle / look-around animation of the BT's idle node 0xD8DC30 (docs/re/creature.md#creature-bt-anims):
# what the held proxy played in the case of lab 20261113-191751 #3 (empty: none)
ANIM = os.environ.get('NEST_ANIM', '731EA3EA')
PUSH = float(os.environ.get('NEST_PUSH', '6.9'))  # m/s: the planted hold
VEL_K = float(os.environ.get('NEST_VEL_K', '1'))  # (diagnostics) the host's reported velocity times this
ANIM_EVERY = 1.5  # s: the host's NPC starts it again (its BT's idle node repeats it)
SECONDS = 30    # of walking back and forth through the nest
OFF = 3.0       # m: the monitor's proxies rule
OFF_FOR = 2.0   # s
P90 = 2.0       # m (the fix keeps a hold only after 0.3 s over 1.5 m: the brief holds of a turn stay the game's)


def dist(a, b):
    return math.sqrt(sum((a[i] - b[i]) ** 2 for i in range(3)))


def nests(con, at):
    out = []
    for m in re.finditer(r'nest \((\S+) (\S+) (\S+)\) ([\d.]+) m away, raised:(.*)',
                         con.call('coop.npc raised 25 %.2f %.2f %.2f' % at)['out']):
        out.append(dict(pos=tuple(map(float, m.group(1, 2, 3))), d=float(m.group(4)), prof=m.group(5).strip()))
    return out


def host_animals(con):
    out = {}
    for m in re.finditer(r'^\s*#(\d+) \S+ species=(\S+) hp=([\d.]+) dist=\S+ pos=\((\S+) (\S+) (\S+)\)', con.call('coop.npc list')['out'], re.M):
        if float(m.group(3)) > 0:
            out[int(m.group(1))] = dict(species=m.group(2), pos=tuple(map(float, m.group(4, 5, 6))))
    return out


def proxies(con):
    out = {}
    for m in re.finditer(r'^\s*#(\d+) species=(\S+) spawned hp=\S+ dist=\S+ err=([-\d.]+) age=\S+ want=\((\S+) (\S+) (\S+)\) '
                         r'at=\((\S+) (\S+) (\S+)\)', con.call('coop.npc list')['out'], re.M):
        out[int(m.group(1))] = dict(err=float(m.group(3)), want=tuple(map(float, m.group(4, 5, 6))),
                                    at=tuple(map(float, m.group(7, 8, 9))))
    return out


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(4)
    hs, gs = host_animals(host), proxies(guest)
    best = None
    for i in sorted(i for i in hs if i in gs)[:24]:
        for n in nests(guest, hs[i]['pos']):
            if 3.0 < n['d'] < 25.0 and (best is None or n['d'] < best[0]):
                best = (n['d'], i, n)
    ck(best, f'a host animal 3-25 m from a nest that the guest has as a proxy ({len(hs)} host animals, {len(gs)} proxies)')
    if not best:
        return ck.result()
    d, nid, n = best
    a = hs[nid]['pos']
    end = tuple(n['pos'][i] + (n['pos'][i] - a[i]) / d * 8.0 for i in range(3))  # 8 m past the nest's centre
    print(f'  animal #{nid} (species {hs[nid]["species"]}) {d:.1f} m from the nest at ({n["pos"][0]:.1f} {n["pos"][1]:.1f} '
          f'{n["pos"][2]:.1f}); surface over the terrain there: {n["prof"]}', flush=True)
    print('  guest objects at the nest:', guest.call('coop.npc objnear %.2f %.2f %.2f 6' % n['pos'])['out'].strip(), flush=True)
    print('  host:', host.call('coop.npc dashto %d %.2f %.2f %.2f %.1f %d %.2f' % (nid, *end, SPEED, SECONDS + 4, VEL_K))['out'].strip(), flush=True)
    if PUSH > 0:
        print('  guest:', guest.call(f'coop.npc pushback {nid} {PUSH} {SECONDS + 2}')['out'].strip(), flush=True)
    errs, off_since, worst_run, rows, anim_at = [], None, 0.0, 0, 0.0
    t_end = time.time() + SECONDS
    while time.time() < t_end:
        if ANIM and time.time() - anim_at > ANIM_EVERY:
            anim_at = time.time()
            out = host.call(f'coop.npc bhv {nid} {ANIM}')['out'].strip()
            if rows == 0:
                print('  host:', out, flush=True)
        g = proxies(guest).get(nid)
        now = time.time()
        if g:
            rows += 1
            e = g['err']
            errs.append(e)
            if e > OFF:
                off_since = off_since or now
                worst_run = max(worst_run, now - off_since)
            else:
                off_since = None
            if rows % 5 == 0 or e > OFF:
                cur = re.search(rf'^cur {nid} anim=(\w+)', guest.call('coop.npc cur')['out'], re.M)
                print(f'    anim {cur.group(1) if cur else "-"} err {e:5.2f} m, proxy {dist(g["at"], n["pos"]):4.1f} m from the nest, target '
                      f'{dist(g["want"], n["pos"]):4.1f} m', flush=True)
        time.sleep(0.2)
    errs.sort()
    p90 = errs[int(len(errs) * 0.9)] if errs else -1
    print(f'  {len(errs)} samples: p50 {errs[len(errs) // 2] if errs else -1:.2f} m, p90 {p90:.2f} m, max {errs[-1] if errs else -1:.2f} m, '
          f'longest stretch over {OFF} m {worst_run:.1f} s', flush=True)
    m = re.search(r'proxy_holds=(\d+)', guest.call('coop.npc')['out'])
    print(f'  guest: placings kept through the update (npc.proxy_hold): {m.group(1) if m else "?"}', flush=True)
    ck(len(errs) >= SECONDS * 3, f'samples of the proxy ({len(errs)})')
    ck(worst_run < OFF_FOR, f'the proxy never {OFF} m off its target for {OFF_FOR} s ({worst_run:.1f} s)')
    ck(0 <= p90 < P90, f'the proxy on its target (p90 {p90:.2f} m < {P90} m)')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
