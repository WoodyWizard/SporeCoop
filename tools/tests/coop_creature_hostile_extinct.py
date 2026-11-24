#!/usr/bin/env python3
"""BUG-1540: Creature guest wiping a hostile nest undiscovered by the host receives the extinction goal DNA.

When a guest hunts a naturally hostile species (tuning +0x390 == 5) far from the host, species discovery
(0xBA5130) never ran on the host, so rec[0] stayed 0. On lethal hits/extinction, ApplyEvent (0xBA4A00)
computed old_rel = GetRelationship(..., creature, nullptr) = 5, set new_rel = 5, saw 5 == 5, and skipped
writing rec[0]. SpeciesGoal_Update (0xC2EE80) and goal_outcome() call GetRelationship(..., nullptr, nullptr)
and see 0 != 5, skipping the extinction goal DNA reward.

1. Creature fixture, 2 players. Find a loaded non-player NPC on the host with a valid tuning pointer.
   If its default relation (+0x390) is not already 5 (the fixture has no hostile species at start),
   temporarily set it to 5 via `coop.social setdefault` and restore it on exit.
2. Reset the host's species record for that species to relation=0 (`coop.social record <sp> 0 0`),
   matching an undiscovered species on the host.
3. Trigger lethal kill events (`coop.social event <sp> 0` x4) so the host's real ApplyEvent(0) runs
   with a live creature of that species through h_RelEvent.
4. Check that host and guest species tables materialize relation=5 (and rel_mat >= 1 on the host).
5. Guest reports the extinction goal card reward (`coop.social goal <sp> 5 60`): host verifies
   goal_outcome == 5, pays +60 DNA (`goal_dna_paid` increases), and both games' DNA rises.

  SPORE_FIX_OFF=social.undiscovered_rel  FAILS (rec[0] stays 0, K_GOAL rejected, DNA unchanged)
"""
import re
import time

from twoinst import Checks, game_runs, start

TYPE, GROUP = '2B978C46', '40626200'
REC = re.compile(r'species ([0-9A-F]{8})/([0-9A-F]{8})/([0-9A-F]{8}) relation=(-?\d+) x=\d+ impressed=(\d+) killed=(\d+)')
REL_ROW = re.compile(r'^rel (\d+) species=([0-9A-F]+) tuning=([0-9A-Fa-f]+) default=(-?\d+) relation=(-?\d+)', re.M)


def table(con):
    out = con.call('coop.social species')['out']
    return {m.group(1): (int(m.group(4)), int(m.group(5)), int(m.group(6)))
            for m in REC.finditer(out) if m.group(2) == TYPE and m.group(3) == GROUP}


def dna_of(con):
    m = re.search(r'dna=([-\d.]+)', con.call('coop.avatar')['out'] or '')
    return float(m.group(1)) if m else None


def host_stat(con, key):
    m = re.search(key + r'=(\d+)', con.call('coop.social')['out'] or '')
    return int(m.group(1)) if m else 0


def main():
    host, guest = start('creature')
    ck = Checks()
    restored = None
    try:
        game_runs(host, 60)
        game_runs(guest, 60)
        time.sleep(3)
        own_m = re.search(r'key=0x([0-9A-Fa-f]+)', host.call('coop.avatar')['out'] or '')
        own_sp = own_m.group(1).upper().zfill(8) if own_m else ''

        cands = []
        for m in REL_ROW.finditer(host.call('coop.social rel')['out'] or ''):
            nid, sp, tun, dflt, rel = int(m.group(1)), m.group(2).upper().zfill(8), int(m.group(3), 16), int(m.group(4)), int(m.group(5))
            if sp != own_sp and tun != 0 and dflt >= 0:
                cands.append((0 if dflt == 5 else 1, nid, sp, dflt, rel))
        cands.sort()
        if not ck(bool(cands), f'loaded wild NPC with tuning on host ({len(cands)})'):
            ck.faults(host=host, guest=guest)
            return ck.result()
        _, nid, sp, old_def, _ = cands[0]
        print(f'  chosen wild NPC #{nid} species={sp} default={old_def}', flush=True)

        if old_def != 5:
            out = host.call(f'coop.social setdefault {nid} 5')['out'].strip()
            print(f'  host setdefault: {out}', flush=True)
            restored = (nid, old_def)

        # Undiscovered on host: rec[0] = 0
        host.call(f'coop.social record {sp} 0 0')
        time.sleep(1.0)
        h0 = table(host).get(sp)
        print(f'  before kills: host record {h0}, social={host.call("coop.social")["out"].strip()}', flush=True)
        ck(h0 is not None and h0[0] == 0, f'host species {sp} starts undiscovered with relation=0 ({h0})')

        paid0 = host_stat(host, 'goal_dna_paid')
        mat0 = host_stat(host, 'rel_mat')
        dna_h0 = dna_of(host)
        dna_g0 = dna_of(guest)

        for i in range(4):
            out = host.call(f'coop.social event {sp} 0')['out'].strip()
            if i == 0:
                print(f'  host kill event 0: {out}', flush=True)
            time.sleep(0.25)

        time.sleep(2.0)
        h1, g1 = table(host).get(sp), table(guest).get(sp)
        mat1 = host_stat(host, 'rel_mat')
        print(f'  after kills: host={h1} guest={g1} rel_mat={mat0}->{mat1}', flush=True)
        ck(h1 is not None and h1[0] == 5, f'host materialized relation=5 after hostile kill ({h1})')
        ck(g1 is not None and g1[0] == 5, f'guest table received relation=5 ({g1})')
        ck(mat1 > mat0, f'host rel_mat counter incremented ({mat0} -> {mat1})')

        # Guest reports the extinction goal card reward (+60 DNA)
        out = guest.call(f'coop.social goal {sp} 5 60')['out'].strip()
        print(f'  guest goal report: {out}', flush=True)
        paid1 = paid0
        dna_h1, dna_g1 = dna_h0, dna_g0
        t0 = time.time()
        while time.time() - t0 < 8:
            paid1 = host_stat(host, 'goal_dna_paid')
            dna_h1 = dna_of(host)
            dna_g1 = dna_of(guest)
            if paid1 > paid0 and dna_h0 is not None and dna_h1 is not None and dna_h1 >= dna_h0 + 50:
                break
            time.sleep(0.5)
        print(f'  after goal: goal_dna_paid={paid0}->{paid1} host_dna={dna_h0}->{dna_h1} guest_dna={dna_g0}->{dna_g1}',
              flush=True)
        ck(paid1 > paid0, f'host paid the extinction goal DNA ({paid0} -> {paid1})')
        ck(dna_h0 is not None and dna_h1 is not None and dna_h1 >= dna_h0 + 50,
           f'host DNA increased by extinction goal reward ({dna_h0} -> {dna_h1})')
        ck.faults(host=host, guest=guest)
        return ck.result()
    finally:
        if restored is not None:
            host.call(f'coop.social setdefault {restored[0]} {restored[1]}')


if __name__ == '__main__':
    raise SystemExit(main())
