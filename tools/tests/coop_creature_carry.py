#!/usr/bin/env python3
"""BUG-0458: a rock (or stick) a player picks up and throws in Creature is carried and lands in the other game too.

The fixture's rock (cRock, noun 0x2A8FB3F, one in the save). A player's creature stands 3 m from it and the player
clicks it (`coop.rules clickobj`: the game's click handler, cursor kind "pick up"); the fixture's creatures cannot pick
it up (cursor kind 0x1C: walk), so the game's own Creature_HoldObject is called as the carry node 0xD84120 calls it
(`coop.rules hold`). The creature carries it to flat ground by the nest (the rock lies on a slope) and lets go of it
(Creature_ReleaseObject, `coop.rules drop`: the game's physics throws it off the hand).
Checked in the OTHER game, on the game's own state (`coop.rules carry`: the hold slots, the rock's position):
  1. while carried: the carrier's puppet holds a rock in its hold slot there, and that rock is within 2 m of the
     puppet's position (carried, not lying where it was);
  2. after the release: the other game's rock lies within 1 m (two samples in a row, within 15 s) of where the thrower's rock came to rest.
First the host carries and throws, then the guest. SPORE_FIX_OFF=creature.carry_relay fails 1 and 2.
"""
import math
import re
import sys
import time

from coop_npc_corpse import Corpses, host_npcs, pos, proxies
from twoinst import Checks, start

ROCK = '2A8FB3F'
NEAR = 1.0


