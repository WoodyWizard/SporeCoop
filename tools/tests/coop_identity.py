#!/usr/bin/env python3
r"""BUG-9258 (the Lineages port, phase 1): every game keeps a persistent player identity per Spore profile
(SporeCoop\identity.bin next to EditorSaves.package). A guest sends its own to the host after each join
(MSG_IDENTITY, u64), and the host sends the table of the players here to every guest.

Checks the other game's view, not the sender's bookkeeping: the host knows the guest's identity (the one the guest
made for its own profile), and the guest knows the host's from the host's table. Then the guest leaves and joins again:
the host gets the same identity again (it is read back from the profile, not made anew).

A new feature, no fix switch: on main (no MSG_IDENTITY) `coop.identity` is an unknown command and the test fails.
"""
import re
import time

import lane
from twoinst import Checks, start, wait_guest_in


def ident(inst):
    out = inst.call('coop.identity').get('out', '')
    m = re.search(r'own=([0-9A-F]{16})', out)
    return (m.group(1) if m else ''), out


def wait_for(inst, needle, seconds=30):
    out = ''
    t = time.time() + seconds
    while time.time() < t:
        _, out = ident(inst)
        if needle and needle in out.split('own=', 1)[-1][16:]:
            return True, out
        time.sleep(1)
    return False, out


def main():
    host, guest = start('creature')
    ck = Checks()
    h_own, h_out = ident(host)
    g_own, g_out = ident(guest)
    print('  host: ', h_out.strip())
    print('  guest:', g_out.strip())
    ck(len(h_own) == 16 and int(h_own, 16) != 0, f'the host has an identity of its own ({h_own or "none"})')
    ck(len(g_own) == 16 and int(g_own, 16) != 0, f'the guest has an identity of its own ({g_own or "none"})')
    ck(h_own != g_own, 'the two profiles have different identities')
    ok, out = wait_for(host, g_own[:8])
    ck(ok, f'the host knows the guest\'s identity {g_own[:8]}... ({out.strip()})')
    ok, out = wait_for(guest, h_own[:8])
    ck(ok, f'the guest knows the host\'s identity {h_own[:8]}... from the host\'s table ({out.strip()})')
    # a rejoin: the same profile comes back with the same identity
    guest.call('coop.leave')
    time.sleep(3)
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest), 'the guest joined again')
    g_again, _ = ident(guest)
    ck(g_again == g_own, f'the guest\'s identity is the same after a rejoin ({g_again})')
    ok, out = wait_for(host, g_own[:8], 45)
    ck(ok, f'after the rejoin the host knows the guest by the same identity ({out.strip()})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
