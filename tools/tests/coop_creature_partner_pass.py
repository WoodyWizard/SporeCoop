#!/usr/bin/env python3
"""Creature: the second hunter reaches the animal the partner stands at, and its click picks the animal (BUG-0913).

The owner's decision (2026-11-11, docs/plan-0.7.13.md): while friendly fire is off (and outside Rivals) a partner's
creature does not block the way (a player walks through it) and a click does not pick it as the attack target (the
click goes to the animal behind it). Before: the second player to attack an animal the partner stands at, coming
from the partner's side, stopped ~2 m short behind the partner's creature (the game's walk-and-use, goal 0x20) and
its attack key did nothing; a click on the animal often took the partner's creature standing in front of it.

Rounds (one animal each, not of the players' family; the guest is the second hunter):
1. The host's creature stands right at the animal (`coop.npc gonpc <id> 1.0`: 1 m from its centre).
2. The guest takes the animal as its combat target from the animal's other side (a real click; the host's creature is
   behind the animal from there), then is put 6 m behind the host's creature on the line through it (`coop.npc
   gopos`) and presses the attack key (real input): the game's walk-and-use must bring it within the bite's reach
   (the game's gap, `coop.abilities reach`) within 12 s, with no other input than the key pressed again when the
   walk did not start. Judged when the host's creature is under 1.8 m from the animal and under 0.5 m off the way.
3. From behind the host's creature again, the camera turned to the animal (right-drag), a real click on the animal:
   the game's target field of the guest's avatar (+0xE7C, creature + 0x5A8) must name the animal (its proxy), not the
   host's creature (the puppet). Judged when the host's creature is within 90 px of the click point and nearer.
Before: `coop.find rivals 1` (and with --ff friendly fire on) must switch the skipping off (`coop.partnerpass on=0`).
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
        m = re.match(r'\s*#(\d+) (?:([0-9A-Fa-f]{8}) )?species=([0-9A-F]+) (?:\S+ )?hp=([\d.]+) dist=([\d.]+)( DEAD)?', line)
        if m:
            p = re.search(r'\bptr=(?:0x)?([0-9A-Fa-f]+)', line)
            d[int(m.group(1))] = dict(species=m.group(3), hp=float(m.group(4)), dist=float(m.group(5)),
                                      dead=bool(m.group(6)) or 'game_dead=1' in line,
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
    """the host's creature right at the animal (`coop.npc gonpc <id> 1.0`: 1 m from its centre, as the partner
    that fights it stands): the host's own gap to it"""
    print('  host: ' + host.call(f'coop.npc gonpc {nid} 1.0')['out'].strip(), flush=True)
    time.sleep(1.5)
    hs = sample(host)
    a, h = (hs.get(f'n{nid}') or (None,))[0], (hs.get('p0') or (None,))[0]
    return (round(math.dist(a, h), 2), True) if a and h and math.dist(a, h) < 3.0 else (None, False)


def one_round(host, guest, nid, hid, label):
    """one animal the host fights: (front, click_ok, reached, on_partner, walk judged, closest gap)"""
    guest.call('coop.abilities target none')  # (no target left from the round before)
    hg, engaged = host_engage(host, nid)
    print(f'  [{label}] the host\'s creature at animal n{nid}: {hg} m from its centre', flush=True)
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
    # 1. the target from the animal's other side (the host's creature behind it there: the click takes the animal
    # with the fix or without, so the walk is judged on its own)
    place(guest, [x - c * 6.0 + sc * 1.5 for x, c, sc in zip(a, u, side)])
    guest.call(f'click {W // 2 + 43} {H - 82}')  # the combat stance
    time.sleep(0.5)
    o = aim(guest, key)
    if not o:
        print(f'  [{label}] the animal is not on the guest\'s screen (from its other side)', flush=True)
        return None
    guest.call(f'click {o[1]} {o[2] - 15}')
    time.sleep(0.5)
    tg, ok = target_is(guest, av, mine)
    st = reach(guest)
    if not ok or st[3] != 0:  # (status 12: an animal of the players' own family: it may not be attacked)
        print(f'  [{label}] the guest\'s target from the other side {tg or 0:08X} (animal {ok}), key 1 {st}: not a round', flush=True)
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
    reached, gaps, t0, presses = False, [], time.time(), 1
    while time.time() - t0 < 12:
        tr, gap, rch, kst = reach(guest)
        if gap is not None:
            gaps.append(round(gap, 1))
            if gap <= rch:
                reached = True
                break
            # (a press now and then does not start the walk at all, with or without the fix: a player presses again;
            # stopped behind the partner, "every further press does nothing", BUG-0913)
            if presses < 4 and time.time() - t0 > 1.5 * presses and len(gaps) > 5 and abs(gaps[-1] - gaps[-6]) < 0.05:
                guest.call('key 1')
                presses += 1
        time.sleep(0.25)
    print(f'  [{label}] (a) the guest\'s walk-and-use from behind the host: gaps {gaps[::4]} -> reached {reached} '
          f'({time.time() - t0:.1f} s, {presses} press(es)); {guest.call("coop.partnerpass")["out"].strip()}', flush=True)
    # judged: the host's creature right at the animal (under 1.8 m from its centre) and on the guest's way to it
    walk = bool(still and path is not None and path < 0.5 and hg is not None and hg < 1.8)
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

    # the players' family at the nest may not be attacked (key 1 status 12): its species, from the guest's proxies (fam=1)
    gl = guest.call('coop.npc list')['out']
    family = set(re.findall(r'species=([0-9A-F]{8}) .*?fam=1', gl))
    print(f'  the players\' family species (not hunted): {sorted(family)}', flush=True)
    used, rounds = set(), []
    try:
        for r in range(a.rounds + 14):
            if sum(1 for x in rounds if x[0]) >= 2 and sum(1 for x in rounds if x[4]) >= 3 and len(rounds) >= a.rounds:
                break
            cand = sorted((v['dist'], k) for k, v in npcs(host).items()
                          if not v['dead'] and k not in used and v['dist'] < 120 and v.get('species') not in family)
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
        ck(len(way) >= 2, f'rounds with the host\'s creature right at the animal in the guest\'s way, the animal the target: {len(way)} >= 2')
        ck(way and all(x[2] for x in way), 'the guest\'s attack key walks it to the animal past the host\'s creature, '
           f'no side step ({sum(1 for x in way if x[2])} of {len(way)}; closest gaps {[x[5] for x in way]})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
