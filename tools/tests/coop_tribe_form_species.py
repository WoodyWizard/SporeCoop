#!/usr/bin/env python3
"""Tribe: the NPC tribe controller does not read its next species past its list (BUG-1233).

NpcTribeController_FormTribe 0xCC6440 takes the next tribe's species from the controller's list (+0xD0..+0xD4) at
index min(size, tribes - 1): with as many tribes as species it reads the dword after the list's end and the bake
check faults at 0x406EFF (a final check's 4-player campaign: one fault a second from the game's own update, then a
host crash). The game builds that list shorter than its property list allows on some planets: the campaign's own
planet (its Tribe save, lab 20261118-002106) had 4 species for 5 NPC tribes, the tribe fixture has 5 of 5.

1. Two games on the tribe fixture (cold): the host's controller has 2 tribes and a list of 5 species.
2. The list is made as short as the game made it on the campaign's planet, for the fixture's tribes: 2 species, the
   dword after them unreadable as there (0xFFFFFFFF: what the campaign's save had past its list, the same fault).
3. One `coop.tribe form` forms the third tribe (species 1 of 2, in the list; tribe.form_pace off). With 3 tribes the
   game's own update calls FormTribe every second by itself: each call would take species 2, past the list.
4. For 12 s: no fault in the host's game, the host's game still has 3 tribes (nothing formed from outside the list).
   Then the test command once more (the call that crashed the host): no fault, it says the controller is held.
5. The list is put back as it was.

SPORE_FIX_OFF=tribe.form_species must FAIL (a fault a second at 0x406EFF in FormTribe, caught by the game's handler,
or a crash).

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
    old = dword(host, lb + 8)
    # (2) as short as the game built it on the campaign's planet, for the fixture's 2 tribes; past it what was there
    put(host, lb + 8, 0xFFFFFFFF)
    put(host, ctrl + 0xD4, lb + 8)
    out, st = species(host)
    print(f'  host: planted: {out}', flush=True)
    ck(st and st[2] == 2, 'the host\'s species list holds 2 species now')
    host.call('coop.fix tribe.form_pace off')
    f0 = faults(host)
    out = host.call('coop.tribe form')['out'].strip()
    print(f'  host: {out}', flush=True)
    ck(out.startswith('tribes 2 -> 3'), 'the host\'s controller formed the third tribe (species 1 of the list)')
    # (4) the game's own update: with 3 tribes it calls FormTribe every second
    t0, seen = time.time(), []
    while time.time() - t0 < 12:
        time.sleep(2)
        seen.append((faults(host) - f0, tribes(host)))
    print(f'  host: (new faults, tribes) every 2 s: {seen}', flush=True)
    out, st = species(host)
    print(f'  host: {out}', flush=True)
    ck(st and st[0] == 3 and st[2] == 2, 'the next species would be past the list (3 tribes, 2 species)')
    ck(all(f == 0 for f, _ in seen), f'no fault in the host\'s game while its update calls FormTribe '
       f'({seen[-1][0] if seen else "?"} new)')
    ck(all(n == 3 for _, n in seen), f'the host\'s game formed no tribe from outside the list ({[n for _, n in seen]})')
    # the test command once more: the call that crashed the host in the campaign
    out = host.call('coop.tribe form')['out'].strip()
    print(f'  host: {out}', flush=True)
    ck(faults(host) == f0, 'no fault after coop.tribe form')
    ck('held: tribe.form_species' in out, 'coop.tribe form says the controller is held')
    # (5) back as it was
    put(host, ctrl + 0xD4, le)
    put(host, lb + 8, old)
    out, st = species(host)
    print(f'  host: restored: {out}', flush=True)
    host.call('coop.fix tribe.form_pace on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
