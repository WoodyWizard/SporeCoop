#!/usr/bin/env python3
"""O1 of 0.7.13: a guest that loads a Space save of its own while it stays in the session (BUG-0816, BUG-0818).

  1. the usual Space session (fixture space); in the co-op galaxy the guest's hold gets spice A x7 (the game's
     cargo), which the host keeps as the guest's own co-op hold (BUG-0678)
  2. the guest, still connected, loads a copy of the share save under another planet name (own_world.load_own):
     its hold is that save's (no spice A x7)
  3. BUG-0816: over 25 s the guest's game hold (cPlayerInventory, `coop.space trade inv`) stays its own save's: the
     co-op hold the host kept is not put into it (no spice A x7)
  4. BUG-0818: the host's stage history changes (cPlayer +0x10FC, `coop.wsync stage`); over 10 s the guest's own
     save keeps its own (`coop.wsync dump` stages: the game's cPlayer fields)

  SPORE_FIX_OFF=space.hold_own_save coop_own_space.py  (step 3 FAILS)
  SPORE_FIX_OFF=wsync.space_own coop_own_space.py      (step 4 FAILS)
"""
import re
import time

import own_world
from coop_space_items_rejoin import inv, spice_keys
from coop_space_map import no_movie, sp
from twoinst import Checks, start


def traits(con):
    out = con.call('coop.wsync dump')['out']
    m = re.search(r'stages traits=([0-9A-F,]+)', out)
    return m.group(1).split(',') if m else None


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    no_movie(guest)
    no_movie(host)
    keys = spice_keys(guest)
    ck(keys, f'the game knows spices ({len(keys)})')
    if not keys:
        return ck.result()
    sa = (4, keys[0])
    g0, _ = inv(guest)
    n0 = g0.get(sa, (0,))[0]
    print('  guest: ' + sp(guest, 'trade spice 0 7'), flush=True)
    g1, _ = inv(guest)
    ck(g1.get(sa, (0,))[0] == n0 + 7, f'the guest has spice A x{n0 + 7} in the co-op galaxy ({g1.get(sa)})')
    time.sleep(5)  # the guest's hold reaches the host
    print('  host: ' + sp(host, 'trade kept').replace('\n', ' | '), flush=True)
    ht = traits(host)
    print(f'  the host\'s stage traits {ht}, the guest\'s {traits(guest)}', flush=True)
    # 2. the guest's own save
    ok = own_world.load_own(guest, 'Madas.spo', 'Otherspace', 'Space')
    ck(ok, 'the guest plays a Space save of its own')
    ck(own_world.connected(guest), 'the guest is still in the session')
    if not ok:
        return ck.result()
    no_movie(guest)
    print('  guest:', [l for l in guest.call('coop.status')['out'].splitlines() if l.startswith('own world')], flush=True)
    # 3. BUG-0816
    seen = set()
    t0 = time.time()
    while time.time() - t0 < 25:
        g, _ = inv(guest)
        seen.add(g.get(sa, (0,))[0])
        time.sleep(1)
    print(f'  the guest\'s spice A in its own save over 25 s: {sorted(seen)} (its own save: {n0})', flush=True)
    print('  guest: ' + sp(guest, 'trade kept').splitlines()[0], flush=True)
    ck(seen == {n0}, f'the guest\'s own save keeps its own hold ({sorted(seen)}, the save has {n0})')
    # 4. BUG-0818
    own = traits(guest)
    ck(own, f'the guest\'s own stage traits ({own})')
    if own and ht:
        new = next((v for v in own if v != own[0] and v != '00000000'), None) or '00000000'
        if new == own[0]:
            new = '00000001'
        print('  host:', host.call(f'coop.wsync stage 0 {new}')['out'].strip(), flush=True)
        vals = set()
        t0 = time.time()
        while time.time() - t0 < 10:
            t = traits(guest)
            vals.add(t[0] if t else None)
            time.sleep(1)
        print(f'  the guest\'s own stage 0 over 10 s: {sorted(str(v) for v in vals)} (its own {own[0]}, the host\'s now {new})', flush=True)
        print('  guest:', [l for l in guest.call('coop.wsync')['out'].splitlines() if 'stages' in l or 'space_own' in l], flush=True)
        ck(vals == {own[0]}, f'the guest\'s own save keeps its own stage history ({sorted(str(v) for v in vals)})')
        host.call(f'coop.wsync stage 0 {ht[0]}')
    guest.call('coop.worldfix autoload 1')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
