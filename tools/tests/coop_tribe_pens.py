#!/usr/bin/env python3
"""Tribe: the player tribe of both games has its animal pen and egg pen (BUG-0856, evidence).

The fork PandizeLive/SporeCoop-Lineages crashed at the end of Tribe (after the City Hall editor): graduation hides the
villages' assets and calls the domesticated-animal pen's vt 0x54 (cTribe +0x19C4) with no null check; every tribe
of the fork's restored fixture had a null pen and egg pen (+0x19C8). This looks at our tribes: the fixture's, a tribe
the host's game forms during play (`coop.tribe npc_tribe`: the game's own forming), the guest's copy of that tribe
(made by the mod, BUG-0381 / BUG-0483), and both after the guest rejoined (the guest's world loaded from the host's
share save).
Graduation's call (0xCDD461 in 0xCDD0F0) reads the pen of the player tribe only (GetPlayerTribe); the computer's
villages have no pens in the game itself (null in the fixture and in a tribe the game forms: lab 20261112-091444).
PASS: the player tribe of each game has a non-null +0x19C4 and +0x19C8 (the other tribes are printed).
"""
import re
import struct
import time

import lane
from coop_tribe_member_age import rejoin
from twoinst import Checks, start


def mem(con, a, n):
    b = bytearray()
    for line in con.call(f'mem.read {a:#x} {n}')['out'].splitlines():
        p = line.split()
        if len(p) > 1 and re.fullmatch(r'[0-9A-F]{8}', p[0]):
            b += bytes(int(x, 16) for x in p[1:17] if re.fullmatch(r'[0-9A-F]{2}', x))
    return bytes(b[:n])


def pens(con):
    """[(tribe line, pen, egg pen)] of every tribe of this game"""
    out = []
    for ln in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'(tribe .*?)obj=(?:0x)?([0-9A-Fa-f]+)', ln)
        if not m:
            continue
        t = int(m.group(2), 16)
        b = mem(con, t + 0x19C4, 8)
        pen, egg = struct.unpack('<II', b) if len(b) == 8 else (None, None)
        out.append((m.group(1).strip()[:60], pen, egg))
    return out


def check(con, who, tag, ck):
    rows = pens(con)
    for r in rows:
        print(f'  {tag} {who}: {r[0]} pen={r[1] and hex(r[1])} egg_pen={r[2] and hex(r[2])}', flush=True)
    pl = [r for r in rows if '(player)' in r[0]]
    ck(len(pl) == 1 and pl[0][1] and pl[0][2], f'{tag}: the player tribe of the {who}\'s game has its pens ({pl})')


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    check(host, 'host', 'fixture', ck)
    check(guest, 'guest', 'fixture', ck)
    print('  host: ' + host.call('coop.tribe npc_tribe 3 45')['out'].strip(), flush=True)
    time.sleep(12)
    check(host, 'host', 'formed', ck)
    check(guest, 'guest', 'formed', ck)
    if rejoin(guest, ck):
        time.sleep(15)
        check(guest, 'guest', 'rejoined', ck)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
