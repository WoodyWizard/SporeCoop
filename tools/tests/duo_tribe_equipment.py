"""Native equipment replication on disposable Clyth worlds; not a purchase test."""
import argparse
import time

from coop_ability_bar import eax, sym, u32
from duo_orders import wait_until
from duo_tribe_rescue import citizens
from duo_villages import Console, command


def attached_model(c, citizen):
    # C0E130 is the native read-only attachment model lookup: thiscall(id), ret4.
    # C250A0 builds the preferred-hand attachment id from 02735300 + local grasper.
    delta = sym(c, 'Simulator::Cell::cCellGame::_ptr') - 0x16B3C04
    grasper = u32(c, citizen + 0xFC4)
    return eax(c, f'{delta + 0xC0E130:#x} thiscall {citizen:#x} {(0x2735300 + grasper) & 0xffffffff:#x}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--control', action='store_true')
    args = parser.parse_args()
    h, g = Console(args.host), Console(args.guest)
    for c in (h, g):
        if "world 'Clyth.spo'" not in command(c, 'coop.avatar'):
            raise RuntimeError('Use disposable Clyth copies')
    rows = citizens(h)
    identity, row = next((i, r) for i, r in rows.items()
                         if r['species'] == '0680BF38' and not r['dead']
                         and u32(h, r['ptr'] + 0x102C) == 0
                         and u32(h, r['ptr'] + 0xB34) == 1)
    body, mirror = row['ptr'], citizens(g)[identity]['ptr']
    if args.control:
        command(g, 'coop.fix tribe.equipment off')
    for tool in (1, 4, 7, 0):
        command(h, f'call SporeCoop::Citizen_SetSpecializedTool thiscall {body:#x} {tool}')
        item = eax(h, f'Simulator::cCreatureCitizen::GetHandheldItemForTool thiscall {body:#x} {tool}') if tool else 0
        command(h, f'call SporeCoop::Citizen_SetHandheldItem thiscall {body:#x} {item} -1')
        if args.control and tool == 1:
            time.sleep(1)
            if u32(g, mirror + 0x102C) != 0:
                raise RuntimeError('Disabled equipment fix did not reproduce stale guest specialization')
            print('Control reproduced: equipped host citizen remains unequipped on guest.', flush=True)
            command(g, 'coop.fix tribe.equipment on')
        wait_until(lambda: u32(g, mirror + 0x102C) == tool and u32(g, mirror + 0xFFC) == item,
                   f'Equipment {tool}/{item} did not replicate', 10)
        if tool:
            for c, p in ((h, body), (g, mirror)):
                if not attached_model(c, p):
                    raise RuntimeError(f'Tool {tool} has no native attached model')
        print(f'Tool {tool}, held item {item}: both native citizens agree; model checked for equipped tools.', flush=True)
    for c, p in ((h, body), (g, mirror)):
        if u32(c, p + 0xB28) != 0x0680BF38:
            raise RuntimeError('Equipment changed original species')
        if 'total faults: 0' not in command(c, 'exc'):
            raise RuntimeError('Native faults occurred')
    if citizens(g)[identity]['ptr'] != mirror:
        raise RuntimeError('Equipment replaced the native citizen')
    print('PASS weapon, instrument, utility and unequip replicate with native models and zero faults.', flush=True)


if __name__ == '__main__':
    main()
