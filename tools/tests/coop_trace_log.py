#!/usr/bin/env python3
"""Support logging (mods/coop/trace.h, docs/REPORTING.md#reporting-reading-logs): a player's report
zip has to answer "only one of us could eat" (BUG-0279) and "I spawn as the host's creature"
(BUG-0278). Two games play a little Cell, then a little Creature, and the test reads both games'
logs:

  Cell:  the guest eats a plant of the host's pool with the game's eat handler (EatFood,
         `coop.pool eat`), the host eats one of its own;
         * the guest's log says "eat: sent gone c<K>", the host's "eat: from <id> gone c<K> ... applied"
           (the same object key on both machines);
         * both logs have periodic "trace:" lines (role, mode, world, position) with a partner line
           ("partner <id> ... same_place=1 puppet=shown"), "cell:" lines with the food on the screen
           and the diet, the guest's with the distance to the host;
         * a "body: puppet of" line names the partner's cell model.
  Creature (a new start from the creature fixture):
         * "creature:" lines (NPCs near, nest distance) and "body: creature: own=..." on both;
         * the host's "body: puppet of <id>" line names the species its game built the guest's puppet
           from, and it is the species the guest's own trace line reports;
         * the versions of the partner are in the log (CAP_VERSION).
  Size: the log grows by less than 30 KB a minute per game at log_detail=1 (well under 2 MB an hour).

  coop_trace_log.py [--cell-seconds 40] [--creature-seconds 40]
"""
import argparse
import os
import re
import time

import lane
from twoinst import Checks, start


def tail_from(inst, off):
    with open(lane.game_log(inst), 'rb') as f:
        f.seek(off)
        return f.read().decode('utf-8', 'replace')


def size(inst):
    try:
        return os.path.getsize(lane.game_log(inst))
    except OSError:
        return 0


def lines(text, kind):
    return [l for l in text.splitlines() if f'coop: {kind}: ' in l]


