#!/usr/bin/env python3
"""The guest's copy of the host's ships (squadron.cpp) under packet loss, and the guest's own escort:

 1. BUG-0037: the host has more than 18 NPC ships here, so its ship state goes out in 2 unreliable
    chunks per cycle (10 Hz). The host drops chunk 0 five times in a row (`coop.fleet drop 0 5`,
    a lost packet on a real link). The guest's game must keep the very same ship objects for the
    host's ships (the pointers in the game's UFO list): before the fix every ship of the lost chunk
    was blown up with the game's death and spawned again 0.1 s later.
 2. BUG-0084: the guest has an escort ship of its own (type 3 = the posse: DestroyNPCUFOs keeps
    exactly type 3 for keepPosse) while it is in the host's place: it must stay in the guest's
    game. Before the fix the guest destroyed it with the host's next ship state. The ship is made
    with SpawnUFO and pol -1: a type 3 with the own empire's political id made that way is freed by
    the game at once (it is not in the real posse).

 3. BUG-0230: a ship the host destroys goes on the guest, also with 2 chunks per cycle and lost
    chunks: two of the host's ships (the first and the last listed, one per chunk) are destroyed
    through the game's death while chunk 0 is lost for 3 s; within 2 s after the drops ended both
    mirrors are gone from the guest's game and every other mirror kept its object. With chunk 1
    lost for good a destroyed ship's mirror stays (no complete cycle, no removal: by design) and
    goes within 2 s once the chunk comes again.

  coop_space_fleet.py [--without-fix]   (--without-fix: coop.fleet chunkfix 0 and keepposse 0 on
                                          the guest, the old code, so the test must FAIL)"""
import re
import sys
import time

from twoinst import Checks, start

SHIP = re.compile(r'ship #(-?\d+) (\S+) type=(\d+) pol=(\S+)')


def ships(con):
    """[(net id, pointer, type, pol)] of the game's UFO list (coop.fleet list walks GetAllUFOs)"""
    out = con.call('coop.fleet list')['out']
    return [(int(m.group(1)), m.group(2), int(m.group(3)), m.group(4)) for m in SHIP.finditer(out)], out


def counters(con):
    out = con.call('coop.fleet list')['out']
    return {k: int(v) for k, v in re.findall(r'(\w+)=(\d+)', out.splitlines()[0] if out else '')}


