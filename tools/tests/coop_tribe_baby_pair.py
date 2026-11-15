#!/usr/bin/env python3
"""Tribe: a member the host buys at its hut is the host's, also when a guest buys one at the same time (BUG-0824).

A left click on the own hut costs food 10 and a baby is born 5-8 s later through the stage's message 0x63BBC42 (from
the hut effect's data). The world effects relay replays a player's hut effect in the other games, so every game's
message comes: the host's game makes a baby for the host's click and one for the guest's relayed effect, and the
guest's game would make one for the host's relayed effect too. Before the fix the host took any baby its game made
within 4 s of a guest's ask (CMD_BABY, BUG-0671) as the guest's, its own purchase too, and the guest asked for a
baby for the host's relayed effect as if it were its own purchase.

1. Two games on the tribe fixture; the host's tribe at level 2 (cap 12), food 60 on the host (shared).
2. Members are spawned until the last new one went to the host in the turn split: the next member that nobody
   claims goes to the guest.
3. Part A: the host's player clicks its hut; up to 20 s. PASS: one new member, the host's; the guest asked for none.
4. Part B: the host's player and the guest's player click the hut within 1 s; up to 25 s. PASS: two new members,
   one the host's and one the guest's (the owners in the host's split, the game's own member list).

SPORE_FIX_OFF=tribe.baby_pair: the host's purchase goes to the guest (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_tribe_baby_pair.py
"""
import re
import time

from twoinst import Checks, start


def player(con):
    """(members, tribe pointer) of the game's player tribe"""
    for ln in con.call('coop.tribe')['out'].splitlines():
        if '(player)' in ln:
            m = re.search(r': (\d+) members .*obj=(?:0x)?([0-9A-Fa-f]+)', ln)
            return (int(m.group(1)), int(m.group(2), 16)) if m else (-1, 0)
    return (-1, 0)


def owners(con):
    """{member id: owner} of the host's player tribe"""
    out, inside = {}, False
    for ln in con.call('coop.tribe')['out'].splitlines():
        if ln.startswith('tribe '):
            inside = '(player)' in ln
            continue
        m = re.match(r'\s+#(\d+) \S+ owner=(-?\d+)', ln)
        if m and inside:
            out[int(m.group(1))] = int(m.group(2))
    return out


def food(con):
    m = re.search(r'food=(-?[\d.]+)', con.call('coop.res')['out'])
    return float(m.group(1)) if m else None


def hut(con):
    for line in con.call('coop.targets objs')['out'].splitlines():
        f = line.split()
        if len(f) == 5 and f[0] == 'obj' and f[1].startswith('hut') and f[1].endswith('p'):
            return int(f[3]), int(f[4])
    return None


def baby(con):
    """the `baby ...` lines of coop.tribe anims (counters, the last call's probe)"""
    return ' | '.join(ln for ln in con.call('coop.tribe anims')['out'].splitlines() if ln.startswith('baby'))


def sent(con):
    m = re.search(r'baby fix=\d+ sent=(\d+)', baby(con))
    return int(m.group(1)) if m else -1


def set_food(host, amount):
    _, pt = player(host)
    f = food(host) or 0
    host.call(f'call SporeCoop::cTribe_AddFood thiscall 0x{pt:X} {int(amount - f)}')
    time.sleep(3)


def click(con, h):
    con.call(f'move {h[0]} {h[1]}')
    time.sleep(0.6)
    con.call(f'click {h[0]} {h[1]}')


def wait_new(host, guest, o0, want, secs):
    t0 = time.time()
    while time.time() - t0 < secs:
        time.sleep(1)
        new = {i: o for i, o in owners(host).items() if i not in o0}
        if len(new) >= want and player(guest)[0] == player(host)[0]:
            break
    time.sleep(5)  # a late second baby would show here
    return {i: o for i, o in owners(host).items() if i not in o0}


def next_is_guest(host):
    """spawn members until the last new one is the host's (the next unclaimed member is the guest's)"""
    for _ in range(3):
        o0 = owners(host)
        host.call('coop.tribe spawn 1')
        time.sleep(2.5)
        new = {i: o for i, o in owners(host).items() if i not in o0}
        print(f'  spawned: {new}', flush=True)
        if new and list(new.values())[-1] == 0:
            return True
    return False


def main():
    host, guest = start('tribe')
    ck = Checks()
    time.sleep(5)
    print(' ', host.call('coop.tribe level 2')['out'].strip(), flush=True)
    for c in (host, guest):
        print(' ', c.call('coop.fix list')['out'].count('tribe.baby_pair'), 'baby_pair switch', flush=True)

    # part A: only the host buys
    ck(next_is_guest(host), 'the next unclaimed member of the turn split is the guest\'s')
    set_food(host, 60)
    o0, s0 = owners(host), sent(guest)
    h = hut(host)
    ck(h is not None, f'the host\'s hut is on its screen ({h})')
    if not h:
        return ck.result()
    f0 = food(host)
    click(host, h)
    new = wait_new(host, guest, o0, 1, 20)
    print(f'  A: new members {new}, food {f0} -> {food(host)}', flush=True)
    print('  A host', baby(host), flush=True)
    print('  A guest', baby(guest), flush=True)
    ck(len(new) == 1, f'A: the host\'s click made one member ({new})')
    ck(len(new) == 1 and list(new.values())[0] == 0, f'A: the member the host bought is the host\'s ({new})')
    ck(sent(guest) == s0, f'A: the guest asked for no member of its own ({s0} -> {sent(guest)})')

    # part B: both buy within a second
    ck(next_is_guest(host), 'B: the next unclaimed member of the turn split is the guest\'s')
    set_food(host, 60)
    o0 = owners(host)
    hh, gh = hut(host), hut(guest)
    ck(hh is not None and gh is not None, f'B: both huts on screen ({hh} {gh})')
    if not (hh and gh):
        return ck.result()
    guest.call(f'move {gh[0]} {gh[1]}')
    host.call(f'move {hh[0]} {hh[1]}')
    time.sleep(0.6)
    host.call(f'click {hh[0]} {hh[1]}')
    time.sleep(0.5)
    guest.call(f'click {gh[0]} {gh[1]}')
    new = wait_new(host, guest, o0, 2, 25)
    print(f'  B: new members {new}', flush=True)
    print('  B host', baby(host), flush=True)
    print('  B guest', baby(guest), flush=True)
    ck(len(new) == 2, f'B: two clicks made two members ({new})')
    ck(sorted(1 if o else 0 for o in new.values()) == [0, 1], f'B: one is the host\'s and one the guest\'s ({new})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