def nearest(con, kinds=('plant',)):
    out = con.call('coop.pool see 80')['out']
    for m in re.finditer(r'see (c\d+) (\S+) \S+ \S+ (\S+)', out):
        if m.group(2) in kinds:
            return m.group(1)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--cell-seconds', type=int, default=40)
    ap.add_argument('--creature-seconds', type=int, default=40)
    a = ap.parse_args()
    ck = Checks()

    # ---- Cell ----
    host, guest = start('cell')
    for c in (host, guest):
        c.call('coop.log detail 1')
    ho, go, t0 = size(lane.HOST), size(lane.GUEST), time.time()
    gid = re.search(r'client id=(\d+)', guest.call('coop.status')['out'])
    gid = gid.group(1) if gid else '1'
    eaten = []
    for _ in range(3):  # the guest eats a few plants of the host's pool (the herbivore fixture)
        k = nearest(guest, ('plant', 'meat'))
        if not k:
            break
        r = guest.call(f'coop.pool eat {k}')['out']
        print(f'  guest eats {k}: {r}')
        if 'points=' in r:
            eaten.append(k)
        time.sleep(1.5)
    k = nearest(host, ('plant', 'meat'))
    if k:
        print(f'  host eats {k}: {host.call(f"coop.pool eat {k}")["out"]}')
    time.sleep(max(12, a.cell_seconds - (time.time() - t0)))
    hl, gl = tail_from(lane.HOST, ho), tail_from(lane.GUEST, go)
    cell_min = (time.time() - t0) / 60
    sent = set(re.findall(r'eat: sent gone (c\d+)', gl))
    got = set(re.findall(r'eat: from ' + gid + r' gone (c\d+) .*: applied', hl))
    print(f'  guest sent {sorted(sent)}, host applied {sorted(got)}')
    ck(eaten, f'the guest ate {len(eaten)} plant(s) of the host\'s pool')
    ck(sent & got, f'a guest eat is in both logs with the same object ({sorted(sent & got)})')
    ck(any('own cell ate' in l for l in lines(hl, 'eat')), 'the host\'s own eat is in its log')
    for name, text in (('host', hl), ('guest', gl)):
        tr = lines(text, 'trace')
        main_lines = [l for l in tr if re.search(r'trace: (host|guest) id=\d+ players=2 Cell world=[0-9A-F]{8} .*pos=\(', l)]
        partner = [l for l in tr if re.search(r'partner \d+ .* Cell .*same_place=1 puppet=shown', l)]
        cl = lines(text, 'cell')
        ck(len(main_lines) >= 2, f'{name}: periodic trace lines in Cell ({len(main_lines)}): '
           + (main_lines[-1].split('trace: ', 1)[1][:150] if main_lines else '-'))
        ck(partner, f'{name}: a partner line with the puppet shown ({len(partner)})')
        ck(cl and re.search(r'diet=(herb|carn|omni|none) .*screen: plants=\d+', cl[-1]),
           f'{name}: a cell line with the diet and the food on screen: ' + (cl[-1].split('cell: ', 1)[1][:160] if cl else '-'))
        ck(any('body: puppet of' in l and 'cell model' in l for l in text.splitlines()),
           f'{name}: a body line for the partner\'s cell')
    gcl = lines(gl, 'cell')
    ck(gcl and re.search(r'host d=\d+', gcl[-1]), 'guest: its cell line has the distance to the host')
    cell_rate = [(len(t.encode()) / 1024) / cell_min for t in (hl, gl)]

    # ---- Creature ----
    host, guest = start('creature')
    for c in (host, guest):
        c.call('coop.log detail 1')
    ho, go, t0 = size(lane.HOST), size(lane.GUEST), time.time()
    time.sleep(a.creature_seconds)
    host.call('coop.log trace')
    guest.call('coop.log trace')
    time.sleep(2)
    hl, gl = tail_from(lane.HOST, ho), tail_from(lane.GUEST, go)
    # the whole log of this start too: the puppet's body line comes when the guest appears
    hfull, gfull = tail_from(lane.HOST, 0), tail_from(lane.GUEST, 0)
    cr_min = (time.time() - t0) / 60
    for name, text in (('host', hl), ('guest', gl)):
        cl = lines(text, 'creature')
        bl = [l for l in lines(text, 'body') if 'creature: own=' in l]
        ck(cl and 'npcs' in cl[-1] and ('nest d=' in cl[-1] or 'no nest' in cl[-1]),
           f'{name}: creature lines (NPCs, nest): ' + (cl[-1].split('creature: ', 1)[1] if cl else '-'))
        ck(bl, f'{name}: the own body line: ' + (bl[-1].split('body: ', 1)[1][:170] if bl else '-'))
    gtr = [l for l in lines(gl, 'trace') if re.search(r'trace: guest id=\d+ .*Creature', l)]
    gsp = re.search(r'species=([0-9A-F]{8})', gtr[-1]) if gtr else None
    pup = [re.search(r'puppet of (\d+) .*species ([0-9A-F]{8}) here; its creation here (\S+), its own game reports ([0-9A-F]{8})', l)
           for l in hfull.splitlines() if 'body: puppet of' in l and 'species' in l]
    pup = [m for m in pup if m]
    print(f'  guest reports species {gsp.group(1) if gsp else "-"}; host puppet lines: '
          + '; '.join(f'{m.group(1)}: {m.group(2)} (creation {m.group(3)}, reported {m.group(4)})' for m in pup))
    ck(pup and gsp and pup[-1].group(4) == gsp.group(1),
       'the host\'s puppet line names the species the guest\'s game reports')
    ck(pup and pup[-1].group(3) != '-' and pup[-1].group(2) == pup[-1].group(3),
       'the host built the guest\'s puppet from the guest\'s creation')
    ck(re.search(r'player \d+ .*: coop \S+, SporeKit', hfull) and re.search(r'player 0 .*: coop \S+, SporeKit', gfull),
       'both logs name the partner\'s versions')
    cr_rate = [(len(t.encode()) / 1024) / cr_min for t in (hl, gl)]
    print(f'  log growth KB/min: cell host {cell_rate[0]:.1f} guest {cell_rate[1]:.1f}; '
          f'creature host {cr_rate[0]:.1f} guest {cr_rate[1]:.1f}')
    worst = max(cell_rate + cr_rate)
    ck(worst < 30, f'the log grows by less than 30 KB a minute ({worst:.1f} KB/min = {worst * 60 / 1024:.1f} MB/h)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
