#!/usr/bin/env python3
"""Space Rivals: a budget the host gives a player with `coop.store adopt` reaches that player's game (BUG-1482, audit
SPC-3).

`coop.store adopt <player>` (the host's word) gives a guest that came back with a new rejoin token the stored things of
its name. The Rivals budget only moved to the new key in the host's store: nobody sent it to the guest (whose budget
is known, so a K_SET would be dropped), and the guest's next report (its money: a newcomer's 0) replaced it.

1. Two games in Space, Rivals mode; the guest earns 7000: its budget mg.
2. The guest leaves and joins as another game of the same name (`keptkey other on`, BUG-1107's seam: a new rejoin
   token): a newcomer to Rivals, its money 0. The host writes its store (`coop.store save`: both keys, the name Bob).
3. The host adopts: `coop.store adopt Bob`. Within 15 s the guest's game money (cEmpire +0xD0) must be mg, stay mg for
   10 s, and the host's copy of the guest's budget must be mg.
   0.7.16.1 (SPORE_FIX_OFF=space.rival_adopt): the guest keeps 0 and the host's copy goes back to 0 (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_space_rival_adopt.py
"""
import re
import time

from coop_space_kept_key import budget_on_host, rejoin
from coop_space_rival_merge import money, rivals, sp, wait
from twoinst import Checks, keyframe, start


def gid_of(guest):
    return int(re.search(r'\bid=(\d+)', guest.call('coop.status')['out']).group(1))


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    if (money(host) or 0) < 20000:
        host.call('coop.res add money 30000')
        time.sleep(2)
    print('  host:', sp(host, 'rivalry mode rivals'), flush=True)
    ck(wait(lambda: rivals(host) and rivals(guest), 15), 'rivals mode on both games')
    time.sleep(4)
    guest.call('coop.res add money 7000')
    time.sleep(4)
    gid, mg = gid_of(guest), money(guest)
    ck(mg and budget_on_host(host, gid) == mg, f'the host knows the guest\'s budget {mg}')
    if not mg:
        return ck.result()

    # 2. the same player with a new token: a newcomer
    print('  ' + sp(guest, 'keptkey other on'), flush=True)
    ok = rejoin(host, guest, ck, 'Bob comes back with a new token')
    sp(guest, 'keptkey other off')
    if not ok:
        return ck.result()
    gid = gid_of(guest)
    m0 = money(guest)
    print(f'  Bob\'s money as a newcomer: {m0} | host: ' + sp(host, 'rivalry').splitlines()[0], flush=True)
    ck(m0 == 0, f'as a newcomer the guest starts with 0 (it has {m0})')
    print('  host store:', host.call('coop.store save')['out'].strip(), flush=True)
    time.sleep(3)

    # 3. the host's word
    out = host.call('coop.store adopt Bob')['out'].strip()
    print('  host adopt:', out, flush=True)
    ck('are Bob\'s now' in out, f'the host gave Bob the stored things of its name ({out})')
    got = wait(lambda: money(guest) == mg, 15, 1)
    print(f'  after the adopt: guest money {money(guest)} (stored {mg}) | host: '
          + sp(host, 'rivalry').splitlines()[0][:300], flush=True)
    ck(got, f'the guest\'s game has its stored budget {mg} (it has {money(guest)})')
    seen = set()
    for _ in range(10):
        seen.add(money(guest))
        time.sleep(1)
    ck(seen == {mg}, f'and keeps it for 10 s (seen {sorted(x for x in seen if x is not None)})')
    hb = budget_on_host(host, gid)
    ck(hb == mg, f'the host\'s copy of the guest\'s budget is {mg} ({hb})')
    keyframe('rival_adopt', 'the guest after the host\'s coop.store adopt: its HUD shows its stored Rivals budget '
             f'({mg}), not 0', players={'host': host, 'guest': guest})
    if rivals(host):
        sp(host, 'rivalry mode allies')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