def click_rock(con, radius):
    out = con.call(f'coop.rules clickobj {ROCK} {radius}')['out']
    m = re.search(r'kind=(-?\d+) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
    return (int(m.group(1)), tuple(map(float, m.groups()[1:]))) if m else (None, None)


def carry(con):
    out = con.call('coop.rules carry')['out']
    holds = [(m.group(1), m.group(2), tuple(map(float, m.groups()[2:])))
             for m in re.finditer(r'^hold (\S+) socket=\S+ obj=\S+ noun=(\S+) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out, re.M)]
    return holds, out


def rock_pos(con):
    _, p = rock_where(con)
    return p


def rock_where(con):
    out = con.call(f'coop.rules findobj {ROCK} 3000')['out']
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', out)
    return out, (tuple(map(float, m.groups())) if m else None)


def one(ck, who, me, other, other_id, target_con, ids, home):
    """`me` carries the rock and throws it; `other` must show it"""
    rp = rock_pos(me)
    if not rp:
        ck(False, f'{who}: the rock of the save exists')
        return
    me.call('coop.npc gopos %.2f %.2f %.2f' % (rp[0] + 3, rp[1], rp[2]))
    time.sleep(2.0)
    k, _ = click_rock(me, 10)
    print(f'  {who}: clicked the rock (cursor kind {k})', flush=True)
    held, by_click = None, True
    for _ in range(8):
        time.sleep(0.5)
        h, _ = carry(me)
        held = [x for x in h if x[0] == 'own' and x[1].lstrip('0') == ROCK]
        if held:
            break
    if not held:  # the fixture's creatures may have no hands for it: the game's own hold (as the carry node calls it)
        by_click = False
        print(f'  {who}: no pickup by the click; the game\'s HoldObject: ' + me.call(f'coop.rules hold {ROCK} 10')['out'].strip(),
              flush=True)
        time.sleep(0.5)
        h, _ = carry(me)
        held = [x for x in h if x[0] == 'own' and x[1].lstrip('0') == ROCK]
    ck(bool(held), f'{who} (control): its own game carries the rock ({carry(me)[1].strip()})')
    if not held:
        return
    # carry it to flat ground by the nest (the fixture's rock lies on a slope: dropped there it rolls on for 30 s)
    me.call('coop.npc gopos %.2f %.2f %.2f' % home)
    time.sleep(2.5)
    h, out = carry(other)
    pup = [x for x in h if x[0] == f'puppet{other_id}' and x[1].lstrip('0') == ROCK]
    ppos = pos(me)
    d = math.dist(pup[0][2], ppos) if pup and ppos else None
    print(f'  {who} carries: the other game: {out.strip()}', flush=True)
    ck(bool(pup) and d is not None and d <= 2.5,
       f'{who}: the other game shows its creature holding the rock ({d and round(d, 2)} m from it)')
    # throw at an NPC near
    tgt = next(iter(ids), None)
    if by_click and tgt is not None:
        out = me.call(f'coop.rules click {tgt}')['out'].strip()
        print(f'  {who}: throw at #{tgt}: {out[:120]}', flush=True)
    else:  # let go (the game's ReleaseObject): its physics throws it off the creature's hand
        print(f'  {who}: lets go: ' + me.call('coop.rules drop')['out'].strip(), flush=True)
    t0, last, still = time.time(), None, 0
    while time.time() - t0 < 25:
        time.sleep(0.5)
        h, _ = carry(me)
        if any(x[0] == 'own' and x[1].lstrip('0') == ROCK for x in h):
            continue
        p = rock_pos(me)
        if last and p and math.dist(p, last) < 0.1:
            still += 1
            if still >= 3:
                break
        else:
            still, last = 0, p
    # then the two rocks: the same place within NEAR in two samples in a row within 15 s (the thrower's game sends each
    # new rest point; a rock pushed on in one game is put there again in the other)
    t1, ok_in_row, dd, mp, op = time.time(), 0, None, None, None
    while time.time() - t1 < 15 and ok_in_row < 2:
        time.sleep(1.0)
        mp, op = rock_pos(me), rock_pos(other)
        dd = math.dist(mp, op) if mp and op else None
        ok_in_row = ok_in_row + 1 if dd is not None and dd <= NEAR else 0
    print(f'  {who} threw: the rock at {mp} in its game, at {op} in the other ({dd and round(dd, 2)} m) after '
          f'{time.time() - t0:.0f} s', flush=True)
    print('    thrower: ' + carry(me)[1].strip().splitlines()[-1] + ' | other: ' + carry(other)[1].strip().splitlines()[-1],
          flush=True)
    ck(ok_in_row >= 2, f'{who}: the thrown rock lies at the same place in the other game ({dd and round(dd, 2)} m <= {NEAR})')


def main():
    host, guest = start('creature')
    ck = Checks()
    t0 = time.time()
    while time.time() - t0 < 30 and not any(s for s, _ in proxies(guest).values()):
        time.sleep(1)
    for c in (host, guest):
        c.call('coop.team hp 1000')
    print('  host rock: ' + rock_where(host)[0].strip() + ' | guest rock: ' + rock_where(guest)[0].strip(), flush=True)
    home = pos(host)  # the start: flat ground by the home nest
    rp = rock_pos(host)
    if rp:
        guest.call('coop.npc gopos %.2f %.2f %.2f' % (rp[0] - 3, rp[1], rp[2]))
    time.sleep(2.0)
    C = Corpses(host, guest, ck)
    near = [i for _, i, _, _ in C.candidates(near=rp, maxd=40)] if rp else []
    # the other player 22 m off while one carries (a copy is never put within 4 m of the own creature); the rock is
    # dropped 10 m from the start (a creature put onto a rock by a teleport, the next test's warm reset, takes 60-90
    # damage from it: "hit by none", out/lab/20261106-023016 #2-#4)
    guest.call('coop.npc gopos %.2f %.2f %.2f' % (home[0] - 22, home[1], home[2]))
    one(ck, 'the host', host, guest, 0, guest, near, (home[0] + 10, home[1], home[2]))
    time.sleep(2.0)
    host.call('coop.npc gopos %.2f %.2f %.2f' % (home[0] + 22, home[1], home[2]))
    one(ck, 'the guest', guest, host, 1, host, [i for i in near], (home[0] - 10, home[1], home[2]))
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    sys.exit(main())
