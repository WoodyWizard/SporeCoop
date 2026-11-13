#!/usr/bin/env python3
"""BUG-1053 (Tribe, class BUG-0426): the host never runs a guest's order on a member its game removed since the last
table cycle.

The host resolves a guest's command (MSG_TRIBE_CMD) through its member table g_hm, rebuilt from the game's tribes only
every 0.12 s. A member the host's game took out of its tribe and destroyed since then (a fall, a raid's end) was still
in the table: the guest's attack order on it went to DoAction with the destroyed citizen as its target.

Two games on the tribe fixture. Each round, on the host's console, in one pump (two connections, 3 ms apart):
`coop.tribe drop_member <T>` (RemoveMember(T, destroy): a member of an NPC tribe leaves the game) and
`coop.tribe guestcmd 1 7 <T> <M>` (the guest's attack order on T for its member M, handled as on_message does). A
round counts when the order came after the drop and before the next table cycle (`in_table=1 listed=0`); then the
member's order record in the host's game (`target=`, the game's ActionRec +0x28) must not name T's destroyed block.
SPORE_FIX_OFF=tribe.cmd_listed must FAIL (the order targets the destroyed citizen).

  SPORE_LANE=6 python3 tools/tests/coop_tribe_cmd_listed.py [--rounds 3]
"""
import argparse
import re
import threading
import time

from twoinst import Checks, game_runs, start  # (first: it puts tools/ on the path)
import lane
from sporectl import Console


def members(con):
    """[(id, owner)] of the player tribe, [ids] of the other tribes' members"""
    own, other, player = [], [], False
    for line in con.call('coop.tribe')['out'].splitlines():
        m = re.match(r'tribe (\d+)', line)
        if m:
            player = 'player' in line
            continue
        m = re.match(r'\s+#(\d+) \S+ owner=(-?\d+) age=\d+ sel=\d+ hp=([\d.]+)', line)
        if m and m.group(1) != '0' and float(m.group(3)) > 0:
            (own.append((int(m.group(1)), int(m.group(2)))) if player else other.append(int(m.group(1))))
    return own, other


def hexptr(s):
    s = s.strip()
    return int(s, 16) if re.fullmatch(r'(0x)?[0-9A-Fa-f]+', s) else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=3)
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    game_runs(guest, 90)
    time.sleep(4)
    print(' ', host.call('coop.tribe spawn 4')['out'].strip(), flush=True)  # (members for the guest's share)
    time.sleep(4)
    counted, bad, tried = 0, [], set()
    for _ in range(a.rounds * 3):
        if counted >= a.rounds:
            break
        own, other = members(host)
        mine = [i for i, o in own if o == 1]
        targets = [i for i in other if i not in tried]
        if not mine or not targets:
            print(f'  no guest member ({len(mine)}) or no NPC tribe member left ({len(targets)})', flush=True)
            if not targets:
                host.call('coop.tribe npc_tribe 4 60')
            time.sleep(4)
            continue
        t, m = targets[0], mine[0]
        tried.add(t)
        c1, c2 = Console(lane.HOST), Console(lane.HOST)
        out = {}

        def drop():
            out['drop'] = c1.call(f'coop.tribe drop_member {t}')['out'].strip()

        def cmd():
            time.sleep(0.003)
            out['cmd'] = c2.call(f'coop.tribe guestcmd 1 7 {t} {m}')['out'].strip()

        ths = [threading.Thread(target=drop), threading.Thread(target=cmd)]
        for th in ths:
            th.start()
        for th in ths:
            th.join()
        md = re.search(r'dropped member #\d+ (\S+)', out.get('drop', ''))
        mc = re.search(r'in_table=(\d) listed=(\d) ran=(\d+) refused=(\d+)', out.get('cmd', ''))
        mt = re.search(r'member: rec=(-?\d+) target=(\S+)', out.get('cmd', ''))
        print(f'  round: T=#{t} M=#{m} | {out.get("drop")} | {out.get("cmd")}', flush=True)
        stale = bool(md and mc and mc.group(1) == '1' and mc.group(2) == '0')
        if stale:
            counted += 1
            tp = hexptr(md.group(1))
            got = hexptr(mt.group(2)) if mt else 0
            if tp and got == tp:
                bad.append(f'#{m} ordered onto the dropped #{t} ({mt.group(2)}, ran={mc.group(3)})')
        else:
            print('  (not counted: the order did not come between the drop and the next table cycle)', flush=True)
        time.sleep(3)
    st = re.search(r'tribe\(host\):[^\n]*', host.call('coop.status')['out'])
    print(f'  host: {st and st.group(0)}', flush=True)
    ck(counted >= 1, f'a guest order came after the drop and before the next table cycle ({counted} rounds)')
    ck(not bad, f'no member of the host\'s game got an order on a citizen its game had removed ({bad})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
