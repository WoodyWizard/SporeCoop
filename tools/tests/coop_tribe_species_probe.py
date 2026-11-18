#!/usr/bin/env python3
"""Probe (plan/0.7.16, prints only): can one player tribe hold members of two species?

docs/plan-0.7.16.md item 1 (own species per player in Tribe), docs/re/tribe.md#tribe-two-species. The game gives a new
member the tribe's species slot (cTribe +0x18CC, 0xC8E9B0(0)) in the citizen's init (vt 0xE8 0xC27330); a member's own
species is cCreatureBase +0xB20 / +0xB28 and its model is built from its own profile (0xC21D50). This probe sets the
species of the guest-owned members only (cCreatureAnimal::SetSpecies, as `coop.tribe setspecies` does for all) and
looks at what the game does with that:

1. Two games on the tribe fixture. The host's player tribe members, their owners and species (`coop.tribe who`).
2. Host: every member becomes the host's creation 290229B9 ("TigerHorse", `coop.tribe setspecies`), then the host's
   own members go back to the fixture's species (SetSpecies with the profile they had) and the tribe's first species
   key +0x1920 is written back: only the guest's members are of the other species.
3. Host, 40 s later: still two species (nothing of the game puts them back), the tribe's species slot unchanged,
   IsPlayerSpecies / citizen vt 0xD0 / the ability count of a member of each species.
4. The guest's copies at that time (the species is a field of each game: not sent).
5. The guest's game starts again and joins: the host's world (a save of it) goes to it. The guest's members of the
   loaded world: which species (a save holds the member's own key, or the tribe's).

  SPORE_LANE=4 python3 tools/tests/coop_tribe_species_probe.py
"""
import re
import struct
import time

import lane
import twoinst
from coop_creature_desync import kill_game
from twoinst import Checks, Console, shot, start, wait_up, watch

OTHER = 0x290229B9  # the host's own creation (not in the guest's My Creations), as tribe_portraits


def mem(con, a, n):
    b = bytearray()
    for line in con.call(f'mem.read {a:#x} {n}')['out'].splitlines():
        p = line.split()
        if len(p) > 1 and re.fullmatch(r'[0-9A-F]{8}', p[0]):
            b += bytes(int(x, 16) for x in p[1:17] if re.fullmatch(r'[0-9A-F]{2}', x))
    return bytes(b[:n])


def u32(con, a):
    b = mem(con, a, 4)
    return struct.unpack('<I', b)[0] if len(b) == 4 else None


def eax(con, cmd):
    m = re.search(r'eax=0x([0-9A-Fa-f]+)', con.call(cmd)['out'])
    return int(m.group(1), 16) if m else None


def who(con, p):
    out = con.call(f'coop.tribe who {p:08X}')['out']
    m = re.search(r'tribe=(\w+) .*player tribe=(\w+); mod owner=(-?\d+) id=(\d+); age=(\d+) dead=(\d+) species=(\w+)', out)
    if not m:
        return None
    return dict(tribe=int(m.group(1), 16), player=int(m.group(2), 16), owner=int(m.group(3)), id=int(m.group(4)),
                age=int(m.group(5)), dead=int(m.group(6)), species=int(m.group(7), 16))


def player_members(con):
    """[(ptr, who dict)] of the player tribe's members in this game"""
    out = con.call('coop.tribe airprobe -10000')['out']
    res = []
    for m in re.finditer(r'^M #\d+ (\w+) owner=', out, re.M):
        p = int(m.group(1), 16)
        w = who(con, p)
        if w and w['tribe'] == w['player']:
            res.append((p, w))
    return res


def census(tag, con):
    ms = player_members(con)
    by = {}
    for p, w in ms:
        by.setdefault(w['species'], []).append(w['owner'])
    print(f'  {tag}: {len(ms)} members; species -> owners: ' +
          ', '.join(f'{k:08X}: {sorted(v)}' for k, v in by.items()), flush=True)
    return ms


