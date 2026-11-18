"""A performance with no selected musicians must explain why no order was sent."""
import argparse
import re

from coop_ability_bar import u32
from duo_tribe_rescue import citizens
from duo_villages import Console, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--guest', type=int, required=True)
    args = parser.parse_args()
    guest = Console(args.guest)
    assert "world 'Vantiegan.spo'" in command(guest, 'coop.avatar')
    rows = citizens(guest)
    member = next(row for row in rows.values() if row['species'] == '06663915' and not row['dead'])
    target = u32(guest, u32(guest, member['ptr'] + 0x1014) + 0x30)
    for identity, row in rows.items():
        if row['species'] == '7CF26CCB':
            command(guest, f'coop.tribe select {identity} 0')

    def sent():
        return int(re.search(r'cmds_sent=(\d+)', command(guest, 'coop.tribe'))[1])

    before = sent()
    reply = command(guest, f'coop.tribe perform_to {target:X}')
    print('Guest reply:', reply, 'packets sent:', sent() - before, flush=True)
    assert sent() == before and 'Select' in reply, 'Empty selection reported a performance request'


if __name__ == '__main__':
    main()
