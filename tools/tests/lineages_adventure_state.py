"""Read native Adventure objectives for targeted shared-mission regressions.

No progress writes or native function calls. Requires isolated DuoQA profiles.
"""
import argparse
import json
import struct

from coop_ability_bar import mem, sym, u32
from duo_villages import Console, command


def read_state(c):
    assert 'duoqa' in command(c, 'coop.savedb').lower(), 'Disposable profile required'
    assert 'Scenario' in command(c, 'game.mode'), 'Enter an Adventure first'
    mode = u32(c, sym(c, 'App::cScenarioMode::ptr'))
    play, data = u32(c, mode + 0x78), u32(c, mode + 0x74)
    resource = u32(c, data + 0x10)
    begin, end = u32(c, play + 0x64), u32(c, play + 0x68)
    assert begin and end >= begin and (end - begin) % 0x1AC == 0
    count = (end - begin) // 0x1AC
    assert count <= 16, 'Unexpected native goal vector'
    goals = []
    for index in range(count):
        raw = mem(c, begin + index * 0x1AC, 0x40)
        assert len(raw) == 0x40
        done, value = raw[0], struct.unpack_from('<i', raw, 4)[0]
        kind, target, secondary, required = struct.unpack_from('<4i', raw, 0x24)
        assert done in (0, 1) and 0 <= kind <= 11 and value >= 0
        goals.append(dict(index=index, done=bool(done), count=value, kind=kind,
                          target=target, secondary=secondary, required=required))
    return dict(mission=struct.unpack('<3I', mem(c, resource + 8, 12)),
                act=u32(c, play + 0xB8), state=u32(c, play + 0x90),
                completed=u32(c, play + 0x28), goals=goals)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--instances', type=int, nargs='+', default=[4, 5])
    parser.add_argument('--expect-shared', action='store_true')
    args = parser.parse_args()
    states = [read_state(Console(i)) for i in args.instances]
    print(json.dumps(dict(zip(args.instances, states)), indent=2), flush=True)
    if args.expect_shared:
        assert all(s == states[0] for s in states[1:]), 'Native Adventure objectives diverged'