def main():
    host, guest = start('tribe')
    ck = Checks()
    twoinst.game_runs(host)
    ms = census('host before', host)
    ck(len(ms) >= 2, 'the host has player tribe members')
    if len(ms) < 2:
        return ck.result()
    tribe = ms[0][1]['tribe']
    key1920 = u32(host, tribe + 0x1920)
    slot = u32(host, tribe + 0x18CC)
    slot_key = u32(host, slot + 0x504) if slot else None
    orig = {p: u32(host, p + 0xB20) for p, _ in ms}
    print(f'  host tribe {tribe:08X}: +0x1920 key {key1920 or 0:08X}, species slot +0x18CC {slot or 0:08X} '
          f'(+0x504 {slot_key or 0:08X})', flush=True)
    print('  host:', host.call(f'coop.tribe setspecies {OTHER:08X}')['out'].strip(), flush=True)
    other_prof = u32(host, ms[0][0] + 0xB20)
    n_back = 0
    for p, w in ms:
        if w['owner'] != 1 and orig.get(p):  # the host's own (0) and unowned members go back
            age = u32(host, p + 0xB34) or 0
            host.call(f'call Simulator::cCreatureAnimal::SetSpecies thiscall {p:#x} {orig[p]:#x} {age} 1')
            n_back += 1
    if key1920 is not None:
        host.call(f'mem.write {tribe + 0x1920:#x} {struct.pack("<I", key1920).hex()}')
    print(f'  host: {n_back} members back to their own species, the other species profile {other_prof or 0:08X}', flush=True)
    time.sleep(2)
    ms = census('host mixed', host)
    shot(host, 'tribe_species_probe_host_mixed.png')
    time.sleep(40)
    ms = census('host +40 s', host)
    kinds = {w['species'] for _, w in ms}
    ck(len(kinds) >= 2, f'the host\'s player tribe holds two species 40 s later ({len(kinds)})')
    slot2 = u32(host, tribe + 0x18CC)
    print(f'  host tribe species slot now {slot2 or 0:08X} (+0x504 {u32(host, slot2 + 0x504) if slot2 else 0:08X}), '
          f'+0x1920 {u32(host, tribe + 0x1920) or 0:08X}', flush=True)
    for p, w in ms[:12]:
        ps = eax(host, f'call SporeCoop::cCreatureBase_IsPlayerSpecies thiscall {p:#x}')
        vd0 = eax(host, f'call 0x00C228B0 thiscall {p:#x}')
        ab = eax(host, f'call SporeCoop::Citizen_AbilityCount thiscall {p:#x}')
        gen = u32(host, p + 0xB24)
        print(f'  host member {p:08X} owner={w["owner"]} species={w["species"]:08X} IsPlayerSpecies={ps and ps & 0xFF} '
              f'vt0xD0={vd0 and vd0 & 0xFF} abilities={ab} +0xB24={gen}', flush=True)
    shot(host, 'tribe_species_probe_host_40s.png')
    twoinst.keyframe('mixed', 'the host\'s village: the guest\'s members drawn as another creature than the host\'s')
    census('guest live (its copies)', guest)
    shot(guest, 'tribe_species_probe_guest_live.png')
    # the host's world goes to a new guest process (a save of the host's world)
    for m in twoinst._monitors:
        m.stop = True
    kill_game(lane.GUEST)
    time.sleep(3)
    lane.run_guest()
    if not wait_up(lane.GUEST, 90):
        lane.run_guest()
        assert wait_up(lane.GUEST), 'the guest did not start'
    guest = Console(lane.GUEST)
    t0 = time.time()
    while time.time() - t0 < 120 and 'GalaxyGameEntry' not in guest.call('game.mode')['out']:
        time.sleep(1)
    guest.call('wait.frames 200 60000')
    guest.call(lane.JOIN)
    ck(twoinst.wait_guest_in(host, guest, 180), 'the new guest is in the host\'s world')
    watch(lane.HOST, lane.GUEST)
    time.sleep(10)
    print('  guest creations png:', guest.call(f'coop.creations png {OTHER:08X}')['out'].strip()[:160], flush=True)
    gms = census('guest after the world share', guest)
    gk = {w['species'] for _, w in gms}
    print(f'  RESULT-PROBE the loaded world holds {len(gk)} species in the player tribe '
          f'({", ".join(f"{k:08X}" for k in gk)})', flush=True)
    census('host after the share', host)
    shot(guest, 'tribe_species_probe_guest_loaded.png')
    shot(host, 'tribe_species_probe_host_end.png')
    twoinst.keyframe('loaded', 'the guest\'s game after loading the host\'s world: two kinds of creatures in the village')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
