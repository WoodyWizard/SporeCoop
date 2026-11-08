#!/usr/bin/env python3
"""BUG-0420 (class: world state applied without a world filter): a guest that plays another world of the host's
stage while it stays in the session takes no part in the host's evolution vote and keeps its own species table.

  1. the usual session (Creature fixture): the guest has the host's species table
  2. the guest, still connected, loads a world of its own (a copy of the fixture planet under another name, as
     coop_join_other_world.py; coop.worldfix autoload 0)
  3. the guest changes a species relation in its own world (coop.social record): it stays for 6 s (the host's last
     table was written over it every 0.5 s: social.world_gate)
  4. the host starts its evolution vote and ends it (coop.vote editor, coop.vote go): the guest's game stays in
     Creature for 25 s, no evolution editor opens in its own world, its vote screen does not open (core.vote_world)

  SPORE_FIX_OFF=social.world_gate coop_world_gate_vote.py   (step 3 FAILS)
  SPORE_FIX_OFF=core.vote_world coop_world_gate_vote.py     (step 4 FAILS)
"""
import glob
import os
import re
import shutil
import time

import lane
from twoinst import Checks, start


def world_of(con):
    m = re.search(r"world '([^']*)'", con.call('coop.avatar')['out'])
    return m.group(1) if m else None


def species(con):
    return {m.group(1): int(m.group(2)) for m in
            re.finditer(r'species ([0-9A-F]{8})/2B978C46/40626200 relation=(-?\d+)', con.call('coop.social species')['out'])}


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(6)
    hs = species(host)
    print(f'  host species table: {len(hs)} records, guest {len(species(guest))}')
    m0 = guest.call('game.mode')['out']
    guest.call('coop.worldfix autoload 0')
    games = os.path.join(lane.GUEST_DIR, 'Games')
    src = next(os.path.dirname(p) for p in glob.glob(os.path.join(games, '*', 'Scalinus.spo')))
    dst = os.path.join(games, 'Game9')
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(src, dst)
    if os.path.exists(os.path.join(dst, 'sporecoop.txt')):
        os.remove(os.path.join(dst, 'sporecoop.txt'))
    os.rename(os.path.join(dst, 'Scalinus.spo'), os.path.join(dst, 'Otherworld.spo'))
    print(' ', guest.call('coop.load Game9 Otherworld')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 120:
        if world_of(guest) == 'Otherworld.spo' and guest.call('coop.avatar')['ok']:
            break
        time.sleep(2)
    ck(world_of(guest) == 'Otherworld.spo', f"the guest plays a world of its own ({world_of(guest)})")
    guest.call('wait.frames 300 60000')
    time.sleep(6)
    st = guest.call('coop.status')['out']
    ck('connected' in st.lower() or 'client' in st.lower(), 'the guest is still in the session')
    # 3. its own species relation stays its own
    gs = species(guest)
    key = next((k for k in sorted(gs) if gs[k] in (1, 2, 3, 4)), next(iter(sorted(gs)), None))
    ck(key is not None, f'the guest has species records ({len(gs)})')
    if key:
        want = 6 if gs[key] != 6 else 2
        print(' ', guest.call(f'coop.social record {key} {want} 0')['out'].strip(), f'{key}: {gs[key]} -> {want}')
        seen = set()
        t0 = time.time()
        while time.time() - t0 < 6:
            seen.add(species(guest).get(key))
            time.sleep(0.5)
        print(f'  the guest\'s relation of {key} over 6 s: {sorted(x for x in seen if x is not None)}')
        ck(seen == {want}, f"the guest's own relation stays its own ({sorted(x for x in seen if x is not None)}, set {want})")
    # 4. the host's vote
    print(' ', host.call('coop.vote editor')['out'].strip().splitlines()[0])
    time.sleep(4)
    gv = guest.call('coop.vote')['out']
    ck(gv.startswith('vote none'), f"the guest's vote screen did not open ({gv.splitlines()[0]})")
    print(' ', host.call('coop.vote go')['out'].strip().splitlines()[0])
    modes = set()
    t0 = time.time()
    while time.time() - t0 < 25:
        modes.add(guest.call('game.mode')['out'].strip())
        time.sleep(1)
    print(f'  guest modes for 25 s after the host\'s vote: {sorted(modes)} (Creature: {m0.strip()})')
    ck(modes == {m0.strip()}, f"no evolution editor opened in the guest's own world ({sorted(modes)})")
    print('  guest:', [l for l in guest.call('coop.vote')['out'].splitlines() if 'elsewhere' in l])
    guest.call('coop.worldfix autoload 1')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
