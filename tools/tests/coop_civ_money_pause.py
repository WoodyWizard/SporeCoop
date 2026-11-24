#!/usr/bin/env python3
"""BUG-1461: Civilization, a guest's NPC nation keeps the money the host's game gave it while the host's world stands.

The wsync row civ_money writes every NPC nation's money (+0x98) on the guest every 0.5 s. An NPC nation's AI update
(CivAI_WealthToBudgets 0xBF7370, every second) moves all of +0x98 into its budgets (+0x4AC / +0x4B0): on the host it
stands while a caption pauses the host's world, the guest's AI runs on and moved the copied money away at once (lab
20261123-023643 wsync_civ: host 1500, guest 0 at 3 s; 1 of 9 runs, the one read fell on a 0).

1. civ_npc fixture. The host's civilization captures an NPC city (`coop.civ capture`: the game's caption pauses the
   host's world, its busy state reaches the guest as MSG_HOST_BUSY paused).
2. Another nation gets 1500 on the host (`coop.wsync civmoney`).
3. While the host's world stands, the nation's money is read on both games (`coop.wstate`, the game's +0x98) 8 times
   in 4 s from 1 s after the change: every read on the guest is the host's (within 50), none is 0.
Twin: SPORE_FIX_OFF=civ.npc_money_hold: the guest's reads fall to 0 between the row's writes: must FAIL.

  SPORE_LANE=4 python3 tools/tests/coop_civ_money_pause.py
"""
import time

import worlddiff
from coop_civ import civ
from twoinst import Checks, start


def nation(con, pol):
    return next((c for c in worlddiff.state_dump(con).civs if c['pol'] == pol), None)


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(6)
    hc, _ = civ(host)
    npc = [k for k, v in hc.items() if not v['player']]
    if not ck(npc, 'an NPC city to capture'):
        return ck.result()
    city, cpol = npc[0], hc[npc[0]]['pol']
    civs = [c for c in worlddiff.state_dump(host).civs if not c['player']]
    other = next((c for c in civs if c['pol'] != cpol), civs[0] if civs else None)
    if not ck(other is not None, "another nation in the host's game"):
        return ck.result()
    print(' ', host.call(f'coop.civ capture {city}')['out'].strip(), flush=True)
    time.sleep(1.5)  # the caption is up, the busy state on its way
    print('  host:', host.call('coop.civ movie')['out'].strip(), flush=True)
    print(' ', host.call(f'coop.wsync civmoney {other["pol"]} 1500')['out'].strip(), flush=True)
    time.sleep(1)
    reads = []
    for _ in range(14):
        h, g = nation(host, other['pol']), nation(guest, other['pol'])
        reads.append((h and round(h['money']), g and round(g['money'])))
        time.sleep(0.5)
    print(f'  nation {other["pol"]} money (host, guest) while the host stands: {reads}', flush=True)
    st = next((ln for ln in guest.call('coop.civ')['out'].splitlines() if ln.startswith('money:')), '')
    print('  guest:', st, flush=True)
    ck(all(h is not None and h >= 1000 for h, g in reads), "the host's game: the nation keeps its 1500 (its world stands)")
    bad = [(h, g) for h, g in reads if h is None or g is None or abs(h - g) >= 50]
    ck(not bad, f"the guest's game: every read is the host's money ({len(bad)} of {len(reads)} differ: {bad[:3]})")
    host.call('key enter')  # the caption
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
