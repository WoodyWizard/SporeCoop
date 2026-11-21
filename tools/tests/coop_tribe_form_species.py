#!/usr/bin/env python3
"""Tribe: the NPC tribe controller does not read its next species past its list (BUG-1233), and when the list is used
up the next villages take its last species, as many as the planet allows (BUG-1238).

NpcTribeController_FormTribe 0xCC6440 takes the next tribe's species from the controller's list (+0xD0..+0xD4) at
index min(size, tribes - 1): with as many tribes as species it reads the dword after the list's end and the bake
check faults at 0x406EFF (a final check's 4-player campaign: one fault a second from the game's own update, then a
host crash). The game builds that list shorter than its property list allows now and then: the campaign's own
planet (its Tribe save, lab 20261118-002106) had 4 species for 5 NPC villages, so its fifth village never came and the
goal bar stayed at 4 of 5 (BUG-1238). The tribe fixture has 5 of 5.

1. Two games on the tribe fixture (cold): the host's controller has 2 tribes, a property list of 10 (5 NPC villages)
   and a list of 5 species.
2. The list is made as short as the game made it on the campaign's planet, for the fixture's tribes: 2 species, the
   dword after them unreadable as there (0xFFFFFFFF: what the campaign's save had past its list, the same fault), and
   no room after them (the villages after the list need the game's own vector insert, which moves the list).
3. One `coop.tribe form` forms the third tribe (species 1 of 2, in the list; tribe.form_pace off). With 3-4 tribes
   the game's own update calls FormTribe every second by itself (5 needs three goals: the test command then).
   BUG-1392: right after that form the test runs the update's 1 s timer out (0x157E940 = 0): the game's next call
   comes within the second the form's "in the list" answer (made with 2 tribes) is kept, as it did by chance in lab
   20261121-003202 (0.998 s after the form: one fault at 0x406EFF).
4. Within 30 s: no fault in the host's game; the host's game has the 6 tribes its property list allows (the villages
   after the list with its last species); the guest's game has its copy of each (same species), no fault there.
   The test command once more at 6 tribes: no fault, nothing formed (the game's own limit).
5. The list is put back as it was.

SPORE_FIX_OFF=tribe.form_species must FAIL (a fault a second at 0x406EFF in FormTribe, caught by the game's handler,
or a crash). SPORE_FIX_OFF=tribe.form_last_species must FAIL (no fault, but the host stays at 3 tribes).
SPORE_FIX_OFF=tribe.form_species_fresh must FAIL (BUG-1392: the fault at 0x406EFF of the call right after the form).

  SPORE_LANE=2 python3 tools/tests/coop_tribe_form_species.py
"""
import re
import time

from twoinst import Checks, start


def species(con):
    out = con.call('coop.tribe form_species')['out'].strip()
    m = re.search(r'tribes (\d+), property list (\d+) .*species list (\d+)', out)
    return out, (tuple(int(v) for v in m.groups()) if m else None)


def dword(con, addr):
    m = re.match(r'[0-9A-F]{8}\s+((?: [0-9A-F]{2}){4})', con.call(f'mem.read 0x{addr:X} 4')['out'].strip())
    return int(''.join(reversed(m.group(1).split())), 16) if m else None


def put(con, addr, value):
    con.call(f'mem.write 0x{addr:X} {value.to_bytes(4, "little").hex()}')


def faults(con):
    m = re.search(r'total faults: (\d+)', (con.call('exc')['out'].splitlines() or [''])[0])
    return int(m.group(1)) if m else -1


