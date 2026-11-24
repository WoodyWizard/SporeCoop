#!/usr/bin/env python3
"""Cell: the audit items of 0.7.17 (BUG-1510..1514; CELL-8, 9, 11, 13, 14 of the external audit of 0.7.15.5), one part
each (the Cell fixture, two games; --part shadow three guests). Each part is judged on the game's own fields of the
game that shows the effect; each has a twin with its switch off that must FAIL.

--part breaking (BUG-1511, cell.breaking_dying): the host's lethal bite breaks an NPC creature (BreakOrSwallow, its
  states 7/8/9 for 0.1-0.3 s before event 5 removes it) and the guest's kill of the same creature arrives right after
  (`coop.pool sendact c<key> kill`). Control first: another creature of the same model broken by the host alone. The
  host's game drops no more meat for the creature killed twice than for the control (a second KillCell dropped a second
  loot), and the creature is gone.
--part scale (BUG-1512, cell.partner_scale): the guest 8 m from the host; the host's food goes up a level line
  (`coop.pool food 120`: its world rescales for ~3 s while the guest's has not yet). During the 5 s after it the host's
  distance to its partner (nearest_partner: the revive, the regen, the respawn spot; `coop.pool` partner_dist) agrees
  with the distance to the guest's cell as the host's game draws it (`coop.pool scalepos`) within 25 %.
--part own_cloud (BUG-1513, cell.own_cloud): friendly fire on; the GUEST's puppet has a poison part in the host's game
  (its clouds are made there and mirrored back to it); the host is 60 m away. The guest is put onto the mirrors of its
  own clouds (as its game shows them) for up to 40 s: its hp (+0x244) never goes down and it is never knocked out.
--part shrink (BUG-1514, cell.plant_shrink): the guest bites a plant of the host's pool once (`coop.pool eat c<key>`:
  the game's EatFood): within 3 s the HOST's plant is one level lower and smaller (`coop.pool cell c<key>` level=,
  size=), as the guest's mirror is.
--part shadow (BUG-1510, cell.shadow_empty): four games; three guests together 40 m from the host (one list holds the
  shared tiles, the others shadows of them); the host's population budget is set below its live cells
  (`coop.pool budget 1 1`) while the first guest swims away, then back to normal: the two guests that stayed still see
  the host's cells round them (>= 10 mirrors each, 15 s later) and the host's tile pass leaves no tile of theirs a
  shadow nobody populates (`coop.pool tilestat` starved back to 0).

  SPORE_LANE=2 python3 tools/tests/coop_cell_audit_0717.py --part breaking|scale|own_cloud|shrink|shadow
"""
import argparse
import math
import re
import time

import settings_lib as sl
from coop_cell_ko_food import plants, pool
from coop_cell_ko_poison import maker_pos, pos
from coop_cell_mate_copy import loot_near
from coop_cell_partner_poison import clouds_near
from coop_cell_puppet_scale import playing, wait
from harness import field
from twoinst import Checks, keyframe, start


def gid_of(guest):
    return (re.search(r'client id=(\d+)', guest.call('coop.status')['out']) or [None, '1'])[1]


def cell_line(con, key):
    out = con.call(f'coop.pool cell {key}')['out']
    lv, sz = field(out, r'\blevel=(-?\d+)'), field(out, r'\bsize=(-?[\d.]+)')
    pos = re.search(r'pos=\((-?[\d.]+) (-?[\d.]+)\)', out)
    return (int(lv) if lv else None, float(sz) if sz else None,
            (float(pos.group(1)), float(pos.group(2))) if pos else None, out.strip())


def audit(con):
    return next((x for x in con.call('coop.pool')['out'].splitlines() if x.startswith('cell_audit')), '')


