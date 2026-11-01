#!/usr/bin/env python3
"""A guest's creature ability bar holds its own creature's abilities after the swap (BUG-0358).

The game fills the creature HUD's ability bar (SporeCoop::AbilityBar_ptr: 5 x 5 slots, each the
index of one of the avatar's abilities) when the stage's HUD starts, from the avatar it has then:
the loaded world's species. The mod then gives a guest's avatar its own creature (SetSpecies),
which has other abilities, and the bar stayed as it was. A slot past the new creature's abilities
made the game's slot lookup read past a species' ability array, and every key press of the
creature stage walked the bar and faulted on that garbage (0xD4A397, lab 20261031-185843
campaign#2: 203 caught faults on one guest, whose own creature had fewer abilities than the
world's evolved species).

1. Two games on the creature fixture. The guest picks each of its own creatures once and reads
   its ability count (the game's avatar, vt 0xB0): the one with the most and the one with the
   fewest abilities are used below.
2. The guest plays the big one and its game builds the bar for it (`coop.creations bar rebuild`
   calls the game's own builder, as the HUD does at a stage start: the campaign's world species).
3. The guest takes the small one (the mod's swap, as after joining).
4. Checked in the guest's game memory: every slot of the bar is one of the avatar's abilities,
   and the bar is the one the game builds for this avatar.
5. Keys of the creature stage (W, 1, 2, E) are pressed on the guest: no caught fault in any game.

  coop_ability_bar.py   (SPORE_FIX_OFF=creations.ability_bar must FAIL)"""
import re
import struct
import time

from twoinst import Checks, start

KEYS = ('w', '1', '2', 'e', 'w', '1')


def eax(con, what):
    m = re.search(r'eax=0x([0-9A-Fa-f]+)', con.call('call ' + what)['out'])
    return int(m.group(1), 16) if m else None


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


def sym(con, name):
    m = re.match(r'0x([0-9A-Fa-f]+)', con.call('sym ' + name)['out'].strip())
    return int(m.group(1), 16) if m else None


def avatar(con):
    """the game's own avatar (cGameNounManager +0x54)"""
    nm = eax(con, 'Simulator::cGameNounManager::Get cdecl')
    return u32(con, nm + 0x54) if nm else None


def abilities(con):
    """the avatar's ability count: its vtable slot 0xB0, called by the game's convention"""
    av = avatar(con)
    vt = u32(con, av) if av else None
    fn = u32(con, vt + 0xB0) if vt else None
    return eax(con, f'{fn:#x} thiscall {av:#x}') if fn else None


def bar(con):
    """the ability bar as the game has it: {(row, col): ability index}"""
    p = u32(con, sym(con, 'SporeCoop::AbilityBar_ptr') or 0)
    if not p:
        return None
    t = mem(con, p, 0xD8)
    if len(t) < 0xD8:
        return None
    rows, cols = struct.unpack_from('<ii', t, 0xC8)
    out = {}
    for r in range(min(rows, 5)):
        for c in range(min(cols, 5)):
            idx = struct.unpack_from('<i', t, r * 0x28 + c * 8)[0]
            if idx != -1:
                out[(r, c)] = idx
    return out


def own_key(con):
    m = re.search(r'key=0x([0-9A-F]{8})', con.call('coop.avatar')['out'])
    return m.group(1) if m else None


def play(con, inst, timeout=40):
    """the guest takes its own creature `inst` (the mod's swap) and waits until the game has it"""
    con.call(f'coop.creations choose creature {inst}')
    t0 = time.time()
    while time.time() - t0 < timeout:
        if own_key(con) == inst:
            time.sleep(2)  # the bar fix acts on the next frames
            return True
        time.sleep(0.5)
    return False


def faults(con):
    m = re.search(r'total faults: (\d+)', con.call('exc')['out'])
    return int(m.group(1)) if m else -1


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(5)
    own = re.findall(r'^creature ([0-9A-F]{8}) (.*)$', guest.call('coop.creations list')['out'], re.M)
    counts = {}
    for inst, name in own:
        if play(guest, inst):
            counts[inst] = abilities(guest)
            print(f'  own creature {inst} {name!r}: {counts[inst]} abilities', flush=True)
    ck(len(counts) >= 2, f'1. the guest has two own creatures to play ({len(counts)})')
    if len(counts) < 2:
        return ck.result()
    big = max(counts, key=lambda k: counts[k])
    small = min(counts, key=lambda k: counts[k])
    play(guest, big)
    guest.call('coop.creations bar rebuild')
    time.sleep(1)
    b = bar(guest) or {}
    top = max(b.values(), default=-1)
    print(f'  bar for {big} ({counts[big]} abilities): {len(b)} slots, highest index {top}', flush=True)
    ck(top >= counts[small], f'2. the big creature\'s bar has a slot past the small one\'s {counts[small]} abilities ({top})')
    f0 = faults(guest)
    ck(play(guest, small), f'3. the guest plays {small} ({counts[small]} abilities)')
    n = abilities(guest)
    b = bar(guest) or {}
    past = {k: v for k, v in b.items() if not 0 <= v < (n or 0)}
    print(f'  {guest.call("coop.creations bar")["out"].strip()}', flush=True)
    for (r, c), idx in sorted(past.items()):  # what the game's lookup reads for such a slot (info)
        print(f'  slot {r}.{c} = {idx}: the game reads ability {eax(guest, f"SporeCoop::AbilityBar_Ability cdecl {idx}")}',
              flush=True)
    ck(b and not past, f'4. every slot of the guest\'s ability bar is one of its {n} abilities ({len(past)} of {len(b)} past them)')
    guest.call('coop.creations bar rebuild')
    time.sleep(1)
    ck(bar(guest) == b, '4. the guest\'s bar is the one its game builds for its avatar')
    for k in KEYS:
        guest.call(f'key {k} tap')
        time.sleep(0.4)
    time.sleep(1)
    print(f'  guest faults {f0} -> {faults(guest)} after {len(KEYS)} key presses', flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