def tribes(con):
    return len(re.findall(r'^tribe \d+', con.call('coop.tribe')['out'], re.M))


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(8)
    out, st = species(host)
    print(f'  host: {out}', flush=True)
    if not ck(st and st[0] == 2 and st[2] >= 3, 'the host\'s controller: 2 tribes and a species list of 3 or more'):
        return ck.result()
    m = re.search(r'-> 0x([0-9A-F]{8})', host.call('mem.ptr 0x169B054 0')['out'])
    ctrl = int(m.group(1), 16) if m else 0
    lb, le = (dword(host, ctrl + 0xD0), dword(host, ctrl + 0xD4)) if ctrl else (None, None)
    if not ck(lb and le and (le - lb) // 4 == st[2], f'the species list read (controller {ctrl:#x}, '
              f'list {lb and hex(lb)}..{le and hex(le)})'):
        return ck.result()
    old = [dword(host, lb + 4 * i) for i in range(st[2])]  # the whole list (the villages after it may write past 2)
    # (2) as short as the game built it on the campaign's planet, for the fixture's 2 tribes; past it what was there
    put(host, lb + 8, 0xFFFFFFFF)
    put(host, ctrl + 0xD4, lb + 8)
    # no room after the 2 either (capacity = size, as 4 pushes leave a list of 4: 1, 2, 4): the villages after the list
    # need the game's own insert, which moves the list to new storage (and frees this one)
    cap = dword(host, ctrl + 0xD8)
    put(host, ctrl + 0xD8, lb + 8)
    out, st = species(host)
    print(f'  host: planted: {out}', flush=True)
    ck(st and st[2] == 2, 'the host\'s species list holds 2 species now')
    keys = re.search(r'keys=(\S+)', out)
    keys = keys.group(1).split(',') if keys else []
    host.call('coop.fix tribe.form_pace off')
    f0, g0 = faults(host), faults(guest)
    out = host.call('coop.tribe form')['out'].strip()
    print(f'  host: {out}', flush=True)
    ck(out.startswith('tribes 2 -> 3'), 'the host\'s controller formed the third tribe (species 1 of the list)')
    # BUG-1392: the update's next FormTribe within the second the form's answer is kept (the timer FormTribe set to
    # 1.0 runs out now: the game calls it in its next frame)
    put(host, 0x157E940, 0)
    # (4) the game's own update: with 3 and 4 tribes it calls FormTribe every second; at 5 (three goals needed) the
    # test command asks, as the campaign does when no village is left
    t0, seen, asked = time.time(), [], 0
    while time.time() - t0 < 30:
        time.sleep(2)
        seen.append((faults(host) - f0, tribes(host)))
        if seen[-1][1] >= 6 or seen[-1][0] > 0:
            break
        if seen[-1][1] == 5 and len(seen) > 2 and seen[-2][1] == 5 and asked < 3:
            asked += 1
            print('  host: ' + host.call('coop.tribe form')['out'].strip(), flush=True)
    print(f'  host: (new faults, tribes) every 2 s: {seen}', flush=True)
    out, st = species(host)
    print(f'  host: {out}', flush=True)
    ck(all(f == 0 for f, _ in seen), f'no fault in the host\'s game while its update calls FormTribe '
       f'({seen[-1][0] if seen else "?"} new)')
    n = seen[-1][1] if seen else 0
    ck(n == 6, f'the host\'s game formed the villages its property list allows: 6 tribes ({[t for _, t in seen]})')
    hs = re.findall(r'^tribe \d+ at .* species=([0-9A-F]{8})', host.call('coop.tribe')['out'], re.M)
    print(f'  host: NPC tribes\' species {hs}; the planted list {keys}', flush=True)
    ck(hs[-3:] and all(k in keys for k in hs[-3:]), f'the villages after the list have species of the list '
       f'({hs[-3:]} in {keys})')
    # the guest's copies (the host's villages come to it, its own controller forms none)
    t0, gn = time.time(), 0
    while time.time() - t0 < 20:
        gn = tribes(guest)
        if gn >= n:
            break
        time.sleep(1)
    gs = re.findall(r'^tribe \d+ at .* species=([0-9A-F]{8})', guest.call('coop.tribe')['out'], re.M)
    print(f'  guest: {gn} tribes, NPC species {gs}', flush=True)
    ck(gn == n, f'the guest\'s game has the host\'s tribes ({gn} of {n})')
    ck(sorted(gs) == sorted(hs), 'the guest\'s NPC tribes have the host\'s species')
    ck(faults(guest) == g0, 'no fault in the guest\'s game')
    # the test command once more: the call that crashed the host in the campaign
    out = host.call('coop.tribe form')['out'].strip()
    print(f'  host: {out}', flush=True)
    ck(faults(host) == f0, 'no fault after coop.tribe form')
    ck(tribes(host) == n, 'nothing formed past the property list')
    # (5) back as it was
    lb2, cap2 = dword(host, ctrl + 0xD0), dword(host, ctrl + 0xD8)
    print(f'  host: the list moved: {lb2 != lb} ({lb:#x} -> {lb2:#x}, capacity {(cap2 - lb2) // 4})', flush=True)
    if lb2 == lb:  # (not moved: its own capacity back)
        put(host, ctrl + 0xD8, cap)
    k = min(len(old), (cap2 - lb2) // 4)
    for i, v in enumerate(old[:k]):
        put(host, lb2 + 4 * i, v)
    put(host, ctrl + 0xD4, lb2 + 4 * k)
    out, st = species(host)
    print(f'  host: restored: {out}', flush=True)
    host.call('coop.fix tribe.form_pace on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
