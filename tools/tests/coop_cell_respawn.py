#!/usr/bin/env python3
"""Cell stage: the game's own death and respawn inside a session (BUG-0221).

Since BUG-0131 a knocked-out partner is no helper, so with two players a lethal hit on the second
one runs the game's death whenever the first is still knocked out: PlayerDeath, RequestRespawn
(G+0x51D8 = 1) and RebuildLevel, which recreates the pool and the avatar. This test watches what
follows, in both games:

1. Player A is knocked out (`coop.pool selfhit 250` while B is up), then B takes a lethal bite: the
   game's death runs on B (dying, not knocked out).
2. Within 25 s B is alive again (hp > 0, not dying, no caption waiting) and A's knockout has ended
   (A respawns next to B after its 10 s).
3. Then, in the games' own state:
   * every mirror of the guest is a live cell in the host's pool (`coop.pool mirrormap` against the
     host's `coop.cell list`), and there are more than 5;
   * each game shows the other player's cell (its puppet, `puppet pos=` in coop.status) within a
     few metres of where that player's own game has it;
   * no faults, also 10 s later.
The pool header before and after (`coop.cell pool`: generation, live count, highWater, the avatar
index, G+0x51D8) and the destroy bookkeeping (puppet_saves, kept, `coop.pool why`) are printed for
docs/re/cell.md#cell-rebuilds-in-session.

  coop_cell_respawn.py          A = guest, B = host (the host's pool is rebuilt)
  coop_cell_respawn.py --swap   A = host, B = guest (the guest's pool is rebuilt)
"""
import re
import sys
import time

from twoinst import Checks, start


def num(out, key):
    m = re.search(r'\b' + key + r'=(-?\d+)', out)
    return int(m.group(1)) if m else None


def pool(con):
    return con.call('coop.pool')['out']


def own_pos(con):
    m = re.search(r'^avatar .*?pos=\((-?[\d.]+) (-?[\d.]+)\)', pool(con), re.MULTILINE)
    return (float(m.group(1)), float(m.group(2))) if m else None


def puppet_pos(con, rid):
    lines = con.call('coop.status')['out'].splitlines()
    for i, l in enumerate(lines):
        if l.startswith(f'remote {rid} ') and i + 1 < len(lines) and 'puppet pos=(' in lines[i + 1]:
            v = lines[i + 1].split('puppet pos=(')[1].split(')')[0].split()
            return float(v[0]), float(v[1])
    return None


def dist(a, b):
    return ((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2) ** 0.5


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def record(label, host, guest):
    for name, con in (('host', host), ('guest', guest)):
        p = con.call('coop.cell pool')['out'].strip()
        st = pool(con)
        book = ' '.join(re.findall(r'\b(?:puppet_saves|puppet_new|kept|created|destroyed|wipes|mirrors)=\d+', st))
        why = con.call('coop.pool why')['out'].replace('\n', ' | ').strip()
        print(f'  [{label}] {name}: {p}\n      {book} why: {why[:300]}')


def main():
    swap = '--swap' in sys.argv
    host, guest = start('cell')
    ck = Checks()
    a, b = (host, guest) if swap else (guest, host)
    an, bn = ('host', 'guest') if swap else ('guest', 'host')
    aid, bid = (0, 1) if swap else (1, 0)
    ck(wait(lambda: (num(pool(guest), 'mirrors') or 0) > 5, 30), 'the guest mirrors the host pool')
    ck(wait(lambda: puppet_pos(host, 1) and puppet_pos(guest, 0), 20), 'both show the other player')
    record('before', host, guest)
    # 1. A knocked out, then B's lethal bite
    r = a.call('coop.pool selfhit 250')['out']
    print(f'  {an}: {r}')
    ck(num(r, 'downed') == 1, f'the {an} is knocked out (its partner is up)')
    ck(wait(lambda: re.search(r'^ko\[%d\]=1' % aid, pool(b), re.MULTILINE), 3), f'the {bn} sees the {an} knocked out')
    r = b.call('coop.pool selfhit 250')['out']
    print(f'  {bn}: {r}')
    ck(num(r, 'downed') == 0 and (num(r, 'dying') == 1 or num(r, 'dead') == 1),
       f'the {bn}\'s lethal hit runs the game\'s death, its only partner is knocked out')
    saw_flag = wait(lambda: 'rebuild=1' in b.call('coop.cell pool')['out'], 6, 0.05)
    print(f'  G+0x51D8 seen set by a console poll: {bool(saw_flag)}')

    # 2. both playing again
    def b_alive():
        st = pool(b)
        return num(st, 'hp') and num(st, 'hp') > 0 and 'cine=0' in st and \
            num(b.call('coop.pool selfhit 0')['out'], 'dying') == 0
    t0 = time.time()
    back = wait(b_alive, 25)
    print(f'  {bn} alive again after {time.time() - t0:.1f} s')
    ck(back, f'the {bn} plays again after the game\'s death and respawn')
    up = wait(lambda: num(pool(a), 'downed') == 0, 20)
    ck(up, f'the {an}\'s knockout ends (it respawns)')
    time.sleep(3.0)
    record('after', host, guest)

    # 3. the pool and the players in both games
    def mirrors_ok():
        live = set(int(x, 16) for x in re.findall(r'idx=([0-9A-F]{8})', host.call('coop.cell list')['out']))
        mm = [int(h) for h in re.findall(r'^map (\d+) ', guest.call('coop.pool mirrormap')['out'], re.MULTILINE)]
        dead = [h for h in mm if h not in live]
        return (len(mm), dead)
    res = wait(lambda: (lambda r: r if r[0] > 5 and not r[1] else None)(mirrors_ok()), 15)
    n, dead = res or mirrors_ok()
    print(f'  guest mirrors {n}, not live on the host {len(dead)} {dead[:5]}')
    ck(res is not None, f'every mirror of the guest is a live host cell, more than 5 ({n}, {len(dead)} dead)')

    def near(con, rid, other):
        q, p = puppet_pos(con, rid), own_pos(other)
        return (q and p and dist(q, p) < 3.0), q, p
    for con, cn, rid, other, on in ((host, 'host', 1, guest, 'guest'), (guest, 'guest', 0, host, 'host')):
        r = wait(lambda: near(con, rid, other)[0], 10)
        _, q, p = near(con, rid, other)
        print(f'  {cn} shows the {on} at {q}, the {on}\'s game has it at {p}')
        ck(r, f'the {cn}\'s game shows the {on} where the {on} is ({q} vs {p})')
    time.sleep(10)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
