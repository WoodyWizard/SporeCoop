"""Check two captains inside Adventure Town. --gate moves one captain to the first goal."""
import argparse
import math
import re
import struct

from coop_ability_bar import mem, sym, u32
from duo_orders import wait_until
from duo_villages import Console, command


def captain(client):
    text = command(client, 'coop.avatar')
    if not text.startswith('Adventure:') or 'place=0x6666E0D1' not in text:
        raise RuntimeError('Open Adventure Town on both games.')
    if not re.search(r'^partner \d+: captain marker$', text, re.M):
        raise RuntimeError('No partner marker in this mission.')
    state = re.search(r'act_index=(\d+) play_state=3 goals_completed=(\d+)', text)
    if not state:
        raise RuntimeError('Finish the introduction on both games first.')
    key = re.search(r'key=0x([0-9A-F]+)', text)[1]
    return key, int(state[1]), int(state[2])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True, type=int)
    parser.add_argument('--guest', required=True, type=int)
    parser.add_argument('--gate', choices=('host', 'guest'), help='captain to move in a fresh disposable mission')
    args = parser.parse_args()
    host, guest = (Console(i, timeout=10) for i in (args.host, args.guest))
    h, g = captain(host), captain(guest)
    if h[0] == g[0]:
        raise RuntimeError('Pick different captains for this test.')
    if args.gate:
        if h[1:] != (0, 0) or g[1:] != (0, 0):
            raise RuntimeError('The gate check requires a fresh first act.')
        # SDK cScenarioPlayMode::mMoveToGoalLocation. Spore judges arrival itself.
        walker, other = (host, guest) if args.gate == 'host' else (guest, host)
        def position(client):
            return tuple(map(float, re.search(r'pos=\(([-.\d ]+)\)', command(client, 'coop.avatar'))[1].split()))
        before = position(other)
        sm = u32(walker, sym(walker, 'App::cScenarioMode::ptr'))
        play = u32(walker, sm + 0x78)
        target = struct.unpack('<fff', mem(walker, play + 0xE0, 12))
        if not all(map(math.isfinite, target)) or not 100 < math.dist(target, (0, 0, 0)) < 2000:
            raise RuntimeError('No valid move-to target.')
        if math.dist(before, target) < 20:
            raise RuntimeError('The stationary captain is too close to the gate to establish shared credit.')
        command(walker, 'coop.npc gopos %f %f %f' % target)
        wait_until(lambda: captain(walker)[1:] == (1, 1), 'The game did not credit the arriving captain.', 35)
        wait_until(lambda: captain(other)[1:] == (1, 1), 'The stationary captain did not get native goal credit.', 12)
        h, g = captain(host), captain(guest)
        if math.dist(position(other), before) > 2:
            raise RuntimeError('The stationary captain moved.')
    for c in (host, guest):
        for action in ('coop.save', 'coop.menu'):
            response = c.call(action)
            if response['ok'] or 'adventure' not in response['out'].lower():
                raise RuntimeError(f'{action} must refuse the campaign operation during an adventure.')
        if not re.search(r'total faults: 0\b', command(c, 'exc')):
            raise RuntimeError('The game reported a first-chance fault.')
    print('PASS: different native captains, same mission, both markers, zero faults.')
    print(f'Host species/act/goals {h}; guest {g}.')


if __name__ == '__main__':
    main()
