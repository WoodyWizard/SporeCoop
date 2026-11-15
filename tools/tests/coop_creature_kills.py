#!/usr/bin/env python3
"""Creature stage: a kill is a death on every machine, and NPCs see the partner like the player
(player report 2026-10-25, docs/re/creature.md 14).

1. "Enemies still seen as alive even when dead for quest": the guest hunts an NPC with real input
   (combat stance, a click on it, key 1) until the host's NPC dies. The guest's mirror must die in
   the guest's game too (the game's death flag +0xB5E, `coop.npc creatures`): a mirror has no AI and
   the death is the behaviour tree's, so it stood with 0 health, alive for the game. The kill counts
   for the hunt card on both (the species table's kill count). The hunt is a player's (BUG-0690): one click on the
   animal, then the combat key (the game's walk-and-use) until the animal, which runs off first, is dead;
   KILLS_OLD_HUNT=1: the old loop (a click before every key, 45 keys): fails when the flight is long.
   The stance is the game's (BUG-0944): the hunt waits until the guest's game time runs (the stage entry's pause holds
   a black screen for seconds on some lanes; a click sent into it is lost and the whole hunt was played in the social
   stance, 0 of 6 on lane 4) and reads the stance back (GetAbilityMode). KILLS_BLIND_STANCE=1: the one unchecked click.
2. "Aggressive enemies didn't target my friend": the creature AI notices only the avatar
   (NPCTickAI). The guest stands next to an NPC far from the host; the NPC's AI must get the
   "noticed the player" stimulus 0x4000000 with the guest's puppet as the sender (read from the
   NPC's stimulus slots on the host, AI block +0xB4C, +8: 8 slots of 0x38 bytes, sender at +0x2C)."""
import os
import re
import struct
import time

import creations_state
from twoinst import Checks, ability_mode, game_runs, set_stance, shot, start

W, H = 1440, 1080


def npcs(con):
    """host: {id: (ptr, species, hp, dist, dead)}"""
    d = {}
    for m in re.finditer(r'#(\d+) ([0-9A-F]{8}) species=([0-9A-F]+) hp=([\d.]+) dist=([\d.]+)( DEAD)?',
                         con.call('coop.npc list')['out']):
        d[m.group(1)] = (int(m.group(2), 16), m.group(3), float(m.group(4)), float(m.group(5)), bool(m.group(6)))
    return d


def proxies(con):
    """guest: {id: (ptr, species, hp, dist)} of spawned mirrors"""
    d = {}
    for m in re.finditer(r'#(\d+) species=([0-9A-F]+) spawned hp=([\d.]+) dist=([\d.]+).* ptr=([0-9A-F]+)',
                         con.call('coop.npc list')['out']):
        d[m.group(1)] = (int(m.group(5), 16), m.group(2), float(m.group(3)), float(m.group(4)))
    return d


def killed(con, species):
    m = re.search(species + r'/\S+ relation=-?\d+ x=\d+ impressed=\d+ killed=(\d+)', con.call('coop.social species')['out'])
    return int(m.group(1)) if m else 0


def game_dead(con, ptr, species):
    """the game's own death flag of a creature (not our proxy bookkeeping)"""
    for line in con.call(f'coop.npc creatures {species}')['out'].splitlines():
        if f'creature {ptr:08X} ' in line:
            return ' DEAD' in line
    return None


def family(con):
    m = re.search(r'key=0x([0-9A-F]+)', con.call('coop.avatar')['out'])
    return m.group(1).zfill(8) if m else ''


def read(con, addr, n):
    b = bytearray()
    for line in con.call(f'mem.read 0x{addr:X} {n}')['out'].splitlines():
        b += bytes(int(x, 16) for x in line.split()[1:] if len(x) == 2)
    return bytes(b[:n])


