#!/usr/bin/env python3
"""BUG-1008: Cell: a guest's mouth chunk that kills a host NPC was credited to the host's own cell.

Audit 0.7.14 (WLD-20). A guest's lethal hit on a host NPC (MSG_CELL_HIT; a mouth's last chunk is type 6,
EatChunk's, which skips the game's Bite path) was applied on the host with KillCell(victim, the HOST's
avatar, ...): KillCell's species alert (0xE71F00) turned the victim's kind on the host's cell, not on the
guest's puppet that killed it.

Two games in the Cell fixture. The guest picks a host creature, the host puts 4 cells of its species
3 m round it (`coop.pool herd`) and sets its hp to 2; the guest sends a type 6 hit of 6 (`coop.pool sendhit`:
the message its mouth sends), after giving the kind a death alert radius (`npcflag alert`: the fixture's level-1
kinds have none, KillCell's alert 0xE71F00 then does nothing). Checked in the HOST's game: the victim dies, and the flee / chase targets of
its kind (the game's +0x220 / +0x22C, `coop.pool targets`) that the kill set point at the guest's puppet,
none at the host's own cell.

  coop_cell_chunk_killer.py   (SPORE_FIX_OFF=cell.chunk_killer: the host's cell is the target, it FAILS)
"""
import re
import time

from twoinst import Checks, start


def targets(host, idx):
    out = host.call('coop.pool targets ' + ','.join(idx))['out']
    rows = {m.group(1): (int(m.group(2)), int(m.group(3))) for m in re.finditer(r'target (\d+) flee=(\d+) flee_t=\S+ chase=(\d+)', out)}
    av = int(re.search(r'avatar=(\d+)', out).group(1))
    pups = {int(x.split(':')[1]) for x in re.findall(r'\d+:\d+', out.split('puppets=')[-1])}
    return rows, av, pups, out


def main():
    host, guest = start('cell')
    ck = Checks()
    time.sleep(3)
    host.call('coop.pool invulnfor 600')
    guest.call('coop.pool invulnfor 600')
    best = None
    for attempt in range(6):
        key, r = None, ''
        for _ in range(10):
            r = guest.call('coop.pool kill - hit')['out']
            m = re.match(r'bit (c\d+) ', r)
            if m and 'dying' not in r:
                key = m.group(1)
                break
            time.sleep(1)
        if not key:
            continue
        h = host.call(f'coop.pool herd {key} 4')['out'].strip()
        herd = re.findall(r'\d+', h.split('moved:')[-1]) if 'moved:' in h else []
        print(f'  {key}: {h}', flush=True)
        if len(herd) >= 2:
            best = (key, herd)
            break
        time.sleep(2)
    if not best:
        print('RESULT: SKIP (no host creature with two of its kind in the pool)')
        return 0
    key, herd = best
    rows0, av, pups, _ = targets(host, herd)
    guest.call(f'coop.pool swimto {key} tp')
    time.sleep(2)
    host.call(f'coop.pool herd {key} 4')
    print('  ' + host.call(f'coop.pool npcflag {key} hp 2')['out'].strip(), flush=True)
    # the fixture's level-1 kinds have no death alert (AI block +0x2C = 0): give this one a radius, as bigger kinds have
    print('  ' + host.call(f'coop.pool npcflag {key} alert 60')['out'].strip(), flush=True)
    print('  guest: ' + guest.call(f'coop.pool sendhit {key} 6 6')['out'].strip(), flush=True)
    seen_pup, seen_host, out = set(), set(), ''
    t0 = time.time()
    while time.time() - t0 < 2.0:
        rows, av, pups, out = targets(host, herd)
        for i, (flee, chase) in rows.items():
            if (flee in pups and flee != rows0.get(i, (0, 0))[0]) or (chase in pups and chase != rows0.get(i, (0, 0))[1]):
                seen_pup.add(i)
            if (flee == av and flee != rows0.get(i, (0, 0))[0]) or (chase == av and chase != rows0.get(i, (0, 0))[1]):
                seen_host.add(i)
        time.sleep(0.1)
    st = host.call(f'coop.pool cell {key}')['out'].strip()
    print(f'  host: {st[:160]}\n  host targets: {out.strip()}', flush=True)
    dead = 'no such cell' in st or 'dying=1' in st or 'dead=1' in st or re.search(r'\bhp=0\b', st) is not None
    ck(dead, f'the guest\'s chunk killed {key} in the host\'s game')
    if not seen_pup and not seen_host:
        print('RESULT: SKIP (the kill set no flee / chase target of its kind)')
        return 0
    ck(seen_pup and not seen_host, f'its kind turns on the guest\'s puppet (cells {sorted(seen_pup)}), not on the host\'s '
                                    f'cell (cells {sorted(seen_host)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
