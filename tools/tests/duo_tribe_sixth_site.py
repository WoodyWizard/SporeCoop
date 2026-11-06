"""Test physical room for a sixth NPC rival on a disposable two-founder planet."""
import argparse
import json
import math
import re
from pathlib import Path

from coop_ability_bar import u32
from duo_orders import wait_until
from duo_tribe_rebuild_checkpoint import objects
from duo_villages import Console, command


def villages(c):
    return {u32(c, p + 0x30): (p, pos) for p, pos in objects(c, 0x18C6D19)}


def objective_count(c):
    home = villages(c)[0x800021CA][0]
    totem = u32(c, home + 0x19D0)
    return (u32(c, totem + 0x120) - u32(c, totem + 0x11C)) // 4


def above_ground(peer, position):
    text = command(peer, 'coop.ground ' + ' '.join(map(str, position)))
    return float(re.search(r'above ([-\d.]+)', text)[1])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--record', required=True, type=Path)
    parser.add_argument('--restore', action='store_true')
    args = parser.parse_args()
    h, g = Console(args.host), Console(args.guest)
    for c in (h, g):
        assert 'duoqa' in command(c, 'coop.savedb').lower()
        assert "world 'Vantiegan.spo'" in command(c, 'coop.avatar')
        assert 'host 291489FF, guest 7CF26CCB' in command(c, 'coop.founders')

    if args.restore:
        record = json.loads(args.record.read_text())
    else:
        assert not args.record.exists()
        before = villages(h)
        assert len(before) == len(villages(g)) == 7, 'Need two founders and the five native rivals'
        old_count = objective_count(h)
        assert old_count == objective_count(g) and old_count < 5
        reply = command(h, 'coop.tribe npc_tribe 3 clear 067C684A')
        print('Native founding:', reply, flush=True)
        after = villages(h)
        assert len(after) == 8 and before.keys() <= after.keys(), 'Native founding replaced or failed to add a village'
        faction, = after.keys() - before.keys()
        ptr, position = after[faction]
        assert u32(h, ptr + 0x368) and u32(h, ptr + 0x11C)
        assert u32(h,ptr+0x550) == 2 and u32(h,ptr+0x384) > u32(h,ptr+0x380), 'New rival lacks its native instrument rack'
        clearance = min(math.dist(position, pos) for _, pos in before.values())
        assert clearance >= 69, f'Actual native site is too close: {clearance} metres'
        assert re.search(rf'village {faction:08X} species=067C684A\b', command(h, 'coop.tribe villages'))
        record = dict(faction=faction, position=position, objectives=old_count, clearance=clearance,
                      species='067C684A', above=above_ground(h, position))

    def verify(c):
        table = villages(c)
        if len(table) != 8 or objective_count(c) != record['objectives']:
            return False
        matches = []
        for ptr, pos in table.values():
            original = record['position']
            distance = math.dist(pos, original)
            if distance >= 3:
                if not args.restore or distance >= 12 or 'above' not in record:
                    continue
                ratio = math.hypot(*pos) / math.hypot(*original)
                if math.dist(pos, [x*ratio for x in original]) >= .75:
                    continue
                if abs(record['above']) >= .15 or abs(above_ground(c, pos)-record['above']) >= .15:
                    continue
            matches.append((ptr, pos))
        if len(matches) != 1: return False
        ptr, _ = matches[0]
        if not re.search(rf'village {u32(c,ptr+0x30):08X} species={record["species"]}\b', command(c, 'coop.tribe villages')):
            return False
        return (u32(c, ptr + 0x344) - u32(c, ptr + 0x340)) // 4 >= 3 and bool(u32(c, ptr + 0x368))

    assert record['faction'] in villages(h)
    for c in (h, g):
        wait_until(lambda: verify(c), 'Sixth NPC hut and population missing', 30)
        assert 'total faults: 0' in command(c, 'exc')
    if not args.restore:
        args.record.write_text(json.dumps(record), encoding='utf-8')
    print('PASS eight distinct villages on both peers, same earned objective count.', record, flush=True)


if __name__ == '__main__':
    main()