def stimuli(con, npc_ptr):
    """the NPC's behaviour-tree stimuli: [(mask lo, mask hi, sender)]"""
    ai = struct.unpack('<I', read(con, npc_ptr + 0xB4C, 4))[0]
    b = read(con, ai + 8, 8 * 0x38) if ai else b''
    out = []
    for i in range(len(b) // 0x38):
        lo, hi = struct.unpack_from('<II', b, i * 0x38)
        if lo or hi:
            out.append((lo, hi, struct.unpack_from('<I', b, i * 0x38 + 0x2C)[0]))
    return out


def reach(con):
    """the game's target of the avatar and its combat keys' reach (coop.abilities reach, BUG-0618):
    {'target': ptr | None, 'dead', 'gap', 'keys': {key: (reach, status)}}"""
    out = con.call('coop.abilities reach')['out']
    m = re.search(r'target (\S+) dead=(\d) gap=([-\d.]+)', out)
    res = {'target': m.group(1) if m and m.group(1) not in ('none', '-', '0', '00000000') else None,
           'dead': bool(m and m.group(2) == '1'), 'gap': float(m.group(3)) if m else 1e9, 'keys': {}}
    abs_ = {int(a.group(1)): (float(a.group(2)), int(a.group(3))) for a in
            re.finditer(r'ab (\d+) type=\S+ row=\S+ key=\S+ reach=([-\d.]+) min=\S+ status=(-?\d+)', out)}
    b = re.search(r'bar 1:((?: -?\d+)+)', out)
    for col, i in enumerate(b.group(1).split() if b else []):
        if int(i) in abs_:
            res['keys'][str(col + 1)] = abs_[int(i)]
    return res


def hunt(host, guest, nid, secs=90):
    """the guest walks up to NPC nid and bites it with real input until it is dead, as a player does it
    (docs/re/creature.md#creature-attack-reach): the combat stance, ONE click on the animal (it only picks the target),
    then the combat key: out of reach it makes the game walk the creature to the target and use it, in reach it bites
    when the ability is ready; another click only when the game lost the target. BUG-0690: the bitten-at animal (065CB8AB,
    a herbivore) runs off before the first bite and stops 20-160 m on (70 / 86 / 129 / 131 / 200+ m in the runs of
    2026-11-08): the guest follows it and kills it where it stops. The old loop's 45 keys (~55 s) ended inside the long
    flights (the mirror at 2-6 hp, out/lab/20261108-050538, -051223 #1): the hunt lasts until the NPC is dead, at most
    secs. Ends when the host's NPC is dead or after secs."""
    blind = os.environ.get('KILLS_BLIND_STANCE') == '1'  # the twin (BUG-0944): one click on the stance button, unchecked
    waited = None if blind else game_runs(guest)  # the stage entry's pause: input sent into it is lost
    guest.call(f'coop.social goto {nid}')
    time.sleep(1)
    if blind:
        guest.call(f'click {W // 2 + 43} {H - 82}')  # the combat stance
        time.sleep(0.3)
    else:
        set_stance(guest, 'combat', W, H)
    ran = 'not waited for' if waited is None else f'ran after {waited:.1f} s'
    print(f'  the guest before the hunt: game time {ran}, stance {ability_mode(guest)} (1 = combat)', flush=True)
    keys, clicks, trail = 0, 0, []
    walk_at, walk_gap = 0.0, None
    old = os.environ.get('KILLS_OLD_HUNT') == '1'  # the twin: the old loop (a click before every key, 45 keys)
    t0, last = time.time(), 0.0
    while time.time() - t0 < secs:
        n = npcs(host).get(nid)
        if n and time.time() - last >= 1.5:
            trail.append(f'{n[2]:g}@{n[3]:.0f}m')
            last = time.time()
        if not n or n[4]:
            break
        if old:
            if keys >= 45:
                break
            for line in guest.call('coop.targets all')['out'].splitlines():
                f = line.split()
                if len(f) == 5 and f[0] == 'obj' and f[1] == 'n' + nid and 20 < int(f[3]) < W - 20 and 90 < int(f[4]) < H - 170:
                    guest.call(f'click {f[3]} {f[4]}')
                    clicks += 1
            time.sleep(0.3)
            guest.call('key 1')
            keys += 1
            time.sleep(0.9)
            continue
        r = reach(guest)
        px = proxies(guest).get(nid)
        mine = px is not None and r['target'] is not None and int(r['target'], 16) == px[0]
        if not mine or r['dead']:
            if not blind and ability_mode(guest) != 1:
                set_stance(guest, 'combat', W, H)  # the game left the stance (the bar is built again by a creature swap)
            for line in guest.call('coop.targets all')['out'].splitlines():
                f = line.split()
                if len(f) == 5 and f[0] == 'obj' and f[1] == 'n' + nid and 20 < int(f[3]) < W - 20 and 90 < int(f[4]) < H - 170:
                    guest.call(f'click {f[3]} {f[4]}')
                    clicks += 1
                    break
            else:
                guest.call(f'coop.social goto {nid}')  # out of sight: back next to it (a player would look for it)
            walk_gap = None
            time.sleep(0.4)
            continue
        k = min(r['keys'], key=lambda x: r['keys'][x][0]) if r['keys'] else '1'
        rch, st = r['keys'].get(k, (1.0, 0))
        if r['gap'] <= rch:
            if st == 0:
                guest.call('key ' + k)
                keys += 1
            time.sleep(0.35)
        elif walk_gap is None or (time.time() - walk_at > 2.5 and r['gap'] > walk_gap - 1.0):
            guest.call('key ' + k)  # not walking to it (yet, or it stopped): the game's walk-and-use
            keys += 1
            walk_at, walk_gap = time.time(), r['gap']
            time.sleep(0.4)
        else:
            time.sleep(0.3)
    print(f'  hunt of #{nid}: {keys} keys, {clicks} clicks in {time.time() - t0:.0f} s; host hp@distance from the '
          f'host: {" ".join(trail)}', flush=True)
    return keys


def main():
    host, guest = start()
    st0 = creations_state.save(guest)
    try:
        return run(host, guest)
    finally:
        creations_state.restore(guest, st0)  # the profile as it was (BUG-0345)


def run(host, guest):
    ck = Checks()
    fam = family(host)
    # KILLS_OWN=1: the guest hunts with its own creature (chosen; since BUG-0345 a guest plays the world's species in a
    # world it never played its own in, the default here)
    if os.environ.get('KILLS_OWN') == '1':
        own = creations_state.choose_own(guest)
        t0 = time.time()
        while own and time.time() - t0 < 30 and own not in guest.call('coop.avatar')['out']:
            time.sleep(1)
        print(f'  the guest plays its own creature {own}', flush=True)
        time.sleep(3)

    # ---- 1. a kill by the guest
    prox = proxies(guest)
    cand = sorted((n[3], i) for i, n in npcs(host).items()
                  if i in prox and not n[4] and n[2] > 0 and n[1] != fam)
    ck(bool(cand), f'host NPCs mirrored on the guest ({len(cand)})')
    nid = cand[0][1] if cand else None
    if nid:
        sp = npcs(host)[nid][1]
        k0h, k0g = killed(host, sp), killed(guest, sp)
        keys = hunt(host, guest, nid)
        ck(ability_mode(guest) == 1, f'the guest hunted in the combat stance (the game\'s ability mode {ability_mode(guest)})')
        n = npcs(host).get(nid)
        ck(n is not None and n[4], f'the guest killed host NPC #{nid} ({sp}) with real input ({keys} combat keys)')
        time.sleep(2)
        px = proxies(guest).get(nid)
        dead = game_dead(guest, px[0], sp) if px else None
        ck(px is not None and px[2] == 0, f'the guest\'s mirror has 0 health ({px[2] if px else None})')
        ck(dead is True, f'the guest\'s mirror is dead in the guest\'s game, not standing at 0 health ({dead})')
        print(' ', guest.call('coop.npc')['out'].splitlines()[-1])
        time.sleep(1.5)  # the host's table reaches the guest on change
        k1h, k1g = killed(host, sp), killed(guest, sp)
        ck(k1h == k0h + 1, f'the kill counts for the hunt on the host ({k0h} -> {k1h})')
        ck(k1g == k1h, f'and on the guest ({k0g} -> {k1g})')
        shot(guest, 'kills_guest_corpse.png')

    # ---- 2. an NPC notices the partner like the player
    pup = re.search(r'puppet=([0-9A-F]{8})', host.call('coop.status')['out'])
    pup = int(pup.group(1), 16) if pup else 0
    prox = proxies(guest)
    far = sorted((-n[3], i) for i, n in npcs(host).items()
                 if i in prox and not n[4] and n[1] != fam and n[3] > 45 and i != nid)
    ck(bool(far) and pup, f'an NPC far from the host to stand next to ({len(far)})')
    if far and pup:
        fid = far[-1][1]  # the nearest of those farther than 45 m
        ptr = npcs(host)[fid][0]
        guest.call(f'coop.social goto {fid}')
        seen, t0 = 0, time.time()
        while time.time() - t0 < 10:
            seen += any(lo & 0x4000000 and snd == pup for lo, hi, snd in stimuli(host, ptr))
            time.sleep(0.25)
        d = npcs(host).get(fid)
        ck(seen > 0, f'host NPC #{fid} ({d[3] if d else -1:.0f} m from the host) noticed the partner as the '
                     f'player: "noticed" stimulus from the puppet in {seen} of ~40 reads')
        print(' ', host.call('coop.npc')['out'].splitlines()[-1])
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
