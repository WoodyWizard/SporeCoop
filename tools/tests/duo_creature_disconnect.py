"""A partner leaving must not cancel the host's active Creature knockout."""
import argparse
import math
import re
import time
import uifind

from duo_creature_rescue import avatar, dna, position, finish_respawn, wait_playable
from duo_orders import wait_until
from duo_villages import Console, command


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', type=int, required=True)
    p.add_argument('--guest', type=int, required=True)
    p.add_argument('--planet', required=True)
    p.add_argument('--victim', choices=('host', 'guest'), default='host')
    p.add_argument('--nofix', action='store_true')
    p.add_argument('--rejoin', action='store_true', help='reuse the departed seat before expiry')
    args = p.parse_args()
    host, guest = Console(args.host), Console(args.guest)
    wait_playable((host, guest), args.planet)
    original_seat = re.match(r'client id=(\d+) connected', command(guest, 'coop.status'))[1]
    victim = host if args.victim == 'host' else guest
    species = re.search(r'key=0x([0-9A-F]+)', avatar(victim))[1]
    if args.nofix:
        command(host, 'coop.fix creature.recovery_leave off')
    for c in (host, guest):
        state = avatar(c)
        if not state.startswith('Creature:') or f"world '{args.planet}'" not in state:
            raise RuntimeError('Use the specified disposable Creature pair.')
    before = dna(host)
    if before < 10 or dna(guest) != before:
        raise RuntimeError('Shared DNA must have settled before testing.')
    start = position(host)
    command(guest, 'coop.npc gopos %f %f %f' % (start[0] + 24, *start[1:]))
    wait_until(lambda: math.dist(position(host), position(guest)) > 12, 'Players did not separate.')
    command(victim, 'coop.team hit 1000')
    wait_until(lambda: 'Downed:' in command(victim, 'coop.team'), 'No knockout.')
    # The remote Begin may precede its first downed position packet. Wait for
    # the host to acknowledge pending recovery through its real save blocker.
    if args.victim == 'guest':
        wait_until(lambda: 'revive or respawn' in host.call('coop.save')['out'],
                   'Host never registered the remote knockout.', 5)
    command(guest, 'coop.leave')
    time.sleep(1)
    if args.victim == 'host' and 'Downed:' not in command(host, 'coop.team'):
        raise RuntimeError('Partner departure cancelled the host knockout.')
    if dna(host) != before:
        raise RuntimeError('Partner departure charged DNA before the timer expired.')
    if 'revive or respawn' not in host.call('coop.save')['out']:
        raise RuntimeError('A checkpoint can omit the pending expiry debit.')
    if args.rejoin:
        command(guest, 'coop.join 127.0.0.1:47810 DuoReplacementGuest')
    expected = before - min(math.floor(before), math.ceil(before * .10))
    wait_until(lambda: dna(host) == expected, 'The host never applied the expiry debit.', 15)
    wait_until(lambda: 'Downed:' not in command(host, 'coop.team'), 'Knockout did not resolve.')
    if args.victim == 'host':
        finish_respawn(host, species)
    if args.rejoin:
        wait_playable((host, guest), args.planet)
        if not command(guest, 'coop.status').startswith(f'client id={original_seat} connected'):
            raise RuntimeError('The regression did not reuse the departed network seat.')
        wait_until(lambda: dna(guest) == expected, 'The reused seat received the wrong DNA balance.')
        if 'Downed:' in command(guest, 'coop.team') or uifind.shown(guest, 'You were killed'):
            raise RuntimeError('The new participant inherited the old knockout.')
    time.sleep(2)
    if dna(host) != expected:
        raise RuntimeError('The knockout charged DNA more than once.')
    for c in (host, guest):
        if not re.search(r'total faults: 0\b', command(c, 'exc')):
            raise RuntimeError('A game reported a native fault.')
    print(f'PASS guest left during {args.victim} knockout, shared DNA {before} -> {expected} once.', flush=True)


if __name__ == '__main__':
    main()
