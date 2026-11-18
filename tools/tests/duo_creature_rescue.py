"""Exercise a revive and a timed death in a quiet, disposable two-player Creature save."""
import argparse
import math
import re
import time

from duo_orders import wait_until
from duo_villages import Console, command
import uifind


def dna(c):
    return float(re.search(r'\bdna=([\d.]+)', command(c, 'coop.dna'))[1])


def avatar(c):
    return command(c, 'coop.avatar')


def position(c):
    return tuple(map(float, re.search(r'pos=\(([-.\d ]+)\)', avatar(c))[1].split()))


def wait_playable(clients, planet):
    stable_since = None
    def ready():
        nonlocal stable_since
        for c in clients:
            result = c.call('coop.avatar')
            state = result['out']
            status = command(c, 'coop.status')
            if (not result['ok'] or not state.startswith('Creature:') or
                    f"world '{planet}'" not in state or ' age=1 ' not in state or
                    'busy: -' not in status or 'receiving world:' in status or
                    ', load queued' in status or ', load after the editor' in status or
                    not (status.startswith('host port=') or
                         re.match(r'client id=\d+ connected', status)) or
                    uifind.shown(c, 'You were killed')):
                stable_since = None
                return False
        if stable_since is None:
            stable_since = time.monotonic()
        return time.monotonic() - stable_since >= 3
    wait_until(ready, 'The specified Creature pair is not ready for normal play.', 75)


def caption_ok(c, control='0445C99D', enabled='1'):
    rect = re.search(rf'{control} (-?\d+) (-?\d+) (-?\d+) (-?\d+) enabled={enabled}',
                     command(c, f'coop.ui where {control}'))
    if not rect:
        return False
    x0, y0, x1, y1 = map(int, rect.groups())
    x, y = (x0 + x1) // 2, (y0 + y1) // 2
    command(c, f'move {x} {y}')
    time.sleep(.2)
    command(c, f'click {x} {y}')
    return True


def finish_respawn(c, species, hold_death=0, hold_migration=0, hatch_timeout=65):
    """Observe native death, accept its caption, then require a walking adult."""
    wait_until(lambda: uifind.shown(c, 'You were killed'), 'No native death caption.', 20)
    until = time.monotonic() + hold_death
    while time.monotonic() < until:
        if not uifind.shown(c, 'You were killed'):
            raise RuntimeError('The death caption was dismissed without player input.')
        time.sleep(.5)
    if hold_death:
        print(f'Death caption remained available for {hold_death:g} seconds.', flush=True)
    wait_until(lambda: caption_ok(c), 'The death screen never enabled its confirmation button.', 20)
    wait_until(lambda: not uifind.shown(c, 'You were killed') and ' age=0 ' in avatar(c),
               'The native nest hatch never began.', 20)
    print('Native death caption confirmed and nest hatch began.', flush=True)
    finish_hatch(c, species, hold_migration, hatch_timeout)