def part_breaking(ck, host, guest):
    host.call('coop.pool invulnfor 120')
    guest.call('coop.pool tp 2 0')
    cands = []
    t0 = time.time()
    while time.time() - t0 < 20:
        cands = [m.group(1) for m in re.finditer(r'^see (c\d+) creature ', host.call('coop.pool see 80')['out'], re.M)][:6]
        if len(cands) >= 2:
            break
        if time.time() - t0 > 8 and len(cands) < 2:
            host.call('coop.pool food 60')
        time.sleep(2)
    if not ck(len(cands) >= 2, f'two NPC creatures near the host ({cands})'):
        return
    a, b = cands[0], cands[1]

    def lethal(key, also_guest):
        at = cell_line(host, key)[2]
        host.call(f'coop.pool cellhp {key} 1')
        out = host.call(f'coop.pool kill {key}')['out'].strip()
        if also_guest:
            out += ' | guest: ' + guest.call(f'coop.pool sendact {key} kill')['out'].strip()
        time.sleep(3)
        n = loot_near(host, at, 6.0, 10.0) if at else -1
        print(f'  {key} at {at}: {out[:160]}; meat near it {n}; now {cell_line(host, key)[3][:120]}', flush=True)
        return n, at
    l0, _ = lethal(a, False)
    l1, at1 = lethal(b, True)
    print('  host: ' + audit(host), flush=True)
    ck(l0 >= 0 and at1 is not None, 'both creatures were found and broken')
    ck(l1 <= max(l0, 1), f'the creature the guest killed while it broke dropped no second meat ({l1} against {l0} of one death)')
    ck('no such cell' in host.call(f'coop.pool cell {b}')['out'] or 'dead=1' in host.call(f'coop.pool cell {b}')['out'],
       'the creature is gone (or dead) in the host\'s game')


def part_scale(ck, host, guest):
    gid = gid_of(guest)
    host.call('coop.pool invulnfor 120')
    guest.call('coop.pool invulnfor 120')
    guest.call('coop.pool tp 8 0')
    time.sleep(4)
    print('  host: ' + host.call('coop.pool food 120')['out'].strip(), flush=True)
    worst, samples, t0 = 0.0, 0, time.time()
    while time.time() - t0 < 5:
        pd = field(host.call('coop.pool')['out'], r'partner_dist=(-?[\d.]+)')
        sp = host.call('coop.pool scalepos')['out']
        me = re.search(r'me=\((-?[\d.]+) (-?[\d.]+)\)', sp)
        p = re.search(rf'\bp{gid}=\((-?[\d.]+) (-?[\d.]+)\)', sp)
        if pd and me and p:
            d = math.dist((float(me.group(1)), float(me.group(2))), (float(p.group(1)), float(p.group(2))))
            if d > 1.0:
                worst = max(worst, abs(float(pd) - d) / d)
                samples += 1
        time.sleep(0.25)
    print(f'  host: {samples} samples, the largest gap between partner_dist and the drawn partner {worst * 100:.0f} %; '
          f'{audit(host)}', flush=True)
    ck(samples >= 8, f'the host measured its partner during its level change ({samples} samples)')
    ck(worst <= 0.25, f'the host\'s partner distance is the drawn partner\'s during the rescale (gap {worst * 100:.0f} % <= 25 %)')
    keyframe('scale', 'host: a level up, the guest\'s cell 8 m away drawn in the host\'s new scale')


def part_own_cloud(ck, host, guest):
    gid = gid_of(guest)
    if not ck(sl.force(host, [guest], 'all.friendly_fire', 'on'), 'friendly fire on for both'):
        return
    try:
        # Keep the host 10 m away (drawn so the puppet's poison step emits clouds, far enough that the host's own
        # clouds never touch the guest under friendly fire).
        host.call('coop.pool invulnfor 120')
        host.call('coop.pool tp 10 0')
        got = host.call(f'coop.pool borrow poison graft p{gid}')['out'].strip()
        print('  host: ' + got, flush=True)
        if not ck(got.startswith('borrowed'), 'the guest\'s puppet has the poison part in the host\'s game'):
            return
        time.sleep(3)
        hp0 = pool(guest)['hp']
        hp_min, downed, onto, used, t0 = hp0, 0, 0, set(), time.time()
        while time.time() - t0 < 40 and onto < 10:
            hp_host = maker_pos(guest, 'host')
            keys = []
            for k in clouds_near(guest):
                if k in used:
                    continue
                cp = pos(guest, k)
                if cp and (hp_host is None or math.dist(cp, hp_host) > 5.0):
                    keys.append(k)
            if keys:
                used.add(keys[0])
                if 'moved' in guest.call(f'coop.pool swimto {keys[0]} tp 0.2')['out']:
                    onto += 1
            for _ in range(4):
                time.sleep(0.25)
                p = pool(guest)
                if p['hp'] is not None and hp_min is not None:
                    hp_min = min(hp_min, p['hp'])
                downed = max(downed, p['downed'] or 0)
            host.call('coop.pool tp 10 0')
        print(f'  guest: onto {onto} of its own clouds, hp {hp0} -> min {hp_min}, knocked out {downed}; {audit(guest)}', flush=True)
        ck(onto >= 6, f'the guest was put onto its own clouds\' mirrors ({onto} >= 6)')
        ck(hp0 is not None and hp_min is not None and hp_min >= hp0 and not downed,
           f'its own poison never hurts the guest (hp {hp0} -> min {hp_min}, knocked out {downed})')
    finally:
        host.call(f'coop.pool borrow off x p{gid}')
        sl.force(host, [guest], 'all.friendly_fire', 'off')


