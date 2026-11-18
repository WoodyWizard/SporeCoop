"""Replay the native consequence decision on the saved five-alliance Vantiegan campaign."""
import argparse

from coop_ability_bar import eax
from duo_tribe_alliance_checkpoint import state
from duo_villages import Console, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    args = parser.parse_args()
    host, guest = Console(args.host), Console(args.guest)
    for c in (host, guest):
        assert "world 'Vantiegan.spo'" in command(c, 'coop.avatar')
    def campaign(c):
        return state(c, [1] * 5, ['291489FF', '7CF26CCB'])
    saved = campaign(host)
    assert saved and saved == campaign(guest)
    def trait():
        return eax(host, 'SporeCoop::Tribe_GetConsequenceTrait cdecl')
    try:
        command(host, 'coop.fix tribe.allied_consequence off')
        old = trait()
    finally:
        command(host, 'coop.fix tribe.allied_consequence on')
    current = trait()
    print(f'Native consequence with fix off {old:08X}, on {current:08X}', flush=True)
    assert old == 0x0CAC124D, 'Unpatched military result did not reproduce'
    assert current == 0x60A78928, 'Native five-alliance outcome did not reach friendly'
    assert saved == campaign(host) == campaign(guest)
    for c in (host, guest): assert 'total faults: 0' in command(c, 'exc')


if __name__ == '__main__':
    main()
