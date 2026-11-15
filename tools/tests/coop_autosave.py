#!/usr/bin/env python3
"""The host's autosave (E2 of 0.7.15, BUG-0864): a crash of the host's game keeps the progress up to its last
autosave.

Before, nothing saved the shared campaign unless the host saved by hand or a guest joined (a share save): a crash of
the host's game, or a Tribe game over, lost everything since. The host's game now saves itself while it hosts, soon
after a milestone and every N minutes (autosave.cpp; world::save_game, the pause menu's Save into its own slot).

1. Both play the `space` fixture. The host's money goes up by 12345 (its progress since the last save).
2. default (milestone): the guest founds a colony of its empire (`coop.space allies colonize`): a milestone of the
   host's autosave. --timer: the host's interval is 40 s (`coop.autosave interval 40`, a test seam for the setting's
   minutes). Within 60 s the host's game wrote a save of its own (`coop.autosave`: confirmed), with nothing else
   saving it (no join, no `coop.save`).
3. Both games end without saving (twoinst.session_restart(save=False): spore.sh kill, as a crash) and start again;
   the host loads its save.
PASS: the host's money after the load is the money it had before the crash (the 12345 are there).
SPORE_FIX_OFF=world.autosave: no autosave, the load has the fixture's money (FAIL).

  SPORE_LANE=10 python3 tools/tests/coop_autosave.py [--timer]
"""
import re
import sys
import time

from coop_space_map import no_movie, sp, wait
from coop_space_rival_merge import money
from twoinst import Checks, session_restart, start


def autosave(con):
    out = con.call('coop.autosave')['out']
    return {k: v for k, v in re.findall(r'(\w+)=(\S+)', out)}


def main():
    timer = '--timer' in sys.argv
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    no_movie(guest)
    no_movie(host)
    a0 = autosave(host)
    print('  host: ' + host.call('coop.autosave')['out'].strip(), flush=True)
    m0 = money(host)
    host.call('coop.res add money 12345')
    ck(wait(lambda: money(host) == (m0 or 0) + 12345, 10), f'the host\'s money went up by 12345 ({m0} -> {money(host)})')
    m1 = money(host)
    if timer:
        print('  host: ' + host.call('coop.autosave interval 40')['out'].strip(), flush=True)
    else:
        out = sp(guest, 'allies colonize')
        print('  guest:', out.splitlines()[0] if out else '-', flush=True)
        ck(out.startswith('colonized'), 'the guest founded a colony (a milestone of the host\'s autosave)')

    def saved():
        no_movie(host, 2)
        a = autosave(host)
        return a if int(a.get('confirmed', 0)) > int(a0.get('confirmed', 0)) else None
    a1 = wait(saved, 75, 2)
    print('  host: ' + host.call('coop.autosave')['out'].strip(), flush=True)
    kind = 'timer_saves' if timer else 'milestone_saves'
    ck(a1 is not None and int(a1.get(kind, 0)) > int(a0.get(kind, 0)),
       f'the host\'s game saved itself ({"the timer" if timer else "after the colony"}): {a1 and a1.get(kind)}')
    time.sleep(3)
    m1 = money(host)

    host, guest, _ = session_restart(host, guest, save=False, host_kill=True)
    time.sleep(2)
    no_movie(host)
    m2 = money(host)
    print(f'  after the crash and the load: host money {m2} (before the crash {m1}, the fixture {m0})', flush=True)
    ck(m2 == m1, f'the host\'s save has its progress up to the autosave: money {m2} == {m1}')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
