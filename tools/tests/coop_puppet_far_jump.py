#!/usr/bin/env python3
"""Creature: after the guest's avatar jumps 500-1000 m to a place of the planet neither game has near it, the host
draws its puppet on the host's ground there, not metres over it (BUG-0658).

The address space tours (coop_mem_census.py --tour guest, `coop.abilities goto`) failed the monitor's drawn / ground
rules at a few of the hops: the host drew the puppet 8-36 m over its ground for the whole 20 s stay.

1. Two games on the creature fixture.
2. The guest's avatar goes to HOPS places 500-1000 m away (`coop.abilities goto`, as the tour), 16 s each. The host
   records its puppet frame by frame (`coop.track`: the drawn position, its height over the host's ground `hd`, the
   target's `ht`, the state's flags).
3. In the last 6 s of each stay: the puppet's height over the host's ground and the distance between the drawn puppet
   and the guest's own position (its game's `coop.avatar`).
PASS: at every place the puppet is within 1 m of the host's ground and within 3 m of the guest's own position.
The guest's captions are closed first and after each jump, as its player does (--keep-caption: left up, as in the
tours; the test then fails at hop 2 of the default seed by design).

  SPORE_LANE=4 python3 tools/tests/coop_puppet_far_jump.py [--hops 8] [--seed 388177865] [--approach: at each place
  the guest walks 1.5 s and, where the puppet is off, the host's avatar goes to the same place (diagnostics)]

Found with it (lab 2026-11-08, lane 5): at hop 2 of the default seed the host's game lifted the puppet 5.3 m over
GetHeightAt every frame (134 teleports onto the target in 6 s, put back each frame), and the host's own avatar sent
there by the same command stood 5.3 m up too: the game's collision there lies over GetHeightAt, and the command had
put the guest's avatar inside it while a caption held the guest's game (game time paused, its avatar not simulated:
it stayed inside, did not move under W). With the captions closed the guest's own game moves its avatar itself and the
two games agree (BUG-0658: a test artefact of the tours, docs/re/creature.md#creature-far-goto).
"""
import argparse
import math
import random
import re
import time

from coop_puppet_walk import dump
from twoinst import Checks, start

PF_JUMPING = 0x8
APPROACH = False


def modal(con):
    out = con.call('coop.uistate')['out']
    return 'modal=1' in out or 'input_lock=1' in out


def close_captions(con, tries=6):
    """Enter on a caption that holds the game (paused, input locked) as its player does; True when none is left"""
    for _ in range(tries):
        if not modal(con):
            return True
        con.call('key enter')
        time.sleep(1.5)
    return not modal(con)


