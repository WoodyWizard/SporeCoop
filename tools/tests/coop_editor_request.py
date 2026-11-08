#!/usr/bin/env python3
"""Creature: the editor requests the mod builds against the game's own (BUG-0720, class BUG-0431).

A guest's own editor of an evolution vote (coop.cpp open_own_editor) and the test commands (`coop.vote
editor`, `coop.vote evolve`) built their Editors::EditorRequest by hand: +0xC, the species and +0x94, while
the creature stage's own evolution request (CreatureGame_HandleMessage 0xD42E50, message 0x4471C60) also
sets the flags +0x39 / +0x3B = 0, +0x3C = 1 and the +0x70 vector of the four consequence traits. Now all of
them come from one builder (build_editor_request).

On each game (the host and the guest, Creature): `coop.editreq diff` runs the game's own builder (its message
handler once, the Submit captured, nothing opened) and the mod's builder for the own editor and for the
evolution, and compares them byte by byte (pointers, vtables and the strings' storage left out; the vector by its
contents). Allowed: +0x90 (the evolution tag) for the own editor (BUG-0466 charges its price).
1. Both kinds: no other difference, on both games.
The own editor itself (opened, priced, its palette) is checked by editor_sync_guest and guest_editor_locks.
SPORE_FIX_OFF=creations.editor_request must FAIL at 1 (+0x39 / +0x3B / +0x3C and the vector differ).
"""
import re
import time

from twoinst import Checks, start


def diff(con, name, ck):
    out = ''
    for _ in range(10):  # the creature game's handler is seen with its first message
        out = con.call('coop.editreq diff')['out']
        if 'editreq own' in out:
            break
        time.sleep(1)
    print(f'  {name}: ' + out.strip().replace('\n', '\n    '), flush=True)
    for kind in ('own', 'evolve'):
        m = re.search(rf'editreq {kind}: diff= ?(.*?) allowed=', out)
        ck(m is not None and m.group(1).strip() == 'none',
           f'1. {name}: the mod\'s {kind} request has the game\'s fields ({m.group(1).strip() if m else "no answer"})')


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(3)
    diff(host, 'host', ck)
    diff(guest, 'guest', ck)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
