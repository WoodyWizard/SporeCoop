#!/usr/bin/env python3
"""Creature stage: an NPC of a species hostile to the players attacks a partner on sight, like the host's own
avatar (BUG-0168, docs/re/creature.md#creature-npc-aggression).

The host's game runs every NPC's AI; an NPC nearer a partner's creature ticks with that creature as the player
(docs/re/creature.md 14.2). Whether an NPC attacks the player on sight is the behaviour tree's (node 0x04A1A1E6:
the species relation 2 or 5, "red face"). No species of the creature fixture is hostile at the start, so the test
makes one hostile the way a player does: the game's own relationship event 7, which the avatar's egg node sends
when the player eats an egg of that species' nest (`coop.social event <species> 7`, ApplyEvent as the egg node
calls it). Nothing forces the attack itself.

1. Two games on the creature fixture (the guest plays the world's species, BUG-0345). An adult NPC of another
   species, far (> 45 m) from the host, mirrored on the guest.
2. Control: the guest stands next to it for 15 s: no damage (the species is not hostile yet).
3. The host's game eats that species' egg (event 7): the species' relation is 5 on the host and on the guest.
4. The guest stands next to the NPC (no input) for up to 40 s, the host far away: the guest's own health falls
   in the guest's game (coop.hp, the game's health), and the host's NPC has the guest's puppet as its target
   (+0xE7C = the puppet's combatant, +0x5A8; it names the puppet in the control too: the NPC watches it).

  coop_creature_aggression.py   (AGGR_EVENT=none: no egg event, the species stays as it was: must FAIL; AGGR_OWN=1:
                                 the guest plays its own creature; AGGR_PARTNERS=off: `coop.npc partners off` on the
                                 host: passes as well, lab 20261108-052641, the attack needs no AI swap)"""
import os
import re
import struct
import time

import creations_state
from coop_creature_kills import family, npcs, proxies, read
from twoinst import Checks, start


def own_hp(con):
    m = re.search(r'^own: hp=([\d.]+)/([\d.]+)', con.call('coop.hp')['out'], re.M)
    return float(m.group(1)) if m else None


def relation(con, sp):
    m = re.search(sp + r'/\S+ relation=(-?\d+)', con.call('coop.social species')['out'])
    return int(m.group(1)) if m else None


def puppet_ptr(host):
    m = re.search(r'puppet=([0-9A-F]{8})', host.call('coop.status')['out'])
    return int(m.group(1), 16) if m else 0


def stand_by(host, guest, nid, secs, pup):
    """the guest next to NPC nid, no input of its own: (hp lost in the guest's game, reads with the puppet as the NPC's
    target on the host, how many reads). A guest the NPC walked away from is put back next to it (test setup, the
    NPC's own choice to attack is what is checked)"""
    hp0 = own_hp(guest)
    guest.call(f'coop.social goto {nid}')
    targeted = reads = 0
    t0 = last_goto = time.time()
    lost = 0.0
    while time.time() - t0 < secs:
        n = npcs(host).get(nid)
        if not n or n[4]:
            break
        tc = struct.unpack('<I', read(host, n[0] + 0xE7C, 4))[0]
        reads += 1
        targeted += bool(pup) and tc == pup + 0x5A8
        hp = own_hp(guest)
        if hp0 is not None and hp is not None:
            lost = max(lost, hp0 - hp)
        px = proxies(guest).get(nid)
        if px and px[3] > 6 and time.time() - last_goto > 4:
            guest.call(f'coop.social goto {nid}')
            last_goto = time.time()
        if lost >= 1.0 and targeted >= 3:
            break
        time.sleep(0.5)
    return lost, targeted, reads


def main():
    host, guest = start('creature')
    ck = Checks()
    partners_off = os.environ.get('AGGR_PARTNERS') == 'off'
    if partners_off:
        print(' ', host.call('coop.npc partners off')['out'].strip(), '(the twin: must FAIL)', flush=True)
    st0 = creations_state.save(guest)
    try:
        if os.environ.get('AGGR_OWN') == '1':  # the guest plays its own creature (another species than the host's)
            own = creations_state.choose_own(guest)
            t0 = time.time()
            while own and time.time() - t0 < 30 and own not in guest.call('coop.avatar')['out']:
                time.sleep(1)
            time.sleep(3)
            print(f'  the guest plays its own creature {own}', flush=True)
        return run(host, guest, ck)
    finally:
        creations_state.restore(guest, st0)
        if partners_off:
            host.call('coop.npc partners on')


def run(host, guest, ck):
    fam = family(host)
    pup = puppet_ptr(host)
    prox = proxies(guest)
    cand = sorted((n[3], i, n[1]) for i, n in npcs(host).items()
                  if i in prox and not n[4] and n[2] > 0 and n[1] != fam and n[3] > 45)
    ck(bool(cand) and pup, f'an NPC of another species far from the host, mirrored on the guest ({len(cand)}), '
                           f'the host shows the guest\'s puppet ({pup:08X})')
    if not cand or not pup:
        ck.faults(host=host, guest=guest)
        return ck.result()
    _, nid, sp = cand[0]
    print(f'  NPC #{nid} species {sp}, {cand[0][0]:.0f} m from the host; relation {relation(host, sp)}', flush=True)

    # 2. control: not hostile yet
    lost, tg, rd = stand_by(host, guest, nid, 15, pup)
    print(f'  control: the guest lost {lost:g} hp, the NPC targeted the puppet in {tg} of {rd} reads', flush=True)
    ck(lost < 1.0, f'control: an NPC of a species that is not hostile leaves the guest alone ({lost:g} hp lost)')

    # 3. the host's game eats an egg of that species: hostile
    if os.environ.get('AGGR_EVENT') == 'none':
        print('  AGGR_EVENT=none: no egg event (the twin: must FAIL)', flush=True)
    else:
        print(' ', host.call(f'coop.social event {sp} 7')['out'].strip(), flush=True)
    r_h = relation(host, sp)
    r_g = None
    t0 = time.time()
    while time.time() - t0 < 5:
        r_g = relation(guest, sp)
        if r_g == r_h:
            break
        time.sleep(0.5)
    ck(r_h == 5 and r_g == 5, f'the species is hostile on both games (relation host {r_h}, guest {r_g})')

    # 4. the NPC attacks the partner by itself
    lost, tg, rd = stand_by(host, guest, nid, 40, pup)
    d = npcs(host).get(nid)
    print(f'  hostile: the guest lost {lost:g} hp, the NPC targeted the puppet in {tg} of {rd} reads '
          f'({d[3] if d else -1:.0f} m from the host)', flush=True)
    print(' ', guest.call('coop.npc')['out'].splitlines()[0][:200], flush=True)
    # +0xE7C names the puppet already in the control (the NPC watches the creature it ticks with as the player): the
    # attack itself is the guest's health
    ck(tg >= 3, f'the host\'s NPC #{nid} has the guest\'s puppet as its target ({tg} of {rd} reads)')
    ck(lost >= 1.0, f'the guest\'s creature is hurt in the guest\'s game ({lost:g} hp lost)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
