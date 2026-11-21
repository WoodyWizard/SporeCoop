#!/usr/bin/env python3
"""BUG-0637: Cell: after the host changes its own cell at level 10 (and a guest rejoined before), are there
cells in the host's world that do not belong to the level?

Two games in the Cell fixture. The host's shared food goes to the player's report (924: level 10). The host's
pool is listed by creature model and level (`coop.pool species`) a while after the level change (the
level's own population), then the host's own cell is changed with the co-op window's arrows
(`coop.creations choose cell`: a premade cell, then the first one again, as the window's arrows do: the level is
rebuilt each time) and listed again.
Checked in the host's game: after the rebuild no creature of a level outside the level's range (IsTileActive:
the level base and the next five), no creature model the level did not have before other than the player's own
species (the game populates NPCs of the player's species, also in a single player's game), and no NPC of the
previous own cell is left.
0.7.16 (BUG-1257): the wild kin near a partner are of that partner's own cell, so a cell a partner plays NOW (its
player key in the host's game, `coop.pool kin 0`) is allowed in both of the last two checks. The guest of this test has
no cell of its own: it follows the host's change within ~0.5 s (BUG-0508), so the previous cell stays forbidden. Kin
of the cell it was leaving, made by the host's level rebuild before its follow arrived, are BUG-1333 (final check
20261119-214823: 4DA56CA5 x5 after the change to BAF1AD40).

  coop_cell_own_level10.py [--food 924]
The pool is listed twice after each change: at once when the guest's follow reached the host (BUG-1333: kin of the
previous cell made in that window are still there; inferred: the twin of 20261120-130608 passed because those near the
guest's old place were culled before the 8 s listing) and 8 s later (as before).
FAIL twin: SPORE_FIX_OFF=cell.kin_follow (kin of the previous own cell near the guest after a change; the host's
rebuild must populate near the guest's centre in the window: the printed `kin filter since the change: partner+N`
says whether it did).
"""
import argparse
import re
import time

from twoinst import Checks, start


def wait_until(cond, secs, step=0.2):
    t0 = time.time()
    while time.time() - t0 < secs:
        if cond():
            return True
        time.sleep(step)
    return False


def species(con):
    out = con.call('coop.pool species')['out']
    own = re.search(r'own model=([0-9A-F]+) level=(-?\d+) food_level=(-?\d+)', out)
    m = {k: (int(n), [int(x) for x in lv.split(',') if x]) for k, n, lv in re.findall(r'model ([0-9A-F]+) n=(\d+) levels=(\S*)', out)}
    return (own.group(1), int(own.group(2)), int(own.group(3))) if own else ('?', -1, -1), m


def partner_cells(host):
    """the cells the partners play now, as the host's game has them (`coop.pool kin 0`: `kin player <id> cell=`)"""
    out = host.call('coop.pool kin 0')['out']
    return {m.group(2) for m in re.finditer(r'^kin player (\d+) cell=([0-9A-F]{8})', out, re.M)
            if '(me)' not in m.group(0) and m.group(2) != '00000000'}


def kin_counts(host):
    """the host's kin filter counters (`coop.pool kin 0`, first line: own= partner= follow= ...)"""
    line = host.call('coop.pool kin 0')['out'].splitlines()[:1]
    return dict((k, int(v)) for k, v in re.findall(r'\b(own|partner|no_key|unknown|one_mode|follow)=(\d+)', line[0])) if line else {}


def show(name, sp):
    own, m = sp
    print(f'  {name}: own {own[0]} level {own[1]} (food level {own[2]}); ' +
          ', '.join(f'{k} x{n} L{"/".join(map(str, lv))}' for k, (n, lv) in sorted(m.items())), flush=True)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--food', type=int, default=924)
    a = ap.parse_args()
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)
    host.call('coop.pool invulnfor 900')
    guest.call('coop.pool invulnfor 900')
    for step in range(0, a.food, 100):  # level by level, as play does
        host.call(f'coop.pool food {min(100, a.food - step)}')
        time.sleep(3)
    time.sleep(8)
    print('  host creations:', host.call('coop.creations')['out'].strip().replace('\n', ' | ')[:400], flush=True)
    base = species(host)
    show('host before', base)
    show('guest before', species(guest))
    seen_models = set(base[1])
    gown = species(guest)[0][0]
    rows = []
    orig = base[0][0]
    for cmd in ('choose cell BAF1AD40', f'choose cell {orig}'):  # a premade cell (the test profiles have no own ones), then back
        k0 = kin_counts(host)
        r = host.call(f'coop.creations {cmd}')['out'].strip()
        print(f'  host: coop.creations {cmd}: {r[:160]}', flush=True)
        # BUG-1333 twin (lab 20261120-130608 passed with cell.kin_follow off). Inferred: the kin the host's rebuild makes
        # near the guest before its follow arrives (0.5-1.4 s) are culled when they lie where the guest's cell was before ITS
        # rebuild (at level 10 the host's rebuild puts the host at (100 100), the guest's cell stays near (0 0) until
        # its own); only those near the host stay for the 8 s listing. So the pool is listed also at once when the
        # guest's follow is there, and the kin counters show whether the filter made kin of a partner in the window
        new = cmd.split()[-1]
        wait_until(lambda: partner_cells(host) == {new}, 10)
        at = species(host)
        show(f'host when the guest followed {cmd}', at)
        k1 = kin_counts(host)
        print(f'  kin filter since the change: ' + ' '.join(f'{k}+{k1.get(k, 0) - k0.get(k, 0)}' for k in sorted(k1)), flush=True)
        rows.append((cmd + ' (at the follow)', at, {new}))
        time.sleep(8)
        sp = species(host)
        show(f'host after {cmd}', sp)
        show(f'guest after {cmd}', species(guest))
        pc = partner_cells(host)
        print(f'  partners\' cells now (host\'s game): {sorted(pc)}', flush=True)
        rows.append((cmd, sp, pc))
    prev = orig
    for cmd, (own, m), pc in rows:
        if cmd.endswith('(at the follow)'):  # the listing right after the follow (BUG-1333): the previous cell only
            ck(prev == own[0] or prev not in m, f'  {cmd}: no NPC of the previous own cell {prev} (own species now '
               f'{own[0]} x{m.get(own[0], (0,))[0]}; {prev} x{m.get(prev, (0,))[0]}; the guest followed to {sorted(pc)})')
            continue
        lv0 = own[2]
        new = [k for k in m if k not in seen_models and k != own[0] and k not in pc]
        odd = [(k, lv) for k, (n, lv) in m.items() if any(l < lv0 - 1 or l > lv0 + 5 for l in lv if l >= 0)]
        ck(not odd, f'after {cmd}: every creature of the host\'s pool belongs to the level (food level {lv0}; outside: {odd})')
        # the game populates NPCs of the player's own species (8 of the fixture's own cell before any change, as in a
        # single player's game): after a change they are of the new species, none of the previous one is left
        # BUG-1257: a partner's own cell is its kin's (allowed); one no player plays any more is not (BUG-1333)
        ck(prev == own[0] or prev not in m or prev in pc, f'  no NPC of the previous own cell {prev} is left (own species '
           f'now {own[0]} x{m.get(own[0], (0,))[0]}; {prev} x{m.get(prev, (0,))[0]}; partners play {sorted(pc)})')
        ck(not new, f'  no creature model the level did not have before, other than the own species and the partners\' ({new})')
        prev = own[0]
    host.call('coop.creations unchoose cell')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
