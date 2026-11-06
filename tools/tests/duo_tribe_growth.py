"""Native Tribal baby creation and adult reconciliation on disposable Clyth copies.

Uses native SpawnMember/GrowUp to arrange the age transition, then checks the
other game's age and selectable collection. It does not qualify purchase UI.
"""
import argparse
import time

from coop_ability_bar import eax, u32
from duo_orders import wait_until
from duo_tribe_rescue import citizens
from duo_villages import Console, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--control', action='store_true')
    parser.add_argument('--unpatched', action='store_true')
    args = parser.parse_args()
    h, g = Console(args.host), Console(args.guest)
    for c in (h, g):
        if "world 'Clyth.spo'" not in command(c, 'coop.avatar'):
            raise RuntimeError('Use disposable Clyth copies')
    row = next(r for r in citizens(h).values() if r['species'] == '0680BF38' and not r['dead'])
    tribe = u32(h, row['ptr'] + 0x1014)
    if args.control:
        command(g, 'coop.fix tribe.growth off')
    baby = eax(h, f'Simulator::cTribe::SpawnMember thiscall {tribe:#x} 0')
    if not baby:
        raise RuntimeError('Native population cap prevented fixture baby')
    identity = None
    def paired():
        nonlocal identity
        identity = next((i for i, r in citizens(h).items() if r['ptr'] == baby), None)
        return identity is not None and identity in citizens(g)
    wait_until(paired, 'Baby did not replicate', 10)
    mirror = citizens(g)[identity]['ptr']
    if u32(h, baby + 0xB34) != 0 or u32(g, mirror + 0xB34) != 0:
        raise RuntimeError('Fixture was not a baby on both peers')
    grow = u32(h, u32(h, baby) + 0x78)
    command(h, f'call {grow:#x} thiscall {baby:#x}')
    if u32(h, baby + 0xB34) != 1:
        raise RuntimeError('Native host GrowUp failed')
    if args.control or args.unpatched:
        time.sleep(2)
        if u32(g, mirror + 0xB34) != 0:
            raise RuntimeError('Missing growth failure did not reproduce')
        print('FAIL control reproduced: host adult remains a native baby on the guest.', flush=True)
        if args.unpatched:
            return
        command(g, 'coop.fix tribe.growth on')
    wait_until(lambda: u32(g, mirror + 0xB34) == 1, 'Guest baby did not grow', 10)
    for c, p in ((h, baby), (g, mirror)):
        t = u32(c, p + 0x1014)
        begin, end = u32(c, t + 0x354), u32(c, t + 0x358)
        selected = [u32(c, a) for a in range(begin, end, 4)]
        if selected.count(p + 0xC0) != 1:
            raise RuntimeError('Adult is not present exactly once in native selectable members')
        if u32(c, p + 0xB28) != 0x0680BF38:
            raise RuntimeError('Adult changed founding species')
        if 'total faults: 0' not in command(c, 'exc'):
            raise RuntimeError('Native faults occurred')
    time.sleep(1)
    if citizens(g)[identity]['ptr'] != mirror:
        raise RuntimeError('Growth replaced the native citizen')
    print('PASS same native baby grew into a selectable original-species adult on both peers.', flush=True)


if __name__ == '__main__':
    main()
