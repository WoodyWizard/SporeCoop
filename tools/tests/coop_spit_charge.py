#!/usr/bin/env python3
"""BUG-0641: a spit stops a charge across the two games (Discord report 2026-11-06, 0.7.8: "I can't counter a charge
with spit"; "an enemy creature can't stun my charge with their spit").

The game's rule (the ability hit 0xC20390, docs/re/creature.md#creature-spit-counter): a hit of a spit (ability type
0x20) stuns its target (CreatureStun) when the target runs a charge (type 0x1F) right now. The charge runs in its
owner's game only.

Creature fixture, both players' creatures and the host's NPCs with their own abilities (the game's PlayAbility, the
hits are the game's):
  A. a host NPC with a charge charges the guest's creature; while the NPC charges in the host's game, the guest's
     creature spits at it (its mirror). PASS: the host's NPC holds the stun stimulus 0x1000 within 2 s.
  B. the guest's creature charges an NPC; while it charges in the guest's game, a host NPC with a spit spits at the
     guest's puppet. PASS: the guest's own creature holds the stun stimulus 0x1000 within 2 s.
A side needs the creature with that ability; the fixture's creature has neither, so the guest plays its own creature
that has one (the co-op window's choice, put back at the end); a side that has none is reported, not passed.

  coop_spit_charge.py     (SPORE_FIX_OFF=creature.spit_counter: no stun in either game: FAILS)"""
import re
import time

import creations_state
from coop_creature_stun import avatar, stunned
from twoinst import Checks, start


def abil_types(con, who):
    return set(re.findall(r'type=0x([0-9A-F]+)', con.call(f'coop.social abil {who}')['out']))


def npcs(host, guest, hsp):
    mirrored = set()
    for _ in range(10):
        mirrored = set(re.findall(r'#(\d+) .*spawned', guest.call('coop.npc list')['out']))
        if mirrored:
            break
        time.sleep(1)
    out = []
    for line in host.call('coop.npc list')['out'].splitlines():
        m = re.match(r'\s*#(\d+) ([0-9A-F]+) species=(\S+) hp=([\d.]+) dist=[\d.]+ pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', line)
        if m and m.group(3) != hsp and 'DEAD' not in line and 'family' not in line and m.group(1) in mirrored:
            out.append((m.group(1), int(m.group(2), 16), tuple(map(float, m.groups()[4:])), abil_types(host, m.group(1))))
    return out


def avatar_key(con):
    m = re.search(r'key=0x([0-9A-F]{8})', con.call('coop.avatar')['out'])
    return m.group(1) if m else None


def own_with_abilities(guest):
    """the fixture's creature has neither a spit nor a charge: the guest plays its own creature (chosen in the co-op
    window, the game swaps the avatar) that has one; returns its ability types ({} without one)"""
    for key in creations_state.own_keys(guest):
        guest.call(f'coop.creations choose creature {key}')
        t0 = time.time()
        while time.time() - t0 < 40 and avatar_key(guest) != key:
            time.sleep(1)
        time.sleep(2)
        types = abil_types(guest, -1)
        print(f'  guest own creature {key} (playing {avatar_key(guest)}): abilities {sorted(types)}', flush=True)
        if avatar_key(guest) == key and ({'20', '1F'} & types):
            return types
    return set()


def charging(con, who):
    return 'charging=1' in con.call(f'coop.rules charging {who}')['out']


def main():
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team off')
    hsp = re.search(r'key=0x([0-9A-F]+)', host.call('coop.avatar')['out'])
    hsp = hsp.group(1).zfill(8) if hsp else ''
    gtypes = abil_types(guest, -1)
    st0 = creations_state.save(guest)
    if not ({'20', '1F'} & gtypes):
        gtypes = own_with_abilities(guest) or gtypes
    try:
        return run(host, guest, ck, hsp, gtypes)
    finally:
        creations_state.restore(guest, st0)


def run(host, guest, ck, hsp, gtypes):
    found = npcs(host, guest, hsp)
    print(f'  guest abilities {sorted(gtypes)}; NPCs: {[(n, sorted(t)) for n, _, _, t in found]}', flush=True)
    chargers = [n for n in found if '1F' in n[3]]
    spitters = [n for n in found if '20' in n[3]]
    gav = avatar(guest)
    done = 0

    # A. the guest's spit on a charging host NPC
    if '20' in gtypes and chargers:
        nid, nptr, (x, y, z), _ = chargers[0]
        ok = False
        for i in range(5):
            guest.call(f'coop.npc gopos {x + 8:.2f} {y:.2f} {z:.2f}')
            time.sleep(2)
            print('  ' + host.call(f'coop.rules charge {nid} 1')['out'].strip(), flush=True)
            t0 = time.time()
            while time.time() - t0 < 1.5 and not charging(host, nid):
                time.sleep(0.05)
            ch = charging(host, nid)
            print('  guest: ' + guest.call(f'coop.rules use 20 me {nid}')['out'].strip(), flush=True)
            t0 = time.time()
            while time.time() - t0 < 2.5 and not ok:
                ok = stunned(host, nptr)
                time.sleep(0.1)
            print(f'  A try {i + 1}: NPC charging when the spit went {ch}, the host\'s NPC stunned {ok}', flush=True)
            if ok:
                break
            time.sleep(4)  # the stimuli decay, the NPC is ready again
            m = re.search(rf'#{nid} \S+ species=\S+ hp=\S+ dist=\S+ pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)',
                          host.call('coop.npc list')['out'])
            if m:
                x, y, z = map(float, m.groups())
        print('  host ' + [l for l in host.call('coop.rules')['out'].splitlines() if 'spit' in l][0], flush=True)
        ck(ok, f"A: the guest's spit stops the host NPC #{nid}'s charge (the stun in the host's game)")
        done += 1
    else:
        print(f'  A not run: guest spit {"20" in gtypes}, NPCs with a charge {len(chargers)}', flush=True)

    # B. a host NPC's spit on the guest's charging creature
    targets = [n for n in found if spitters and n[0] != spitters[0][0]]
    if '1F' in gtypes and spitters and targets:
        sid, _, (sx, sy, sz), _ = spitters[0]
        tid = targets[0][0]
        ok = False
        time.sleep(4)
        for i in range(5):
            guest.call(f'coop.npc gopos {sx + 10:.2f} {sy:.2f} {sz:.2f}')
            time.sleep(2)
            print('  guest: ' + guest.call(f'coop.rules use 1F me {tid}')['out'].strip(), flush=True)
            t0 = time.time()
            while time.time() - t0 < 1.5 and not charging(guest, 'me'):
                time.sleep(0.05)
            ch = charging(guest, 'me')
            print('  host: ' + host.call(f'coop.rules use 20 {sid} p1')['out'].strip(), flush=True)
            t0 = time.time()
            while time.time() - t0 < 2.5 and not ok:
                ok = stunned(guest, gav)
                time.sleep(0.1)
            print(f'  B try {i + 1}: guest charging when the spit went {ch}, the guest\'s creature stunned {ok}', flush=True)
            if ok:
                break
            time.sleep(4)
        print('  guest ' + [l for l in guest.call('coop.rules')['out'].splitlines() if 'spit' in l][0], flush=True)
        ck(ok, f"B: the host NPC #{sid}'s spit stops the guest's charge (the stun in the guest's game)")
        done += 1
    else:
        print(f'  B not run: guest charge {"1F" in gtypes}, NPCs with a spit {len(spitters)}', flush=True)
    ck(done > 0, f'at least one side ran ({done})')
    for c in (host, guest):
        c.call('coop.team on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
