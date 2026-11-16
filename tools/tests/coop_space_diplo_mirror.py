#!/usr/bin/env python3
"""BUG-0071: shared diplomacy never sends the allies mirror empire (a partner's colonies, local to
each copy), so a guest is not allied with / contacted by an empire of its own galaxy that has the
same political id.

 1. The guest gets an empire of its own galaxy it never met: an allies mirror made and detached
    (`allies fake` + `allies detach`: the mod forgets it, the cEmpire stays with its star, no
    contact and no alliance), pol G.
 2. The host makes an allies mirror (`allies fake`, allied and contacted by construction) with the
    id G: each copy hands out ids from its own counter, so the host first takes the ids up to G
    (`allies nextpol bump`, as other new empires of its galaxy would).
 3. The guest asks for the host's table (`diplomacy sync`, the Resync button). The guest's record
    of G (the game's relationship entry and contact flags) must stay without alliance and contact.
Before the fix the host's mirror record was reconciled onto G: allied and contacted.

  coop_space_diplo_mirror.py [--without-fix]   (--without-fix: `diplomacy mirrorfix 0` on both)"""
import re
import sys
import time

from harness import sp
from twoinst import Checks, start


def free_planets(con):
    out = []
    for s in [int(x) for x in re.findall(r'free (\d+)', sp(con, 'allies free'))][1:6]:
        p = re.findall(r"planet ([0-9A-F]+) '[^']*' tech=([0-3])\b", sp(con, 'allies star', s))
        if p:
            out.append(p[0][0])
    return out


def mirror_pol(con, name):
    m = re.search(r"mirror '%s' id=\d+ \w+ pol=([0-9A-F]+)" % name, sp(con, 'allies list'))
    return m.group(1) if m else None


def flags(con, pol):
    """bit 1 war, 2 alliance (the relationship entry), 0x10 met, 0x20 contacted (cPlayer flags);
    0 when the game has no record of it"""
    m = re.search(r'rec %s flags=(\d+)' % pol, sp(con, 'diplomacy table'))
    return int(m.group(1)) if m else 0


def main():
    without = '--without-fix' in sys.argv
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    if without:
        print(' ', sp(host, 'diplomacy mirrorfix 0'), sp(guest, 'diplomacy mirrorfix 0'))
    ck('rivals=0' in sp(host, 'rivalry') or 'rivalry' not in sp(host, 'features'), 'allies mode (diplomacy shared)')
    fg = free_planets(guest)
    print('  guest fake:', sp(guest, 'allies fake Npc', fg[0]).splitlines()[0])
    det = sp(guest, 'allies detach Npc')
    print('  guest detach:', det)
    g = (re.search(r'detached ([0-9A-F]+)', det) or [None, None])[1]
    f0 = flags(guest, g)
    print('  guest empire:', g, sp(guest, 'allies empire', g), 'flags', f0)
    ck(g and 'Npc' in sp(guest, 'allies empire', g) and f0 & 0x32 == 0,
       f'the guest has an empire {g} it never met (flags {f0})')
    fh = free_planets(host)
    c = int(re.search(r'nextpol ([0-9A-F]+)', sp(host, 'allies nextpol')).group(1), 16)
    for _ in range(max(0, min(20, (int(g, 16) & 0x7FFFFFFF) - c)) if g else 0):
        sp(host, 'allies nextpol bump')  # the host's galaxy made other empires since: same id now
    print('  host', sp(host, 'allies nextpol'), '| fake:', sp(host, 'allies fake Zed', fh[1]).splitlines()[0])
    h = mirror_pol(host, 'Zed')
    ck(h and h == g, f'a host mirror has the id of the guest\'s empire ({h} == {g})')
    ck(h and 'allies=1' in sp(host, 'allies empire', h), f'the host is allied with its mirror: {h and sp(host, "allies empire", h)}')
    print('  sync:', sp(guest, 'diplomacy sync'))
    time.sleep(6)
    f1 = flags(guest, g)
    print('  guest log:', sp(guest, 'diplomacy log').replace('\n', ' | ')[-400:])
    print('  guest flags of', g, 'after the sync:', f1)
    ck(f1 & 2 == 0, f'the guest is not allied with {g} ({f1})')
    ck(f1 & 0x30 == 0,
       f'and has no contact with it it did not make ({f0} -> {f1})')

    sp(host, 'allies drop')
    sp(host, 'diplomacy mirrorfix 1')
    sp(guest, 'diplomacy mirrorfix 1')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