def finish_hatch(c, species, hold_migration=0, hatch_timeout=65):
    next_click = time.monotonic()
    started, next_trace = time.monotonic(), time.monotonic()
    held_migration = False
    def grown():
        nonlocal next_click, held_migration, next_trace
        if time.monotonic() >= next_click and uifind.shown(c, 'Your species is migrating'):
            if hold_migration and not held_migration:
                until = time.monotonic() + hold_migration
                while time.monotonic() < until:
                    if (not uifind.shown(c, 'Your species is migrating') or
                            ' age=0 ' not in avatar(c)):
                        raise RuntimeError('The visible migration caption was skipped.')
                    time.sleep(.5)
                held_migration = True
                print(f'Migration caption waited {hold_migration:g} seconds for player input.', flush=True)
            caption_ok(c)
            next_click = time.monotonic() + 2
        if time.monotonic() >= next_click and uifind.shown(c, 'Use your minimap'):
            # This tutorial's green button is inside a disabled container.
            # Its native child is enabled and shares the container's centre.
            caption_ok(c, '05943B78', '[01]')
            next_click = time.monotonic() + 2
        state, status = avatar(c), command(c, 'coop.status')
        if time.monotonic() >= next_trace:
            age = re.search(r'age=\d+ hatchling_waiting=\d+', state)[0]
            busy = next(line for line in status.splitlines() if line.startswith('busy:'))
            scene = re.search(r'cinematic [0-9A-F]+ playing', status)
            print(f'Hatch +{time.monotonic()-started:.0f}s: {age}; {busy}; {scene[0] if scene else "no cinematic"}', flush=True)
            next_trace = time.monotonic() + 10
        return ' age=1 ' in state and 'busy: -' in status
    wait_until(grown, 'The hatchling did not grow up.', hatch_timeout + hold_migration)
    if hold_migration and not held_migration:
        raise RuntimeError('The fixture never displayed the expected migration caption.')
    if f'key=0x{species}' not in avatar(c):
        raise RuntimeError('Native respawn changed the player species.')
    start = position(c)
    try:
        command(c, 'key 0x57 down')
        time.sleep(2)
    finally:
        command(c, 'key 0x57 up')
    if math.dist(start, position(c)) <= 1:
        raise RuntimeError('The respawned adult cannot walk under normal input.')
    print(f'Walking adult retains species {species}.', flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', type=int, required=True)
    p.add_argument('--guest', type=int, required=True)
    p.add_argument('--planet', required=True)
    p.add_argument('--victim', required=True, choices=('host', 'guest'))
    p.add_argument('--hold-death', type=float, default=0)
    p.add_argument('--hold-migration', type=float, default=0)
    p.add_argument('--hatch-timeout', type=float, default=65)
    args = p.parse_args()
    h, g = Console(args.host), Console(args.guest)
    wait_playable((h, g), args.planet)
    victim, helper = (h, g) if args.victim == 'host' else (g, h)
    for c in (h, g):
        text = avatar(c)
        if not text.startswith('Creature:') or f"world '{args.planet}'" not in text:
            raise RuntimeError('Wrong fixture.')
    before = dna(h)
    if before < 10 or dna(g) != before:
        raise RuntimeError('Use a fixture with a settled pack balance of at least 10 DNA.')
    start = position(victim)
    species = re.search(r'key=0x([0-9A-F]+)', avatar(victim))[1]
    command(helper, 'coop.npc gopos %f %f %f' % (start[0]+20, *start[1:]))
    wait_until(lambda: math.dist(position(victim), position(helper)) > 12, 'Could not separate the players.')
    command(victim, 'coop.team hit 1000')
    wait_until(lambda: 'Downed:' in command(victim, 'coop.team'), 'No knockout.')
    wait_until(lambda: 'Revive ' in command(helper, 'coop.team'), 'Partner never saw the revive prompt.')
    command(helper, 'coop.npc gopos %f %f %f' % position(victim))
    wait_until(lambda: 'Downed:' not in command(victim, 'coop.team'), 'No revive.', 6)
    hp = float(re.search(r'hp=([.\d]+)/', avatar(victim))[1])
    if hp <= 1 or dna(h) != before or dna(g) != before:
        raise RuntimeError('Revive changed the shared balance or did not restore health.')
    print(f'Revived {args.victim}, health {hp}, shared DNA {before}.', flush=True)
    time.sleep(2.5)
    command(helper, 'coop.npc gopos %f %f %f' % (start[0]+24, *start[1:]))
    wait_until(lambda: math.dist(position(victim), position(helper)) > 12, 'Could not separate the players.')
    command(victim, 'coop.team hit 1000')
    wait_until(lambda: 'Downed:' in command(victim, 'coop.team'), 'No second knockout.')
    expected = before - min(math.floor(before), math.ceil(before * .10))
    time.sleep(5)
    if not re.search(r'hp=1\.0/', avatar(victim)):
        raise RuntimeError('Native regeneration healed a knocked-out player.')
    if dna(h) != before:
        raise RuntimeError('The host debited a living or revivable player.')
    wait_until(lambda: dna(h) == expected and dna(g) == expected, 'The shared death debit did not converge.', 15)
    finish_respawn(victim, species, args.hold_death, args.hold_migration, args.hatch_timeout)
    for c in (h, g):
        if dna(c) != expected or not re.search(r'total faults: 0\b', command(c, 'exc')):
            raise RuntimeError('Duplicate debit, balance mismatch or native fault.')
    print(f'Expired {args.victim} knockout, shared DNA {before} -> {expected} on both games.')


if __name__ == '__main__':
    main()