def own_pos(con):
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.avatar')['out'])
    return tuple(float(x) for x in m.groups()) if m else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--hops', type=int, default=8)
    ap.add_argument('--seed', type=int, default=388177865)
    ap.add_argument('--approach', action='store_true')
    ap.add_argument('--keep-caption', action='store_true', help='leave a caption that holds the guest\'s game up '
                    '(the address space tours: its avatar is not simulated, BUG-0658)')
    a = ap.parse_args()
    global APPROACH
    APPROACH = a.approach
    host, guest = start('creature')
    ck = Checks()
    time.sleep(5)
    if not a.keep_caption:
        ck(close_captions(guest), f'the guest\'s player closed the captions that held its game '
           f'({guest.call("coop.uistate")["out"].strip()[:120]})')
    rnd = random.Random(a.seed)
    bad, seen = [], 0
    for hop in range(a.hops):
        v = [rnd.gauss(0, 1) for _ in range(3)]
        n = max(1e-6, sum(x * x for x in v) ** 0.5)
        d = [x / n * 1000 for x in v]
        r = guest.call('coop.abilities goto %.1f %.1f %.1f' % tuple(d))['out'].strip()
        if not a.keep_caption:
            time.sleep(1)
            close_captions(guest)  # (a place may bring one: a new area's caption)
        host.call('coop.track start')
        time.sleep(16)
        host.call('coop.track stop')
        gp = own_pos(guest)
        rows = [x for x in dump(host) if x['who'] == 1]
        if not rows or not gp:
            print(f'  hop {hop + 1}: {r}; no samples', flush=True)
            continue
        t_end = rows[-1]['t']
        late = [x for x in rows if x['t'] >= t_end - 6]
        seen += 1
        hd = max(abs(x['hd']) for x in late if x['hd'] is not None)
        apart = max(math.dist(x['d'], gp) for x in late)
        jump = sum(1 for x in late if x['flags'] & PF_JUMPING)
        first = rows[0]
        rg = math.dist(gp, (0, 0, 0))
        print(f'  hop {hop + 1}: {r}; guest r {rg:.2f}, its game\'s ground there '
              f'{guest.call("coop.abilities ground %.2f %.2f %.2f" % gp)["out"].strip()}, host\'s '
              f'{host.call("coop.abilities ground %.2f %.2f %.2f" % gp)["out"].strip()}; last 6 s: puppet over the host\'s '
              f'ground up to {hd:.2f} m, {apart:.1f} m from the guest, jumping {jump}/{len(late)} frames; first frame: '
              f'hd {first["hd"]} ht {first["ht"]} flags {first["flags"]:#x}', flush=True)
        if APPROACH:
            st = guest.call('coop.uistate')['out'].strip()
            print(f'      guest uistate: {st[:200]}', flush=True)
        if APPROACH:  # the guest's own game: does its avatar stay at that height when it moves?
            guest.call('key w down')
            time.sleep(1.5)
            guest.call('key w up')
            time.sleep(1.5)
            g2 = own_pos(guest)
            print(f'      guest walked 1.5 s: r {math.dist(g2, (0, 0, 0)):.2f} (ground '
                  f'{guest.call("coop.abilities ground %.2f %.2f %.2f" % g2)["out"].strip()}), {math.dist(g2, gp):.1f} m '
                  f'from where it stood', flush=True)
        if hd > 1.0 or apart > 3.0:
            bad.append((hop + 1, round(hd, 1), round(apart, 1)))
            mid = [x for x in rows if x['t'] >= rows[0]['t'] + 1][:1] + rows[len(rows) // 2:len(rows) // 2 + 1]
            for x in mid:
                print(f'      t {x["t"] - rows[0]["t"]:.1f}: hd {x["hd"]} ht {x["ht"]} flags {x["flags"]:#x} '
                      f'd r {math.dist(x["d"], (0, 0, 0)):.2f} p r {math.dist(x["p"], (0, 0, 0)):.2f} tgt r '
                      f'{math.dist(x["tgt"], (0, 0, 0)):.2f} teleports {x["tp"]} anim {x["anim"]} gv {x["gv"]} lod {x["lod"]}',
                      flush=True)
            import worlddiff
            for con, who in ((host, 'host'), (guest, 'guest')):
                w = worlddiff.dump(con, 12, gp)
                near = sorted((round(math.dist(x['pos'], gp), 1), worlddiff.noun_name(x['noun']), x.get('scale'))
                              for x in w.rows if x['pos'] and math.dist(x['pos'], gp) < 12)
                print(f'      {who}: objects within 12 m of the guest: {near[:12]}', flush=True)
            if APPROACH:  # the host's own avatar to the same place: where does its own game put it?
                print('      host goto the same place:', host.call('coop.abilities goto %.2f %.2f %.2f' % gp)['out'].strip(), flush=True)
                for _ in range(3):
                    time.sleep(1.5)
                    h2 = own_pos(host)
                    print(f'      host avatar r {math.dist(h2, (0, 0, 0)):.2f}, {math.dist(h2, gp):.1f} m from the guest '
                          f'(host ground {host.call("coop.abilities ground %.2f %.2f %.2f" % h2)["out"].strip()})', flush=True)
            print(f'      teleports in the last 6 s: {late[-1]["tp"] - late[0]["tp"]}; host: '
                  f'{host.call("coop.abilities ground %.2f %.2f %.2f" % late[-1]["d"])["out"].strip()} under the drawn puppet', flush=True)
    ck(seen >= a.hops - 1, f'the host recorded its puppet at the places ({seen} of {a.hops})')
    ck(not bad, f'the host draws the puppet on its ground where the guest is at every place (hop, over m, apart m: {bad})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
