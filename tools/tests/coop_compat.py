#!/usr/bin/env python3
"""The message manifest's direction gate (B3, BUG-1136) and the message family revisions (B6, BUG-1137) on two games.

1. Direction gate: the guest sends the host a crafted MSG_DNA (a type only the host sends: the shared DNA total)
   through the test seam `coop.manifest send`. The host drops it before any handler and counts it (`coop.manifest`
   dir_drops), its DNA stays, the session goes on. SPORE_FIX_OFF=net.dir_gate fails (the message reaches the host's
   handlers, nothing counts it).
2. A planted conflict: the guest leaves, the host plants Creature family revision 2 (`coop.manifest plant creature 2`)
   while it plays Creature, the guest joins again: refused. The guest's co-op window (its chat log in coop.status)
   says why and names both versions ("... the Creature messages of these versions do not fit together (revision 2
   there, 1 here) ..."), the host says "Bob (...) was not let in". SPORE_FIX_OFF=net.family_check fails (it joins).
3. A revision in a family the stage does not use (Space while the host plays Creature): the guest joins as before.

  SPORE_LANE=10 python3 tools/tests/coop_compat.py
"""
import re
import struct
import time

from harness import dna, waiter
import lane
from twoinst import Checks, keyframe, start, wait_guest_in


def first(con):
    return (con.call('coop.status')['out'].splitlines() or [''])[0]


wait = waiter(timeout=20, step=0.5, final=True)


def drops(con):
    m = re.search(r'dir_drops=(\d+) compat_refused=(\d+)', con.call('coop.manifest')['out'])
    return (int(m.group(1)), int(m.group(2))) if m else (-1, -1)


def main():
    host, guest = start('creature')
    try:
        return run(host, guest)
    finally:  # a warm lane's next test starts with this build's own revisions
        for c in (host, guest):
            try:
                c.call('coop.manifest reset')
            except OSError:
                pass


def run(host, guest):
    ck = Checks()
    time.sleep(2)
    print('  host: ' + host.call('coop.manifest')['out'].strip().replace('\n', ' | ')[:300], flush=True)
    # 1. the direction gate
    d0, r0 = drops(host)
    dna0 = dna(host)
    out = guest.call('coop.manifest send 23 ' + struct.pack('<f', 77777.0).hex())['out'].strip()
    print('  guest: ' + out, flush=True)
    got = wait(lambda: drops(host)[0] > d0, 10)
    d1, _ = drops(host)
    dna1 = dna(host)
    print(f'  host: dir_drops {d0} -> {d1}, DNA {dna0} -> {dna1}', flush=True)
    ck(got and d1 == d0 + 1, f'[gate] the host dropped the guest\'s host-only MSG_DNA and counted it (dir_drops {d0} -> {d1})')
    ck(dna1 is not None and dna0 is not None and abs(dna1 - dna0) < 1 and dna1 < 77000,
       f'[gate] the host\'s DNA did not take the crafted total ({dna0} -> {dna1})')
    ck('connected' in first(guest) and 'players=2/' in first(host), f'[gate] the session goes on ({first(host)})')
    # 2. a planted conflict in the family of the host's stage
    guest.call('coop.leave')
    wait(lambda: first(guest).startswith('offline'), 15)
    wait(lambda: 'players=1/' in first(host), 15)
    print('  host: ' + host.call('coop.manifest plant creature 2')['out'].strip(), flush=True)
    guest.call(lane.JOIN)
    refused = wait(lambda: 'do not fit together' in guest.call('coop.status')['out'], 40)
    gst = guest.call('coop.status')['out']
    why = next((ln.strip() for ln in gst.splitlines() if 'do not fit together' in ln), '')
    hst = host.call('coop.status')['out']
    hline = next((ln.strip() for ln in hst.splitlines() if 'was not let in' in ln), '')
    print(f'  guest: {first(guest)} | {why[:260]}', flush=True)
    print(f'  host: {first(host)} | {hline[:260]}', flush=True)
    keyframe('refused', 'the guest was not let in: its co-op window / feed names both versions and the Creature messages')
    ck(refused and first(guest).startswith('offline') and 'players=1/' in first(host),
       f'[conflict] the guest with another Creature revision is not let in ({first(guest)}; host {first(host)})')
    ck('the Creature messages' in why and 'revision 2 there, 1 here' in why and 'SporeCoop' in why,
       f'[conflict] the guest is told why, with both versions: {why[:200]!r}')
    ck('was not let in' in hline and 'revision 1 there, 2 here' in hline, f'[conflict] the host says so: {hline[:200]!r}')
    ck(drops(host)[1] == r0 + 1, f'[conflict] the host counted one refused join ({drops(host)})')
    # 3. a revision of a family the stage does not use: joins
    host.call('coop.manifest reset')
    print('  host: ' + host.call('coop.manifest plant space 2')['out'].strip(), flush=True)
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest, 200), '[unused family] with another Space revision the guest joins the Creature host')
    ck(drops(host)[1] == r0 + 1, f'[unused family] nobody refused ({drops(host)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
