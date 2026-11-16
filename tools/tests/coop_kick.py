#!/usr/bin/env python3
"""`coop.kick <id | name>` (plan 0.7.15 A11, owner decision 8: a console command, no UI): the host removes a guest.

1. Two games in Creature. A guest's `coop.kick` is refused (only the host removes players), and so is the host itself.
2. The host runs `coop.kick Bob`: the guest's session ends within seconds and its co-op chat log (what its co-op window
   shows) says "the host removed you from the session"; the host's session has the host alone; neither game faulted.
3. Not a ban: the guest joins again and is in the host's world.

  SPORE_LANE=6 python3 tools/tests/coop_kick.py
"""
import time

from harness import waiter
import lane
from twoinst import Checks, start, wait_guest_in

TEXT = 'the host removed you from the session'


def first(con):
    return (con.call('coop.status')['out'].splitlines() or [''])[0]


wait = waiter(timeout=20, step=0.5, final=True)


def main():
    host, guest = start('creature')
    ck = Checks()
    time.sleep(2)
    out = guest.call('coop.kick 0')['out'].strip()
    print('  guest: coop.kick 0 ->', out, flush=True)
    ck('only the host' in out and 'connected' in first(guest), f'a guest removes nobody ({out})')
    out = host.call('coop.kick 0')['out'].strip()
    ck('does not remove itself' in out, f'the host does not remove itself ({out})')
    out = host.call('coop.kick Bob')['out'].strip()
    print('  host: coop.kick Bob ->', out, flush=True)
    ck(out.startswith('removing player'), f'the host removes Bob ({out})')
    gone = wait(lambda: first(guest).startswith('offline'), 15)
    told = wait(lambda: TEXT in guest.call('coop.status')['out'], 10)
    lines = [ln for ln in guest.call('coop.status')['out'].splitlines() if 'disconnected' in ln]
    print(f'  guest: {first(guest)} | {lines[-1:] if lines else "no disconnect line"}', flush=True)
    print(f'  host: {first(host)} | ' + next((ln for ln in host.call('coop.status')['out'].splitlines()
                                              if ln.startswith('limits:')), ''), flush=True)
    ck(gone, 'the removed guest\'s session ended')
    ck(told, f'the removed guest\'s co-op window says why ("{TEXT}")')
    ck('players=1/' in first(host), f'the host is alone in its session ({first(host)})')
    # not a ban
    guest.call(lane.JOIN)
    ck(wait_guest_in(host, guest, 200), 'the removed player may join again')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
