#!/usr/bin/env python3
"""Creature: the second hunter reaches the animal the partner stands at, and its click picks the animal (BUG-0913).

The owner's decision (2026-11-11, docs/plan-0.7.13.md): while friendly fire is off (and outside Rivals) a partner's
creature does not block the way (a player walks through it) and a click does not pick it as the attack target (the
click goes to the animal behind it). Before: the second player to attack an animal the partner stands at, coming
from the partner's side, stopped ~2 m short behind the partner's creature (the game's walk-and-use, goal 0x20) and
its attack key did nothing; a click on the animal often took the partner's creature standing in front of it.

Rounds (one animal each, the guest is the second hunter):
1. The host's creature stands right at an animal of the host's world (`coop.npc gonpc <id> 1.2`).
2. The guest's creature is put on the line from the animal through the host's creature, 7 m behind the host
   (`coop.npc gopos`), its camera turned (right-drag) until the animal is in the middle of its screen: the host's
   creature stands between the camera and the animal.
3. The guest takes the combat stance and clicks at the animal's screen point (real input), as a player does:
   the game's own target field of the guest's avatar (+0xE7C, a combatant: creature + 0x5A8) must name the
   animal's creature in the guest's game (its proxy), not the host's creature (the puppet).
4. The guest presses the attack key once (real input): the game's walk-and-use takes it to the animal; within
   12 s the game's own gap to the target (`coop.abilities reach`) is within the bite's reach, with no other
   input (no side step).
At least 2 rounds must have the host's creature in front of the animal on the guest's screen (within 60 px of
the click point and nearer to the camera), and every such round must pass 3 and 4.
SPORE_FIX_OFF=creature.partner_pass must FAIL (the click takes the host's creature, or the walk stops behind it).
--ff: the same with friendly fire on (all.friendly_fire): the old behaviour is kept; printed only.

  SPORE_LANE=2 python3 tools/tests/coop_creature_partner_pass.py [--rounds 4]
"""
import argparse
import math
import re
import struct
import time

from coop_creature_kills import read
from coop_sync import sample, seg_dist
import settings_lib as sl
from twoinst import Checks, shot, start

W, H = 1440, 1080
BEHIND = 6.0
FRONT_PX = 90


def u32(con, a):
    b = read(con, a, 4)
    return struct.unpack('<I', b)[0] if len(b) == 4 else None


def avatar(con):
    m = re.search(r'local=([0-9A-Fa-f]{8})', con.call('coop.avatar')['out'])
    return int(m.group(1), 16) if m else 0


def puppet(con):
    m = re.search(r'puppet=([0-9A-F]{8})', con.call('coop.status')['out'])
    return int(m.group(1), 16) if m else 0


def npcs(con):
    d = {}
    for line in con.call('coop.npc list')['out'].splitlines():
        # host: "#id <ptr> species=.. hp=.. dist=.. [DEAD]", guest: "#id species=.. <state> hp=.. dist=.. ... ptr=<ptr>"
        m = re.match(r'\s*#(\d+) (?:([0-9A-Fa-f]{8}) )?species=[0-9A-F]+ (?:\S+ )?hp=([\d.]+) dist=([\d.]+)( DEAD)?', line)
        if m:
            p = re.search(r'\bptr=(?:0x)?([0-9A-Fa-f]+)', line)
            d[int(m.group(1))] = dict(hp=float(m.group(3)), dist=float(m.group(4)), dead=bool(m.group(5)) or 'game_dead=1' in line,
                                      ptr=int(p.group(1), 16) if p else (int(m.group(2), 16) if m.group(2) else 0))
    return d


def targets(con):
    """{key: (dist, x, y)}: shared objects on this screen, the partner as 'p<id>'"""
    d = {}
    for line in con.call('coop.targets all')['out'].splitlines():
        f = line.split()
        if len(f) == 5 and f[0] in ('obj', 'player'):
            try:  # (a player line has its owner in the third field, no distance)
                d[('p' if f[0] == 'player' else '') + f[1]] = (float(f[2]) if f[0] == 'obj' else -1.0, int(f[3]), int(f[4]))
            except ValueError:
                pass
    return d


def reach(con):
    """(target, gap, the bite's reach, its status): the game's reach of key 1, the ability in column 0 of the bar's
    combat row 1 (play_creature.py's reading; the fixture creature's bite: reach 1 m)"""
    out = con.call('coop.abilities reach')['out']
    m = re.search(r'target (\S+) dead=(\d) gap=([-\d.]+)', out)
    abs_ = {int(x.group(1)): (float(x.group(2)), int(x.group(3))) for x in
            re.finditer(r'ab (\d+) type=\S+ row=\S+ key=\S+ reach=([-\d.]+) min=\S+ status=(-?\d+)', out)}
    b = re.search(r'bar 1:((?: -?\d+)+)', out)
    col0 = int(b.group(1).split()[0]) if b else None
    rch, st = abs_.get(col0, (1.0, None))
    return (m.group(1) if m else None, float(m.group(3)) if m else None, rch, st)


