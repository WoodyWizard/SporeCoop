#!/usr/bin/env python3
"""A player's own creature leaves with the player (BUG-0076): the guest plays its own creature, leaves,
and comes back under the same player id with "the world's own" creature (no creation of its own).
The host must show it with the world's species, not with the creature of the player who had the id
before (ids are reused: alloc_id takes the lowest free one).

  coop_creations_rejoin.py [--without-fix]   (--without-fix: coop.corefix creations_leave 0 and
                                              id_reuse 0 on the host, the code before the fix, so
                                              the test must FAIL)"""
import re
import sys
import time

import creations_state
import lane
from twoinst import Checks, shot, start, wait_guest_in


def field(out, pat):
    m = re.search(pat, out)
    return m.group(1) if m else None


def puppet_species(host):
    return field(host.call('coop.status')['out'], r"remote 1 '[^']*'.*\n.*species=([0-9A-F]{8})")


def main():
    without = '--without-fix' in sys.argv
    host, guest = start()
    ck = Checks()
    if without:
        for sw in ('creations_leave 0', 'id_reuse 0'):
            host.call('coop.corefix ' + sw)
        print('  fix switches off on the host')
    # BUG-0345: a guest plays its own creature by default only in a world it played it in: choose it,
    # then wait until the host has it (not the world's species it may have announced first)
    st0 = creations_state.save(guest)
    print('  guest profile before:', st0, '| chose', creations_state.choose_own(guest))
    w_av = field(host.call('coop.avatar')['out'], r'key=0x([0-9A-F]{8})')
    key = None
    t0 = time.time()
    while time.time() - t0 < 40 and (not key or key == w_av):
        key = field(host.call('coop.creations')['out'], r'player 1 creature: ([0-9A-F]{8})')
        time.sleep(2)
    hp = None
    for _ in range(15):
        hp = puppet_species(host)
        if hp == key:
            break
        time.sleep(1)
    ck(key and key != '00000000' and hp == key, f'the host shows the guest with its own creature ({key}, puppet {hp})')
    h_av = field(host.call('coop.avatar')['out'], r'key=0x([0-9A-F]{8})')

    # the guest goes back to the world's own creature and leaves; it comes back under id 1
    print(' ', guest.call('coop.creations choose creature none')['out'].strip())
    print(' ', guest.call('coop.leave')['out'].strip())
    time.sleep(3)
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest), 'the guest is back in the host\'s world')
    gs = guest.call('coop.status')['out']
    ck('client id=1 ' in gs, 'it has the same player id again (1)')
    # the host sends its world again and the guest reloads it (its puppet is hidden meanwhile)
    time.sleep(20)
    wait_guest_in(host, guest)
    hp2 = None
    t0 = time.time()
    while time.time() - t0 < 120:
        hp2 = puppet_species(host)
        if hp2 and hp2 != '00000000':
            break
        if int(time.time() - t0) % 10 == 0:
            print(f'  {time.time() - t0:.0f} s: guest', guest.call('game.mode')['out'].strip()[:80], '|',
                  guest.call('coop.status')['out'].splitlines()[0][:80])
        time.sleep(1)
    time.sleep(6)  # a bundle that was going to come would have come by now (checked every 2 s)
    hp2 = puppet_species(host)
    hc = host.call('coop.creations')['out']
    if hp2 is None:
        st = host.call('coop.status')['out'].splitlines()
        for i, line in enumerate(st):
            if line.startswith('remote') or 'puppet' in line:
                print('  host status:', line.strip()[:200])
    print(f'  host avatar {h_av}; after the rejoin the host shows Bob as {hp2} (before: {hp2 == key and "the old creature" or "not the old one"})')
    print('  host creations:', hc.replace('\n', ' | ')[:300])
    ck(hp2 is not None and hp2 != key, f'the newcomer is not shown with the previous player\'s creature {key}')
    ck(hp2 == h_av, f'it is shown with the world\'s species {h_av}')
    shot(host, 'creations_rejoin_host.png')
    if without:
        for sw in ('creations_leave 1', 'id_reuse 1'):
            host.call('coop.corefix ' + sw)
    creations_state.restore(guest, st0)  # the profile as it was (BUG-0345)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
