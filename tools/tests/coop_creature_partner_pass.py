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


def place(guest, p):
    for _ in range(2):  # (twice: a walk ordered before goes on after one teleport)
        guest.call('coop.npc gopos %.2f %.2f %.2f' % tuple(p))
        time.sleep(1.0)


def target_is(guest, av, ptr):
    tg = u32(guest, av + 0xE7C) if av else None
    return tg, bool(ptr and tg == ptr + 0x5A8)


def host_engage(host, nid):
    """the host's own creature walks to the animal and bites it (a real click on it, the attack key): (gap, ok)"""
    host.call(f'coop.npc gonpc {nid} 4')
    time.sleep(1.5)
    host.call(f'click {W // 2 + 43} {H - 82}')  # the combat stance
    time.sleep(0.5)
    o = aim(host, f'n{nid}')
    if not o:
        return None, False
    host.call(f'click {o[1]} {o[2] - 15}')
    time.sleep(0.5)
    host.call('key 1')
    t0 = time.time()
    while time.time() - t0 < 8:
        tr, gap, rch, st = reach(host)
        if gap is not None and gap <= rch:
            return gap, True
        time.sleep(0.25)
    return reach(host)[1], False


def one_round(host, guest, nid, hid, label):
    """one animal the host fights: (front, click_ok, reached, on_partner, walk judged, closest gap)"""
    hg, engaged = host_engage(host, nid)
    print(f'  [{label}] the host\'s creature at animal n{nid}: gap {hg} (in reach {engaged})', flush=True)
    if not engaged:
        return None
    gs = sample(guest)  # the animal (its proxy) and the host's creature (its puppet) as the guest's game has them
    a, h = (gs.get(f'n{nid}') or (None,))[0], (gs.get('p0') or (None,))[0]
    if not a or not h:
        return None
    d = [y - x for x, y in zip(a, h)]
    ln = math.sqrt(sum(c * c for c in d)) or 1
    u = [c / ln for c in d]
    r = math.sqrt(sum(c * c for c in a)) or 1
    up = [c / r for c in a]
    side = [up[1] * u[2] - up[2] * u[1], up[2] * u[0] - up[0] * u[2], up[0] * u[1] - up[1] * u[0]]  # along the ground
    av, pup = avatar(guest), puppet(guest)
    mine = npcs(guest).get(nid, {}).get('ptr', 0)
    key = f'n{nid}'
    # 1. the target from the side, where nothing stands in front of the animal
    place(guest, [x + c * 6.0 for x, c in zip(a, side)])
    guest.call(f'click {W // 2 + 43} {H - 82}')  # the combat stance
    time.sleep(0.5)
    o = aim(guest, key)
    if not o:
        print(f'  [{label}] the animal is not on the guest\'s screen (from the side)', flush=True)
        return None
    guest.call(f'click {o[1]} {o[2] - 15}')
    time.sleep(0.5)
    tg, ok = target_is(guest, av, mine)
    st = reach(guest)
    if not ok or st[3] != 0:  # (status 12: an animal of the players' own family: it may not be attacked)
        print(f'  [{label}] the guest\'s target from the side {tg or 0:08X} (animal {ok}), key 1 {st}: not a round', flush=True)
        return None
    # 2. (a) the walk: from behind the host's creature, on the line through it, the attack key
    gs = sample(guest)
    a, h = (gs.get(f'n{nid}') or (None,))[0], (gs.get('p0') or (None,))[0]
    d = [y - x for x, y in zip(a, h)]
    ln = math.sqrt(sum(c * c for c in d)) or 1
    place(guest, [x + c / ln * (ln + BEHIND) for x, c in zip(a, d)])
    gs = sample(guest)
    me, hp, an = ((gs.get(k) or (None,))[0] for k in (f'p{hid}', 'p0', f'n{nid}'))
    path = seg_dist(hp, me, an) if me and hp and an else None
    in_path = path is not None and path < 1.0
    tg, still = target_is(guest, av, mine)
    print(f'    key 1 before: {reach(guest)} (target still the animal: {still}; the host\'s creature '
          f'{path if path is None else round(path, 2)} m off the way)', flush=True)
    guest.call('key 1')  # the attack key (1): the game's walk-and-use
    reached, gaps, t0 = False, [], time.time()
    while time.time() - t0 < 12:
        tr, gap, rch, kst = reach(guest)
        if gap is not None:
            gaps.append(round(gap, 1))
            if gap <= rch:
                reached = True
                break
        time.sleep(0.25)
    print(f'  [{label}] (a) the guest\'s walk-and-use from behind the host: gaps {gaps[::4]} -> reached {reached} '
          f'({time.time() - t0:.1f} s); {guest.call("coop.partnerpass")["out"].strip()}', flush=True)
    walk = bool(in_path and still)
    # 3. (b) the click: from behind the host again, the camera on the animal, a click on it
    guest.call('coop.abilities target none')
    place(guest, [x + c / ln * (ln + BEHIND) for x, c in zip(a, d)])
    o = aim(guest, key)
    if not o:
        return False, None, reached, False, walk, min(gaps) if gaps else None
    t = targets(guest)
    pp = t.get('p0')
    gs = sample(guest)
    me, hp = (gs.get(f'p{hid}') or (None,))[0], (gs.get('p0') or (None,))[0]
    hd = math.dist(me, hp) if me and hp else 1e9
    front = bool(pp and math.dist(pp[1:], o[1:]) < FRONT_PX and hd < o[0])
    guest.call(f'click {o[1]} {o[2] - 15}')
    time.sleep(0.5)
    tg, on_animal = target_is(guest, av, mine)
    on_partner = bool(pup and tg == pup + 0x5A8)
    print(f'  [{label}] (b) animal at {o}, the host\'s creature on the guest\'s screen {pp} (in front: {front}); target '
          f'+0xE7C={tg or 0:08X}: animal {on_animal}, partner {on_partner}', flush=True)
    shot(guest, f'partner_pass_{label}')
    return front, on_animal, reached, on_partner, walk, min(gaps) if gaps else None


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
        time.sleep(1)
        ck(' on=0 ' in guest.call('coop.partnerpass')['out'], 'friendly fire on: the guest\'s game skips nothing (coop.partnerpass on=0)')
    else:
        guest.call('coop.find rivals 1')  # the Rivals mode (as Space's vote sets it): nothing skipped either
        time.sleep(0.5)
        ck(' on=0 ' in guest.call('coop.partnerpass')['out'], 'in Rivals the guest\'s game skips nothing (coop.partnerpass on=0)')
        guest.call('coop.find rivals 0')
        time.sleep(0.5)
    hid = (re.search(r'client id=(\d+)', guest.call('coop.status')['out']) or [None, '1'])[1]
    print('  guest: ' + guest.call('coop.partnerpass')['out'].strip(), flush=True)

    used, rounds = set(), []
    try:
        for r in range(a.rounds + 10):
            if sum(1 for x in rounds if x[0]) >= 2 and sum(1 for x in rounds if x[4]) >= 2 and len(rounds) >= a.rounds:
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
    way = [x for x in rounds if x[4]]  # the host in the way, the target an animal it may attack: the walk is judged
    print(f'  rounds: {len(rounds)}, with the host in front of the animal: {len(front)}; '
          f'click on the animal {sum(1 for x in front if x[1])}, on the partner {sum(1 for x in front if x[3])}, '
          f'reached {sum(1 for x in front if x[2])}', flush=True)
    if a.ff:
        print('  (friendly fire on: the old behaviour, not judged)', flush=True)
    else:
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