def aim(con, key):
    """right-drag the camera until object key is near the middle of the screen; its (dist, x, y)"""
    o = None
    for _ in range(20):
        o = targets(con).get(key)
        if o and abs(o[1] - W // 2) < 70 and 150 < o[2] < H - 220:
            return o
        dx = 380 if not o else max(-300, min(300, int((o[1] - W // 2) * 0.6)))
        con.call(f'drag {W // 2} {H // 2} {W // 2 + dx} {H // 2} right')
        time.sleep(0.5)
    return o if o and 20 < o[1] < W - 20 and 90 < o[2] < H - 170 else None


def walk_through(host, guest, gid, label):
    """(a) the guest's creature is put 6 m from the host's on a line, its camera turned to the host's creature, and a
    real click on the ground 150 px above it on the screen (beyond it) walks it there: (closest m, passed it)"""
    gs = sample(guest)
    me, hp = (gs.get(f'p{gid}') or (None,))[0], (gs.get('p0') or (None,))[0]
    if not me or not hp:
        return None
    d = [y - x for x, y in zip(hp, me)]
    ln = math.sqrt(sum(c * c for c in d)) or 1
    g = [x + c / ln * 6.0 for x, c in zip(hp, d)]
    guest.call('coop.npc gopos %.2f %.2f %.2f' % tuple(g))
    time.sleep(1.5)
    o = aim(guest, 'p0')
    if not o:
        print(f'  [{label}] the host\'s creature is not on the guest\'s screen', flush=True)
        return None
    gs = sample(guest)
    start, hp = (gs.get(f'p{gid}') or (None,))[0], (gs.get('p0') or (None,))[0]
    guest.call(f'click {o[1]} {max(100, o[2] - 150)}')
    way, t0 = [], time.time()
    while time.time() - t0 < 6:
        gs = sample(guest)
        a, b = (gs.get(f'p{gid}') or (None,))[0], (gs.get('p0') or (None,))[0]
        if a and b:
            way.append((a, b))
        time.sleep(0.1)
    if not way or not start or not hp:
        return None
    u = [y - x for x, y in zip(start, hp)]
    lu = math.sqrt(sum(c * c for c in u)) or 1
    closest = min(math.dist(a, b) for a, b in way)
    past = max(sum((ac - bc) * uc / lu for ac, bc, uc in zip(a, b, u)) for a, b in way)  # beyond the host along the way
    print(f'  [{label}] walk: the guest {math.dist(start, hp):.1f} m from the host\'s creature, closest {closest:.2f} m, '
          f'at most {past:.2f} m beyond it; {guest.call("coop.partnerpass")["out"].strip()}', flush=True)
    return closest, past > 1.0


def one_round(host, guest, nid, hid, label):
    """one animal: (front, click_ok, reached, notes)"""
    print('  host: ' + host.call(f'coop.npc gonpc {nid} 1.2')['out'].strip(), flush=True)
    time.sleep(1.5)
    hs = sample(guest)  # the animal (its proxy) and the host's creature (its puppet) as the guest's game has them
    a, h = (hs.get(f'n{nid}') or (None,))[0], (hs.get('p0') or (None,))[0]
    if not a or not h:
        return None
    d = [y - x for x, y in zip(a, h)]
    ln = math.sqrt(sum(c * c for c in d)) or 1
    g = [x + c / ln * (ln + BEHIND) for x, c in zip(a, d)]
    print('  guest: ' + guest.call('coop.npc gopos %.2f %.2f %.2f' % tuple(g))['out'].strip(), flush=True)
    time.sleep(1.5)
    guest.call(f'click {W // 2 + 43} {H - 82}')  # the combat stance (the red button)
    time.sleep(0.5)
    key = f'n{nid}'
    o = aim(guest, key)
    if not o:
        print(f'  [{label}] the animal is not on the guest\'s screen', flush=True)
        return None
    t = targets(guest)
    pp = t.get('p0')  # the host's creature (its puppet) on the guest's screen
    gs = sample(guest)
    me, hp = (gs.get(f'p{hid}') or (None,))[0], (gs.get('p0') or (None,))[0]
    hd = math.dist(me, hp) if me and hp else 1e9  # the guest to the host's creature (obj lines: to the animal)
    front = bool(pp and math.dist(pp[1:], o[1:]) < FRONT_PX and hd < o[0])
    an = (gs.get(f'n{nid}') or (None,))[0]
    path = seg_dist(hp, me, an) if me and hp and an else None  # the host's creature off the guest's way to the animal
    in_path = path is not None and path < 1.5 and hd < (math.dist(me, an) if me and an else 0)
    av, pup = avatar(guest), puppet(guest)
    mine = npcs(guest).get(nid, {}).get('ptr', 0)
    guest.call(f'click {o[1]} {o[2] - 15}')
    time.sleep(0.5)
    tg = u32(guest, av + 0xE7C) if av else None
    on_animal = bool(mine and tg == mine + 0x5A8)
    on_partner = bool(pup and tg == pup + 0x5A8)
    words = guest.call('coop.abilities reach')['out'].splitlines()[:1]
    print(f'  [{label}] animal n{nid} at {o}, the host\'s creature on the guest\'s screen {pp} (in front: {front}, {path if path is None else round(path, 2)} m off the way: in the way {in_path}); '
          f'target +0xE7C={tg or 0:08X}: animal {on_animal} (proxy {mine:08X}), partner {on_partner} (puppet {pup:08X}); {words}',
          flush=True)
    reached, gaps = False, []
    if on_animal:
        print(f'    key 1 before: (target, gap, reach, status) = {reach(guest)}', flush=True)
        guest.call('key 1')  # the attack key (1): the game's walk-and-use
        t0 = time.time()
        while time.time() - t0 < 12:
            tr, gap, rch, kst = reach(guest)
            if gap is not None:
                gaps.append(round(gap, 1))
                if gap <= rch:
                    reached = True
                    break
            time.sleep(0.25)
        print(f'    key 1 after: {reach(guest)}', flush=True)
        print(f'  [{label}] the guest\'s walk-and-use: gaps {gaps[::4]} -> reached {reached} '
              f'({time.time() - t0:.1f} s); {guest.call("coop.partnerpass")["out"].strip()}', flush=True)
    shot(guest, f'partner_pass_{label}')
    return front, on_animal, reached, on_partner, in_path, min(gaps) if gaps else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=4)
    ap.add_argument('--ff', action='store_true')
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    time.sleep(3)
    if a.ff:
        ck(sl.force(host, [guest], 'all.friendly_fire', 'on'), 'friendly fire on for both')
    hid = (re.search(r'client id=(\d+)', guest.call('coop.status')['out']) or [None, '1'])[1]
    print('  guest: ' + guest.call('coop.partnerpass')['out'].strip(), flush=True)

    walks = []
    for r in range(5):
        w = walk_through(host, guest, hid, f'w{r + 1}')
        if w:
            walks.append(w)
        if len(walks) >= 3:
            break
    used, rounds = set(), []
    try:
        for r in range(a.rounds + 10):
            if sum(1 for x in rounds if x[0]) >= 2 and sum(1 for x in rounds if x[4] and x[1]) >= 2 and len(rounds) >= a.rounds:
                break
            cand = sorted((v['dist'], k) for k, v in npcs(host).items() if not v['dead'] and k not in used and v['dist'] < 120)
            if not cand:
                break
            nid = cand[0][1]
            used.add(nid)
            res = one_round(host, guest, nid, hid, f'r{r + 1}')
            if res:
                rounds.append(res)
            guest.call(f'click {W // 2} {H // 2 + 200}')  # (a click on the ground: the walk ends)
            time.sleep(1)
    finally:
        if a.ff:
            sl.force(host, [guest], 'all.friendly_fire', 'off')
    front = [x for x in rounds if x[0]]
    way = [x for x in rounds if x[4] and x[1]]  # the host in the way and the target the animal: the walk is judged
    print(f'  rounds: {len(rounds)}, with the host in front of the animal: {len(front)}; '
          f'click on the animal {sum(1 for x in front if x[1])}, on the partner {sum(1 for x in front if x[3])}, '
          f'reached {sum(1 for x in front if x[2])}', flush=True)
    print(f'  walks: {walks}', flush=True)
    if a.ff:
        print('  (friendly fire on: the old behaviour, not judged)', flush=True)
    else:
        ck(len(walks) >= 2, f'walks towards the host\'s creature: {len(walks)} >= 2')
        ck(walks and all(c < 0.8 and p for c, p in walks), 'the guest walks through the host\'s creature, not round it or '
           f'stopped by it (closest, passed: {walks})')
        ck(len(front) >= 2, f'rounds with the host\'s creature in front of the animal on the guest\'s screen: {len(front)} >= 2')
        ck(front and all(x[1] for x in front), 'the guest\'s click goes to the animal behind the host\'s creature (the game\'s target)'
           f' ({sum(1 for x in front if x[1])} of {len(front)})')
        ck(len(way) >= 2, f'rounds with the host\'s creature in the guest\'s way and the animal as the target: {len(way)} >= 2')
        ck(way and all(x[2] for x in way), 'the guest\'s attack key walks it to the animal past the host\'s creature, '
           f'no side step ({sum(1 for x in way if x[2])} of {len(way)}; closest gaps {[x[5] for x in way]})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