def part_shrink(ck, host, guest):
    host.call('coop.pool invulnfor 120')
    guest.call('coop.pool invulnfor 120')
    # EatFood shrinks a plant one level in place only when (plant_level - 1) is still inside the current view's level
    # range (i.e. plant_level > own_level). Scan at the starting level (and nudge if needed) for a plant above own_level.
    ol_m = field(guest.call('coop.pool tokens')['out'], r'own_level=(-?\d+)')
    own_lv = int(ol_m) if ol_m is not None else 1
    keys = []
    t0 = time.time()
    while time.time() - t0 < 20:
        keys = plants(guest, n=24)
        lvs = [(k, cell_line(host, k)[0]) for k in keys]
        if any(lv is not None and lv > own_lv for _, lv in lvs):
            break
        guest.call('coop.pool tp 12 0')
        time.sleep(2.5)
    lvs = [(k, cell_line(host, k)[0]) for k in keys]
    print(f'  guest own_level={own_lv}, host plant levels={lvs[:10]}', flush=True)
    if not ck(keys, f'plants on the guest\'s screen ({keys})'):
        return
    done = None
    for k in keys:
        lv0, sz0, _, out0 = cell_line(host, k)
        if lv0 is None or lv0 <= own_lv:
            continue
        print('  guest: ' + guest.call(f'coop.pool eat {k}')['out'].strip(), flush=True)
        time.sleep(0.5)
        g_lv = cell_line(guest, k)[0]
        if g_lv is None or g_lv != lv0 - 1:  # (eaten whole, or not this plant's bite: the next one)
            continue
        ok = wait(lambda: (lambda c: c[0] == lv0 - 1 and c[1] is not None and sz0 is not None and c[1] < sz0)(cell_line(host, k)), 3)
        lv1, sz1, _, out1 = cell_line(host, k)
        done = (k, lv0, lv1, sz0, sz1, g_lv, bool(ok))
        print(f'  host {k}: level {lv0} -> {lv1}, size {sz0} -> {sz1}; the guest\'s mirror at level {g_lv}; {audit(guest)}',
              flush=True)
        break
    if not ck(done, 'the guest\'s bite shrank one of the host\'s plants in its own game (a shrink, not a whole meal)'):
        return
    k, lv0, lv1, sz0, sz1, g_lv, ok = done
    ck(ok, f'the host\'s plant {k} is one level lower and smaller too ({lv0} -> {lv1}, {sz0} -> {sz1})')
    keyframe('shrink', 'the plant the guest bit is the same smaller size on both screens')


def part_shadow(ck):
    from party import Party
    party = Party.start('cell', players=4)
    host, (bob, carol, dave) = party.host, party.guests[:3]
    for pl in party.all:
        pl.call('coop.pool invulnfor 600')
    for g in (bob, carol, dave):
        g.call('coop.pool tp 40 0')
    time.sleep(10)
    print('  host: ' + host.call('coop.pool budget 1 1').strip(), flush=True)
    bob.call('coop.pool tp -40 0')
    time.sleep(4)
    print('  host: ' + host.call('coop.pool budget').strip(), flush=True)
    time.sleep(15)
    st = host.call('coop.pool tilestat')
    print('  host: ' + st.strip()[:300] + ' | ' + audit(host.con), flush=True)
    for g in (carol, dave):
        n = g.call('coop.pool mirrors').count('mirror_local')
        ck(n >= 10, f'{g.name}\'s game shows the host\'s cells round it ({n} mirrors)')
    starved = field(st, r'\bstarved=(\d+)')
    ck(starved is None or int(starved) == 0, f'no tile of theirs waits starved in the host\'s game ({starved})')
    keyframe('shadow', 'carol and dave: food and creatures round them after bob left', players={pl.name: pl.con for pl in party.all})
    ck.faults(**{pl.name: pl.con for pl in party.all})


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--part', choices=('breaking', 'scale', 'own_cloud', 'shrink', 'shadow'), required=True)
    a = ap.parse_args()
    ck = Checks()
    if a.part == 'shadow':
        part_shadow(ck)
        return ck.result()
    host, guest = start('cell')
    ck(wait(lambda: playing(host) and playing(guest), 40), 'both games play (the hatch is over)')
    {'breaking': part_breaking, 'scale': part_scale, 'own_cloud': part_own_cloud, 'shrink': part_shrink}[a.part](ck, host, guest)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
