#!/usr/bin/env python3
"""BUG-0130: a guest that plays another world of the host's stage while connected gets nothing of
the host's world applied (DNA total, its own NPCs removed, ...): its own game would save it.

  1. the usual session (Creature fixture), then the guest leaves and plays a world of its own:
     a copy of the fixture planet under another name (Otherworld: another world id, its own NPCs)
  2. the guest keeps its world when the host's world arrives (coop.worldfix autoload 0, as
     auto_load_world=0 in coop.ini) and joins the host
  3. the host earns DNA (coop.dna add): the host sends its total
  4. on the guest, for 15 s: the game's DNA stays the guest's own, its own NPCs are not removed
     (npc local_removed), it is still in Otherworld

  5. BUG-0231 (GitHub #165, DNA row 2): the guest earns 20 DNA in its own world (coop.dna add 20,
     through the game's AddEvolutionPoints): the guest's game DNA rises by 20 and the host's does
     not; then the guest loads the host's world (coop.share) and its DNA becomes the host's total

  coop_join_other_world.py [--without-fix]   (coop.corefix same_place 0: must FAIL)
  SPORE_FIX_OFF=core.world_dna coop_join_other_world.py   (the DNA goes to the host: must FAIL)
"""
import glob
import os
import re
import shutil
import sys
import time

import lane
from twoinst import Checks, start


def num(pat, text, default=None):
    m = re.search(pat, text)
    return float(m.group(1)) if m else default


def world_of(con):
    m = re.search(r"world '([^']*)'", con.call('coop.avatar')['out'])
    return m.group(1) if m else None


def main():
    without = '--without-fix' in sys.argv
    host, guest = start()
    ck = Checks()
    for con in (host, guest):
        con.call(f'coop.corefix same_place {0 if without else 1}')
    print(' ', guest.call('coop.leave')['out'].strip())
    guest.call('coop.worldfix autoload 0')
    # a world of the guest's own: the received slot copied, the planet under another name
    games = os.path.join(lane.GUEST_DIR, 'Games')
    src = next(os.path.dirname(p) for p in glob.glob(os.path.join(games, '*', 'Scalinus.spo')))
    dst = os.path.join(games, 'Game9')
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(src, dst)
    for f in ('sporecoop.txt',):
        if os.path.exists(os.path.join(dst, f)):
            os.remove(os.path.join(dst, f))
    os.rename(os.path.join(dst, 'Scalinus.spo'), os.path.join(dst, 'Otherworld.spo'))
    print(' ', guest.call('coop.load Game9 Otherworld')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 120:
        if world_of(guest) == 'Otherworld.spo' and guest.call('coop.avatar')['ok']:
            break
        time.sleep(2)
    ck(world_of(guest) == 'Otherworld.spo', f"the guest plays a world of its own ({world_of(guest)})")
    guest.call('wait.frames 300 60000')
    time.sleep(8)
    dna0 = num(r'dna=([\d.]+)', guest.call('coop.dna')['out'])
    rm0 = num(r'local_removed=(\d+)', guest.call('coop.status')['out'], 0)
    print(f'  guest alone: dna {dna0}, own NPCs removed so far {rm0}')

    print(' ', guest.call(lane.JOIN)['out'].strip())
    time.sleep(8)
    print(' ', host.call('coop.dna add 50')['out'].strip())
    hdna = num(r'dna=([\d.]+)', host.call('coop.dna')['out'])
    dna_seen, rm1 = set(), rm0
    t0 = time.time()
    while time.time() - t0 < 15:
        d = num(r'dna=([\d.]+)', guest.call('coop.dna')['out'])
        dna_seen.add(d)
        rm1 = num(r'local_removed=(\d+)', guest.call('coop.status')['out'], 0)
        time.sleep(1)
    st = guest.call('coop.status')['out']
    print(f'  host dna {hdna}; guest dna seen {sorted(x for x in dna_seen if x is not None)}, own NPCs removed {rm1}')
    print('  guest:', ' | '.join(l for l in st.splitlines() if 'elsewhere' in l or 'connected' in l.lower())[:300])
    ck('connected' in st.lower() or 'client' in st.lower(), 'the guest is in the session')
    ck(dna_seen == {dna0}, f"the guest's DNA stays its own ({dna0}, host {hdna})")
    ck(rm1 == rm0, f"the guest's own NPCs stay ({rm1 - rm0} removed)")
    ck(world_of(guest) == 'Otherworld.spo', f'the guest is still in its own world ({world_of(guest)})')

    # 5. DNA earned in the guest's own world stays there (BUG-0231 row 2)
    g0 = num(r'dna=([\d.]+)', guest.call('coop.dna')['out'])
    h0 = num(r'dna=([\d.]+)', host.call('coop.dna')['out'])
    print(' ', guest.call('coop.dna add 20')['out'].strip())
    time.sleep(6)
    g1 = num(r'dna=([\d.]+)', guest.call('coop.dna')['out'])
    h1 = num(r'dna=([\d.]+)', host.call('coop.dna')['out'])
    print(f'  guest earned 20 in its own world: guest dna {g0} -> {g1}, host dna {h0} -> {h1}')
    ck(g0 is not None and g1 is not None and abs(g1 - g0 - 20) < 0.5, f"the guest's own world keeps its 20 DNA ({g0} -> {g1})")
    ck(h0 is not None and h1 is not None and abs(h1 - h0) < 0.5, f"the host's world does not get them ({h0} -> {h1})")
    if not without:
        guest.call('coop.worldfix autoload 1')
        print(' ', host.call('coop.share')['out'].strip())
        t0 = time.time()
        while time.time() - t0 < 150:
            if world_of(guest) not in (None, 'Otherworld.spo') and guest.call('coop.avatar')['ok']:
                break
            time.sleep(2)
        guest.call('wait.frames 120 30000')
        time.sleep(6)
        g2 = num(r'dna=([\d.]+)', guest.call('coop.dna')['out'])
        h2 = num(r'dna=([\d.]+)', host.call('coop.dna')['out'])
        print(f'  guest in the host\'s world ({world_of(guest)}): guest dna {g2}, host dna {h2}')
        ck(world_of(guest) not in (None, 'Otherworld.spo'), f"the guest loaded the host's world ({world_of(guest)})")
        ck(g2 is not None and h2 is not None and abs(g2 - h2) < 0.5, f"the guest's DNA is the host's total there ({g2} vs {h2})")

    for con in (host, guest):
        con.call('coop.corefix same_place 1')
    guest.call('coop.worldfix autoload 1')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
