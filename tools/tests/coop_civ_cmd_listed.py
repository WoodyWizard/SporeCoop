#!/usr/bin/env python3
"""BUG-1053 (Civ, class BUG-0426): the host never runs a guest's order on a vehicle its game removed since the last
table cycle.

The host resolves a guest's command (CMD_ORDER) through its vehicle table g_hv, rebuilt from the game's vehicle list
only every 0.1 s. A vehicle the host's game removed since then (disbanded: Vehicle_Remove destroys it) was still in the
table: the guest's attack order on it went to SetOrder with the destroyed vehicle as the order's target (and an order
for a removed vehicle of the guest went to SetOrder on that vehicle).

Two games on civ_npc. The host spawns a military vehicle and gives it to the guest (V). Each round the host spawns
another vehicle (T); then on the host's console, in one pump (two connections, 3 ms apart): `coop.civ remove <T>`
(the game's disband path) and `coop.civ guestcmd 1 <V> v<T>` (the guest's attack order on T, handled as on_message
does). A round counts when the order came after the removal and before the next table cycle (`in_table=1 listed=0`);
then V's current order in the host's game (mOrders +0xB68, the target) must not name T's destroyed block.
SPORE_FIX_OFF=civ.cmd_listed must FAIL (V's order targets the removed vehicle).

  SPORE_LANE=7 python3 tools/tests/coop_civ_cmd_listed.py [--rounds 3]
"""
import argparse
import re
import threading
import time

from twoinst import Checks, start  # (first: it puts tools/ on the path)
import lane
from sporectl import Console


def vehicles(con):
    """{id: (ptr, owner)} of the host's table"""
    out = {}
    for ln in con.call('coop.civ')['out'].splitlines():
        m = re.match(r'\s+veh #(\d+) (\S+) pol=\w+ owner=(-?\d+)', ln)
        if m and m.group(1) != '0':
            out[int(m.group(1))] = (int(m.group(2), 16), int(m.group(3)))
    return out


def hexptr(s):
    s = s.strip()
    return int(s, 16) if re.fullmatch(r'(0x)?[0-9A-Fa-f]+', s) else 0


def spawn(host):
    before = set(vehicles(host))
    print('  host:', host.call('coop.civ spawnveh 0 0 0')['out'].strip(), flush=True)
    time.sleep(1.5)
    new = sorted(set(vehicles(host)) - before)
    return new[0] if new else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=3)
    a = ap.parse_args()
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(5)
    v = spawn(host)
    if v:
        host.call(f'coop.civ select {v} 1')
        print('  host:', host.call('coop.give 1')['out'].strip(), flush=True)
        time.sleep(2)
    owner = vehicles(host).get(v, (0, -1))[1] if v else -1
    ck(v is not None and owner == 1, f'the guest owns vehicle #{v} on the host (owner {owner})')
    if owner != 1:
        return ck.result()
    counted, bad = 0, []
    for _ in range(a.rounds * 3):
        if counted >= a.rounds:
            break
        t = spawn(host)
        if not t:
            time.sleep(3)
            continue
        c1, c2 = Console(lane.HOST), Console(lane.HOST)
        out = {}

        def remove():
            out['rm'] = c1.call(f'coop.civ remove {t}')['out'].strip()

        def cmd():
            time.sleep(0.003)
            out['cmd'] = c2.call(f'coop.civ guestcmd 1 {v} v{t}')['out'].strip()

        tp = vehicles(host).get(t, (0, 0))[0]
        ths = [threading.Thread(target=remove), threading.Thread(target=cmd)]
        for th in ths:
            th.start()
        for th in ths:
            th.join()
        mc = re.search(r'guestcmd: target (\S+) in_table=(\d) listed=(\d) ran=(\d+) refused=(\d+)', out.get('cmd', ''))
        mo = re.search(r'order target=(\S+) type=(-?\d+)', out.get('cmd', ''))
        print(f'  round: T=#{t} {tp:08X} V=#{v} | {out.get("rm")} | {out.get("cmd")}', flush=True)
        if out.get('rm') == 'removed' and mc and mc.group(2) == '1' and mc.group(3) == '0':
            counted += 1
            got = hexptr(mo.group(1)) if mo else 0
            if tp and got == tp:
                bad.append(f'#{v} ordered onto the removed #{t} ({mo.group(1)}, ran={mc.group(4)})')
        else:
            print('  (not counted: the order did not come between the removal and the next table cycle)', flush=True)
        time.sleep(3)
    st = re.search(r'civ\(host\):[^\n]*', host.call('coop.civ')['out'])
    print(f'  host: {st and st.group(0)[-80:]}', flush=True)
    ck(counted >= 1, f'a guest order came after the removal and before the next table cycle ({counted} rounds)')
    ck(not bad, f'no vehicle of the host\'s game got an order on a vehicle its game had removed ({bad})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
