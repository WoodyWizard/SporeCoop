"""Verify a purchased rack after a cold host restart and fresh guest world transfer."""
import argparse
import json
import math
from pathlib import Path
import struct

from coop_ability_bar import mem,u32
from duo_orders import wait_until
from duo_tribe_rescue import citizens
from duo_tribe_tools import rack
from duo_villages import Console,command


def state(c):
    row=next(r for r in citizens(c).values() if r['species']=='0680BF38' and not r['dead'])
    tribe=u32(c,row['ptr']+0x1014)
    tool=rack(c,tribe,1)
    if not tool:raise RuntimeError('Purchased torches missing')
    return dict(position=struct.unpack('<3f',mem(c,tool+0x38,12)),
                faction=u32(c,tool+0x30),owner=u32(c,tool+0x1f8)==tribe,
                purchased=bool(u32(c,tribe+0x19d8)&2))


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',type=int,required=True);p.add_argument('--guest',type=int,required=True)
    p.add_argument('--record',type=Path,required=True);p.add_argument('--capture',action='store_true')
    args=p.parse_args();h,g=Console(args.host),Console(args.guest)
    if args.capture:
        args.record.write_text(json.dumps(state(h)),encoding='utf-8')
        print('Captured purchased native rack identity/placement.',flush=True);return
    expected=json.loads(args.record.read_text(encoding='utf-8'))
    for c in (h,g):
        wait_until(lambda:command(c,'coop.avatar').startswith('Tribe:'),'Tribal world did not load',60)
        current=state(c)
        if not current['owner'] or current['faction']!=expected['faction'] or math.dist(current['position'],expected['position'])>.1:
            raise RuntimeError('Purchased rack owner or placement changed after restart')
        if c is h and not current['purchased']:raise RuntimeError('Purchase history did not persist')
        if 'total faults: 0' not in command(c,'exc'):raise RuntimeError('Native fault after restart')
    print('PASS purchased native rack retains owner, position and purchase history after cold host restart and guest transfer.',flush=True)


if __name__=='__main__':main()
