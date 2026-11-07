#!/usr/bin/env python3
"""BUG-0635: Cell: after a level-up the cells on the guest's screen vanish and others appear.

A level change runs RemoveCellsOfOldScale 0xE7CD10 in each game: every cell outside that game's own
visible box is destroyed and the tiles are populated again (docs/re/cell.md#cell-levelup-view). A single
player keeps what is on its screen. In co-op the guest's cells are the host's: the host's wipe kept only
the cells in the host's view, so a guest away from the host saw its whole screen replaced.

Two games in the Cell fixture, the guest 30 m from the host (outside the host's view, inside the tiles
the host populates around it). Each round the host changes the shared food by +50 / -50 (both games
change level); before and 5 s after, each game's own screen is read (`coop.pool see`: the cells the game
draws inside the window, by host index). Checked in the guest's game: the share of the cells on its screen
that are gone after the change is about what the host loses from its own screen (cells swimming out of the
window, eaten), not the whole screen.

  coop_cell_levelup_view.py [--rounds 6]   (SPORE_FIX_OFF=cell.levelup_keep: fails)
"""
import argparse
import re
import time

from twoinst import Checks, start


def screen(con):
    out = con.call('coop.pool see 400')['out']
    return {l.split()[1] for l in out.splitlines() if l.startswith('see ')}


def once(con):
    out = con.call('coop.pool')['out']
    return {k: int(v) for k, v in re.findall(r'\b(wipe_kept|wipe_destroyed|wipe_near_lost)=(\d+)', out)}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=6)
    a = ap.parse_args()
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)
    host.call('coop.pool invulnfor 600')
    guest.call('coop.pool invulnfor 600')
    lost = {'host': [], 'guest': []}
    for i in range(a.rounds):
        guest.call('coop.pool tp 30 0')
        time.sleep(5)  # the guest's mirrors around its new place
        s0 = {'host': screen(host), 'guest': screen(guest)}
        w0 = once(host)
        host.call(f'coop.pool food {50 if i % 2 == 0 else -50}')
        time.sleep(5)
        s1 = {'host': screen(host), 'guest': screen(guest)}
        w1 = once(host)
        parts = []
        for who in ('host', 'guest'):
            if len(s0[who]) < 3:
                parts.append(f'{who}: {len(s0[who])} cells on screen (too few)')
                continue
            gone = len(s0[who] - s1[who]) / len(s0[who])
            lost[who].append(gone)
            parts.append(f'{who}: {len(s0[who])} on screen, {len(s0[who] - s1[who])} gone ({gone:.0%}), {len(s1[who] - s0[who])} new')
        print(f'  change {i + 1} ({"up" if i % 2 == 0 else "down"}): ' + '; '.join(parts) +
              f' | host wipe kept {w1.get("wipe_kept", 0) - w0.get("wipe_kept", 0)}, '
              f'destroyed {w1.get("wipe_destroyed", 0) - w0.get("wipe_destroyed", 0)} '
              f'(on the partner\'s screen {w1.get("wipe_near_lost", 0) - w0.get("wipe_near_lost", 0)})', flush=True)
    ck(len(lost['guest']) >= a.rounds // 2, f'the guest had cells on its screen in {len(lost["guest"])} of {a.rounds} changes')
    g = sum(lost['guest']) / max(1, len(lost['guest']))
    h = sum(lost['host']) / max(1, len(lost['host']))
    ck(g <= max(0.35, h + 0.2), f'a level change kept the guest\'s screen as the host\'s game keeps its own '
                                f'(guest lost {g:.0%} of its screen on average, host {h:.0%})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
