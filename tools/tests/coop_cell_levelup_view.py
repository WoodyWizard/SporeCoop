#!/usr/bin/env python3
"""BUG-0635: Cell: after a level-up the cells on the guest's screen vanish and others appear.

A level change runs RemoveCellsOfOldScale 0xE7CD10 in each game: every cell outside that game's own
visible box is destroyed and the tiles are populated again (docs/re/cell.md#cell-levelup-view). A single
player keeps what is on its screen. In co-op the guest's cells are the host's: the host's wipe kept only
the cells in the host's view, so a guest away from the host saw its whole screen replaced.

Two games in the Cell fixture, the guest 30 m from the host (outside the host's view, inside the tiles
the host populates around it). Each round the host changes the shared food by +150 / -150 (both games
change level three times: one change of the world scale); before and 5 s after, each game's own screen is read (`coop.pool see`: the cells the game
draws inside the window, by host index) and 5 s after the change which of them still exist in that game
(`coop.pool alive`). Checked in the guest's game: the share of the cells on its screen that are gone is
about what the host's game removes from its own screen, not the whole screen.

  coop_cell_levelup_view.py [--rounds 6]   (SPORE_FIX_OFF=cell.levelup_keep: fails)
"""
import argparse
import re
import time

from twoinst import Checks, keyframe, start


def screen(con):
    out = con.call('coop.pool see 400')['out']
    return {l.split()[1] for l in out.splitlines() if l.startswith('see ')}


def alive(con, keys):
    if not keys:
        return set()
    out = con.call('coop.pool alive ' + ','.join(sorted(keys)))['out']
    return set(out.split()[1:])


def once(con):
    out = con.call('coop.pool')['out']
    return {k: int(v) for k, v in re.findall(r'\b(wipe_calls|wipe_kept|wipe_destroyed|wipe_near_lost)=(\d+)', out)}


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
    # once, then both stay (a teleport each round, right before a lab hitch, left the host's picture of the guest
    # 30 m behind for two trace lines: the monitor's drawn gate, lab 20261106-223318)
    guest.call('coop.pool tp 30 0')
    time.sleep(10)  # the guest's mirrors around its new place, the host's picture of it there
    keyframe('before', 'the guest 30 m from the host: each screen shows its own cell with the cells round it, the '
                       'partner off screen or at the edge, no white squares')
    for i in range(a.rounds):
        time.sleep(3)
        s0 = {'host': screen(host), 'guest': screen(guest)}
        w0 = once(host)
        host.call(f'coop.pool food {150 if i % 2 == 0 else -150}')  # three levels: one change of the world scale
        time.sleep(5)
        s1 = {'host': alive(host, s0['host']), 'guest': alive(guest, s0['guest'])}  # still in that game's world
        n1 = {'host': screen(host), 'guest': screen(guest)}
        w1 = once(host)
        parts = []
        for who in ('host', 'guest'):
            if len(s0[who]) < 3:
                parts.append(f'{who}: {len(s0[who])} cells on screen (too few)')
                continue
            gone = len(s0[who] - s1[who]) / len(s0[who])
            lost[who].append(gone)
            parts.append(f'{who}: {len(s0[who])} on screen, {len(s0[who] - s1[who])} gone ({gone:.0%}), {len(n1[who] - s0[who])} new on screen')
        if i < 2:
            keyframe(f'change{i + 1}', 'right after a level change ({}): the cells on the guest\'s screen are still there '
                                       'and drawn at the new scale, as on the host\'s'.format('up' if i % 2 == 0 else 'down'))
        print(f'  change {i + 1} ({"up" if i % 2 == 0 else "down"}): ' + '; '.join(parts) +
              f' | host wipes {w1.get("wipe_calls", 0) - w0.get("wipe_calls", 0)}, kept {w1.get("wipe_kept", 0) - w0.get("wipe_kept", 0)}, '
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