def main():
    without = '--without-fix' in sys.argv
    host, guest = start('space')
    ck = Checks()
    if without:
        print(' ', guest.call('coop.fleet chunkfix 0')['out'].strip())
        print(' ', guest.call('coop.fleet keepposse 0')['out'].strip())
    time.sleep(3)

    # ---- 1. a lost chunk (BUG-0037) ----
    for _ in range(25):
        host.call('coop.fleet spawn 2')
    hs, _ = ships(host)
    listed = [s for s in hs if s[0] >= 0]
    print(f'  host lists {len(listed)} NPC ships')
    ck(len(listed) > 18, f'the host has more than 18 NPC ships here ({len(listed)}): 2 chunks per cycle')
    mirrors = {}
    for _ in range(20):
        hs, _ = ships(host)
        gs, _ = ships(guest)
        hid = {s[0] for s in hs if s[0] >= 0}
        mirrors = {s[0]: s[1] for s in gs if s[0] >= 0 and s[0] in hid}
        if len(mirrors) >= len(hid) > 18:
            break
        time.sleep(0.5)
    ck(len(mirrors) > 18, f'the guest shows the host\'s ships ({len(mirrors)})')
    time.sleep(1)
    gs, _ = ships(guest)
    before = {s[0]: s[1] for s in gs if s[0] in mirrors}
    c0 = counters(guest)
    print(' ', host.call('coop.fleet drop 0 5')['out'].strip())
    time.sleep(2)
    gs, _ = ships(guest)
    after = {s[0]: s[1] for s in gs if s[0] >= 0}
    c1 = counters(guest)
    hs, _ = ships(host)
    still = {s[0] for s in hs if s[0] >= 0}  # the host's ships that did not leave or die meanwhile
    changed = sorted(i for i, p in before.items() if i in still and after.get(i) != p)
    print(f'  guest: created {c0.get("created")} -> {c1.get("created")}, removed {c0.get("removed")} -> '
          f'{c1.get("removed")}, stale_chunks={c1.get("stale_chunks")} partial_cycles={c1.get("partial_cycles")}')
    ck(not changed, f'after 5 lost chunks the guest\'s game has the same ship objects (replaced: {changed})')

    # ---- 2. the guest's own escort (BUG-0084) ----
    out = guest.call('coop.fleet spawn 3')['out'].strip()
    print(' ', out)
    m = re.search(r'spawned (\S+) type=3 pol=(\S+)', out)
    ck(m, f'the guest made an escort ship (type 3, the posse): {out}')
    time.sleep(2)
    gs, _ = ships(guest)
    kept = m and [s for s in gs if s[1] == m.group(1) and s[2] == 3]
    ck(kept, f'2 s with the host: the guest\'s escort is still in its game ({kept})')

    # ---- 3. a destroyed ship goes, also with lost chunks (BUG-0230) ----
    def mirror_map():
        gs, _ = ships(guest)
        return {s[0]: s[1] for s in gs if s[0] >= 0}

    def host_ids():
        hs, _ = ships(host)
        return [s[0] for s in hs if s[0] >= 0]

    for _ in range(4):  # keep more than 18 after the kills: the cycle stays at 2 chunks
        host.call('coop.fleet spawn 2')
    time.sleep(2)
    ids = host_ids()
    m0 = mirror_map()
    ck(len(ids) >= 23, f'the host has enough NPC ships for 2 chunks after 3 kills ({len(ids)})')
    a, b = ids[0], ids[-1]
    ck(a in m0 and b in m0, f'the guest mirrors ships #{a} and #{b}')
    print(' ', host.call('coop.fleet drop 0 30')['out'].strip(), '(chunk 0 lost for 3 s)')
    t_drop = time.time()
    for i in (a, b):
        print(f'  shoot #{i}:', host.call(f'coop.fleet shoot {i} 1000000')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 5 and (a in host_ids() or b in host_ids()):
        time.sleep(0.2)
    ck(a not in host_ids() and b not in host_ids(), 'the host no longer lists the two ships')
    t_gone = time.time()
    if t_gone - t_drop < 2.5:
        m1 = mirror_map()
        print(f'  during the loss: #{a} {"kept" if m1.get(a) == m0.get(a) else "gone"}, '
              f'#{b} {"kept" if m1.get(b) == m0.get(b) else "gone"}')
    end = max(t_drop + 3.0, t_gone) + 2.0
    while time.time() < end and (a in mirror_map() or b in mirror_map()):
        time.sleep(0.2)
    m2 = mirror_map()
    ck(a not in m2 and b not in m2, f'within 2 s after the drops ended both mirrors are gone (#{a}: {a in m2}, #{b}: {b in m2})')
    still = set(host_ids())
    replaced = sorted(i for i, p in m0.items() if i in still and i not in (a, b) and m2.get(i) != p)
    ck(not replaced, f'every other mirror kept its object (replaced: {replaced})')
    time.sleep(1)
    ck(len(mirror_map()) == len(host_ids()), f'the guest shows as many mirrors as the host has NPC ships '
       f'({len(mirror_map())} / {len(host_ids())})')
    # steady loss: no complete cycle, no removal (squadron has no timeout); then the chunk comes again
    ids = host_ids()
    c = ids[-1]
    print(' ', host.call('coop.fleet drop 1 1000000')['out'].strip(), '(chunk 1 lost)')
    time.sleep(0.5)
    print(f'  shoot #{c}:', host.call(f'coop.fleet shoot {c} 1000000')['out'].strip())
    wait_t = time.time()
    while time.time() - wait_t < 5 and c in host_ids():
        time.sleep(0.2)
    time.sleep(3)
    ck(c in mirror_map(), f'with chunk 1 lost for good the mirror of #{c} stays (no complete cycle)')
    print(' ', host.call('coop.fleet drop 1 0')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 2 and c in mirror_map():
        time.sleep(0.2)
    ck(c not in mirror_map(), f'the chunk comes again: the mirror of #{c} goes ({time.time() - t0:.1f} s)')
    host.call('coop.fleet drop 0 0')

    guest.call('coop.fleet chunkfix 1')
    guest.call('coop.fleet keepposse 1')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
