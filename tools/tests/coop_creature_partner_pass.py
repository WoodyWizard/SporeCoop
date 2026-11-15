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
Judged (2026-11-12, fix/w48-pass): a walk that reached the animal (pass) or stopped within 2.5 m of the host's creature
with nothing else nearer (BUG-0913: fail); a press that never started the walk 6 m behind the host (moved under
0.3 m, with the fix on and off, lab 20261112-111650 / -113440 / -113425: the test's gopos had put the guest's creature
inside a nest mound's collision, BUG-0964; gopos puts it onto the surface since tools.gopos_surface) or a stop at another
creature is printed, tried again from 4.5 and 8 m behind, and not judged when it happens there too; a walk that
came within the reach between two samples and bit (the animal lost health in the host's game) counts as reached. A click that took the animal (pass) or the host's creature (fail); one that took
nothing or another animal along the ray is printed and not judged. More rounds are played until enough are judged.
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
from twoinst import Checks, ability_mode, set_stance, shot, start

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


def walk_once(host, guest, nid, hid, av, mine, a, d, ln, behind, label):
    """the attack key pressed from `behind` m behind the host's creature on the line through it: (reached, gaps,
    what stopped it, the host's creature's distance off the way, the target still the animal)"""
    place(guest, [x + c / ln * (ln + behind) for x, c in zip(a, d)])
    gs = sample(guest)
    me, hp, an = ((gs.get(k) or (None,))[0] for k in (f'p{hid}', 'p0', f'n{nid}'))
    path = seg_dist(hp, me, an) if me and hp and an else None
    in_path = path is not None and path < 1.0
    tg, still = target_is(guest, av, mine)
    now_ptr = npcs(guest).get(nid, {}).get('ptr', 0)
    print(f'    key 1 before: {reach(guest)} (target still the animal: {still}; the host\'s creature '
          f'{path if path is None else round(path, 2)} m off the way; stance {ability_mode(guest)}, the animal\'s proxy '
          f'{mine:08X} now {now_ptr:08X})', flush=True)
    hp_before = npcs(host).get(nid, {}).get('hp')  # (the animal's health in the host's game: did a bite land)
    guest.call('key 1')  # the attack key (1): the game's walk-and-use
    reached, gaps, t0, presses, bitten = False, [], time.time(), 1, False
    while time.time() - t0 < 12:
        tr, gap, rch, kst = reach(guest)
        if gap is not None:
            gaps.append(round(gap, 1))
            if gap <= rch:
                reached = True
                break
            # (a press now and then does not start the walk at all, with or without the fix: a player presses again;
            # stopped behind the partner, "every further press does nothing", BUG-0913)
            if presses < 4 and time.time() - t0 > 1.5 * presses and len(gaps) > 12 and abs(gaps[-1] - gaps[-13]) < 0.05:
                guest.call('key 1')
                presses += 1
        time.sleep(0.1)
    if not reached:
        # the walk came within the bite's reach between two samples (~0.3 s, 1 m at its speed), the walk-and-use bit
        # and the animal stepped back from the bite (gap 1.1 -> 1.4-2.0 with the partner 1.6-2.0 m away, lab
        # 20261112-114736): the bite landed when the animal lost health in the host's game
        hp_after = npcs(host).get(nid, {}).get('hp')
        bitten = hp_before is not None and hp_after is not None and hp_after < hp_before
        print(f'    the animal\'s health in the host\'s game {hp_before} -> {hp_after}: bitten {bitten}', flush=True)
        reached = bitten
    print(f'  [{label}] (a) the guest\'s walk-and-use from {behind} m behind the host: gaps {gaps[::8]} -> reached {reached} '
          f'({time.time() - t0:.1f} s, {presses} press(es)); {guest.call("coop.partnerpass")["out"].strip()}', flush=True)
    blocker = None
    if not reached:  # what stopped it: the partner's creature (BUG-0913), or something else (not judged)
        gs = sample(guest)
        me2, hp2 = (gs.get(f'p{hid}') or (None,))[0], (gs.get('p0') or (None,))[0]
        others = sorted((round(math.dist(me2, v[0]), 1), k) for k, v in gs.items()
                        if me2 and k.startswith('n') and k != f'n{nid}')
        moved = round(math.dist(me, me2), 1) if me and me2 else None
        dh = round(math.dist(me2, hp2), 1) if me2 and hp2 else None
        an2 = (gs.get(f'n{nid}') or (None,))[0]
        # (from the animal's centre: the avatar and the host's creature; BUG-0913's stop is behind the host's creature)
        da, dha = (round(math.dist(x, an2), 1) if x and an2 else None for x in (me2, hp2))
        behind = da is not None and dha is not None and da > dha + 0.3
        # stopped at the partner: within 2.5 m of the host's creature, behind it (farther from the animal's centre), and
        # nothing else nearer (the BUG-0913 stop is 1.4 m behind it); one that came past it, as near the animal as the
        # host's creature, was not stopped by it (lab 20261112-122259 r10: gap 1.3, then 1.7 beside it, no bite); a walk that never started (moved under 0.3 m) 6 m behind it was a creature put inside a nest
        # mound's collision (docs/re/creature.md#creature-walk-held-in-mound), another creature in the way is the game's
        blocker = ('partner' if dh is not None and dh < 2.5 and (not others or others[0][0] >= dh) and (moved or 0) >= 0.3
                   and behind
                   else 'no start' if moved is not None and moved < 0.3
                   else f'beside the partner (from the animal {da} m, the host\'s creature {dha} m)' if dh is not None and dh < 2.5
                   else f'other {others[:1]}')
        sp = read(guest, av + 0xC0 + 0x1E0, 8) if av else b''  # cLocomotiveObject desired / standard speed
        desired = round(struct.unpack('<f', sp[:4])[0], 2) if len(sp) == 8 else None
        print(f'    not reached: desired speed {desired} (the game still walks it when > 0), moved {moved} m, the host\'s creature {dh} m away, nearest others {others[:3]}, '
              f'stance {ability_mode(guest)}, key 1 {reach(guest)} -> stopped by: {blocker}', flush=True)
        # (BUG-0964 / BUG-0965: the game's view of the stop: the locomotion's request and steering, the nearest nest,
        # the avatar's radius against the terrain and the surface; a creature inside a nest mound's collision is held
        # in every direction, docs/re/creature.md#creature-walk-held-in-mound)
        print('    ' + '\n      '.join(x for x in guest.call('coop.abilities walk raw')['out'].splitlines()
                                       if x.startswith(('speed', 'steer', 'nest'))), flush=True)
        if blocker != 'no start' and me2 and an2:  # (stopped on the way: held, or only that way? a plain walk 2 m
            # to the side and 2 m back, the game's WalkTo; the partner's creature in the way leaves both free)
            r0 = math.sqrt(sum(c * c for c in me2)) or 1
            g = [a - b for a, b in zip(an2, me2)]
            sd = [me2[1] / r0 * g[2] - me2[2] / r0 * g[1], me2[2] / r0 * g[0] - me2[0] / r0 * g[2],
                  me2[0] / r0 * g[1] - me2[1] / r0 * g[0]]
            sl, gl = math.sqrt(sum(c * c for c in sd)) or 1, math.sqrt(sum(c * c for c in g)) or 1
            free = []
            for v in ([c / sl * 2 for c in sd], [-c / gl * 2 for c in g]):
                q1 = (sample(guest).get(f'p{hid}') or (None,))[0]
                guest.call('coop.abilities walkto %.3f %.3f %.3f' % tuple(x + y for x, y in zip(q1, v)))
                time.sleep(1.5)
                q2 = (sample(guest).get(f'p{hid}') or (None,))[0]
                free.append(round(math.dist(q1, q2), 2) if q1 and q2 else -1)
            print(f'    a plain walk 2 m to the side / back from there: moved {free} m '
                  f'({"held in every direction" if max(free) < 0.3 else "free to move"})', flush=True)
    return reached, gaps, blocker, path, still


def one_round(host, guest, nid, hid, label):
    """one animal the host fights: (front, click_ok, reached, on_partner, walk judged, closest gap)"""
    guest.call('coop.abilities target none')  # (no target left from the round before)
    hg, engaged = host_engage(host, nid)
    if not engaged:  # (the host's walk to it ended short: once more)
        hg, engaged = host_engage(host, nid)
    print(f'  [{label}] the host\'s creature at animal n{nid}: {hg} m from its centre', flush=True)
    t1 = time.time()  # (after a long jump of the host: the guest's picture of it catches up first, ~10 s for 90 m)
    while time.time() - t1 < 15:
        hh, gh = (sample(host).get('p0') or (None,))[0], (sample(guest).get('p0') or (None,))[0]
        if not hh or not gh or math.dist(hh, gh) < 1.5:
            break
        time.sleep(0.5)
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
    set_stance(guest, 'combat')
    tg, ok, st = None, False, (None, None, 1.0, None)
    for dy in (15, 0, 30):  # (a click that missed the animal, which walks on: aimed and clicked again, as a player does)
        o = aim(guest, key)
        if not o:
            print(f'  [{label}] the animal is not on the guest\'s screen (from its other side)', flush=True)
            return None
        guest.call(f'click {o[1]} {o[2] - dy}')
        time.sleep(0.5)
        tg, ok = target_is(guest, av, mine)
        st = reach(guest)
        if ok:
            break
    if not ok or st[3] != 0:  # (status 12: an animal of the players' own family: it may not be attacked)
        print(f'  [{label}] the guest\'s target from the other side {tg or 0:08X} (animal {ok}), key 1 {st}: not a round', flush=True)
        return None
    # 2. (a) the walk: from behind the host's creature, on the line through it, the attack key
    gs = sample(guest)
    a, h = (gs.get(f'n{nid}') or (None,))[0], (gs.get('p0') or (None,))[0]
    d = [y - x for x, y in zip(a, h)]
    ln = math.sqrt(sum(c * c for c in d)) or 1
    # (a press that never started the walk, or a stop at another creature, is not the partner's: tried again from
    # another distance, 3 times at most)
    for behind in (BEHIND, 4.5, 8.0):
        reached, gaps, blocker, path, still = walk_once(host, guest, nid, hid, av, mine, a, d, ln, behind, label)
        if reached or blocker == 'partner' or not still:
            break
    # judged: the host's creature right at the animal (under 1.8 m from its centre) and on the guest's way to it
    walk = bool(still and path is not None and path < 0.5 and hg is not None and hg < 1.8)
    # 3. (b) the click: from behind the host again, the camera on the animal, a click on it
    guest.call('coop.abilities target none')
    place(guest, [x + c / ln * (ln + BEHIND) for x, c in zip(a, d)])
    o = aim(guest, key)
    if not o:
        return False, None, reached, False, walk, min(gaps) if gaps else None, blocker
    t = targets(guest)
    pp = t.get('p0')
    gs = sample(guest)
    me, hp = (gs.get(f'p{hid}') or (None,))[0], (gs.get('p0') or (None,))[0]
    hd = math.dist(me, hp) if me and hp else 1e9
    front = bool(pp and math.dist(pp[1:], o[1:]) < FRONT_PX and hd < o[0])
    guest.call(f'click {o[1]} {o[2] - 15}')
    time.sleep(0.5)
    tg, on_animal = target_is(guest, av, mine)
    now_ptr = npcs(guest).get(nid, {}).get('ptr', 0)
    if not on_animal:  # (diagnostics: which object the click took)
        print(f'    click took {tg or 0:08X}: the animal\'s proxy {mine:08X} now {now_ptr:08X}, stance {ability_mode(guest)}; '
              + ' '.join(f'#{k}={v["ptr"]:08X}' for k, v in npcs(guest).items() if tg and v['ptr'] + 0x5A8 == tg), flush=True)
    on_partner = bool(pup and tg == pup + 0x5A8)
    print(f'  [{label}] (b) animal at {o}, the host\'s creature on the guest\'s screen {pp} (in front: {front}); target '
          f'+0xE7C={tg or 0:08X}: animal {on_animal}, partner {on_partner}', flush=True)
    shot(guest, f'partner_pass_{label}')
    return front, on_animal, reached, on_partner, walk, min(gaps) if gaps else None, blocker


def clicks(rounds):
    """the clicks judged: the host's creature in front of the animal and the click took the animal or the host's
    creature (a click that took nothing or another animal along the ray is the game's pick, not the partner's)"""
    return [x for x in rounds if x[0] and (x[1] or x[3])]


def walks(rounds):
    """the walks judged: the host's creature right at the animal in the way, and the walk reached the animal or stopped
    at the host's creature (a press that never started the walk 6 m behind it, a creature held inside a nest mound's
    collision, BUG-0964, and a stop at another creature are not the partner's)"""
    return [x for x in rounds if x[4] and (x[2] or x[6] == 'partner')]


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
            if len(clicks(rounds)) >= 2 and len(walks(rounds)) >= 3 and len(rounds) >= a.rounds:
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
    front, way = clicks(rounds), walks(rounds)
    print(f'  rounds: {len(rounds)}, with the host in front of the animal: {sum(1 for x in rounds if x[0])}, its click judged '
          f'{len(front)}: on the animal {sum(1 for x in front if x[1])}, on the partner {sum(1 for x in front if x[3])}; '
          f'walks judged {len(way)}, reached {sum(1 for x in way if x[2])}; not judged: clicks on something else '
          f'{sum(1 for x in rounds if x[0] and x not in front)}, walks stopped by something else '
          f'{[x[6] for x in rounds if x[4] and x not in way]}', flush=True)
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
