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
   One colony makes one save: no milestone save in the 30 s after it (BUG-1203: a save every 8 s,
   SPORE_FIX_OFF=world.autosave_colony_ids fails).
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
from twoinst import Checks, keyframe, session_restart, start


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
    if not timer:  # BUG-1203: one colony, one save (the host's read missed a colony after each save: a save every 8 s)
        n1 = int(autosave(host).get('milestone_saves', 0))
        time.sleep(30)
        a2 = autosave(host)
        ck(int(a2.get('milestone_saves', 0)) == n1 and a2.get('pending') == '0',
           f'... once: no milestone save in the next 30 s ({n1} -> {a2.get("milestone_saves")}, pending {a2.get("pending")})')
    m1 = money(host)
    # the host's co-op window (F9) says when its game last saved
    host.call('coop.uistate 1')
    time.sleep(1.5)
    line = next((ln for ln in host.call('coop.uistate')['out'].splitlines() if ln.startswith('session ')), '')
    print('  host F9: ' + line, flush=True)
    ck('saved just now' in line or ' min ago' in line, f'the host\'s co-op window shows when the game saved ({line})')
    keyframe('autosave_f9', 'the host\'s co-op window: the session line ends with "saved just now (autosave on)"',
             players={'host': host})
    host.call('coop.uistate 0')

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
