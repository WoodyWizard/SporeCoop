#!/usr/bin/env python3
"""Creature stage: a kill is a death on every machine, and NPCs see the partner like the player
(player report 2026-10-25, docs/re/creature.md 14).

1. "Enemies still seen as alive even when dead for quest": the guest hunts an NPC with real input
   (combat stance, a click on it, key 1) until the host's NPC dies. The guest's mirror must die in
   the guest's game too (the game's death flag +0xB5E, `coop.npc creatures`): a mirror has no AI and
   the death is the behaviour tree's, so it stood with 0 health, alive for the game. The kill counts
   for the hunt card on both (the species table's kill count).
2. "Aggressive enemies didn't target my friend": the creature AI notices only the avatar
   (NPCTickAI). The guest stands next to an NPC far from the host; the NPC's AI must get the
   "noticed the player" stimulus 0x4000000 with the guest's puppet as the sender (read from the
   NPC's stimulus slots on the host, AI block +0xB4C, +8: 8 slots of 0x38 bytes, sender at +0x2C)."""
import re
import struct
import time

from twoinst import Checks, shot, start

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


def hunt(host, guest, nid):
    """the guest walks up to NPC nid and bites it (combat stance, click, key 1) until it is dead"""
    guest.call(f'coop.social goto {nid}')
    time.sleep(1)
    guest.call(f'click {W // 2 + 43} {H - 82}')  # the combat stance
    time.sleep(0.3)
    keys = 0
    for _ in range(45):
        n = npcs(host).get(nid)
        if not n or n[4]:
            break
        for line in guest.call('coop.targets all')['out'].splitlines():
            f = line.split()
            if len(f) == 5 and f[0] == 'obj' and f[1] == 'n' + nid and 20 < int(f[3]) < W - 20 and 90 < int(f[4]) < H - 170:
                guest.call(f'click {f[3]} {f[4]}')
        time.sleep(0.3)
        guest.call('key 1')
        keys += 1
        time.sleep(0.9)
    return keys


def main():
    host, guest = start()
    ck = Checks()
    fam = family(host)

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
        n = npcs(host).get(nid)
        ck(n is not None and n[4], f'the guest killed host NPC #{nid} ({sp}) with real input ({keys} bites)')
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
