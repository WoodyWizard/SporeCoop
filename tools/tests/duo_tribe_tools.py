"""Native rack creation, placement and removal on disposable Clyth copies.

Creates a rack with verified native methods as a fixture. Does not qualify
the purchase UI, food charge, or campaign unlock rules.
"""
import argparse
import math
import struct
import time

from coop_ability_bar import eax, mem, u32
from duo_orders import wait_until
from duo_tribe_rescue import citizens
from duo_villages import Console, command


def rack(c, tribe, kind):
    return eax(c, f'Simulator::cTribe::GetToolByType thiscall {tribe:#x} {kind}')


def transform(c, tool, person):
    spatial = tool + 0x34
    setter = u32(c, u32(c, spatial) + 0x44)
    command(c, f'call {setter:#x} thiscall {spatial:#x} {person + 0xC4:#x} {person + 0xD0:#x}')


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
    identity, row = next((i,r) for i,r in citizens(h).items() if r['species']=='0680BF38' and not r['dead'])
    body, mirror = row['ptr'], citizens(g)[identity]['ptr']
    ht, gt = u32(h,body+0x1014), u32(g,mirror+0x1014)
    if args.control:command(g,'coop.fix tribe.tools off')
    if rack(h,ht,2) or rack(g,gt,2):raise RuntimeError('Fixture already has axes')
    result=command(h,f'coop.tribe tool_fixture 2 {identity}')
    if 'fixture created' not in result:raise RuntimeError(result)
    tool=rack(h,ht,2)
    print('Created native axes fixture',hex(tool),'flags',hex(u32(h,tool+0x84)),flush=True)
    if args.control:
        time.sleep(1)
        if rack(g,gt,2):raise RuntimeError('Missing rack control did not reproduce')
        print('Control reproduced: the new host rack is absent on guest.',flush=True)
        command(g,'coop.fix tribe.tools on')
    wait_until(lambda:rack(g,gt,2),'Native guest rack was not created',10)
    remote=rack(g,gt,2)
    def placed():
        a=struct.unpack('<3f',mem(h,tool+0x38,12))
        b=struct.unpack('<3f',mem(g,remote+0x38,12))
        return math.dist(a,b)<.05 and u32(g,remote+0x1f8)==gt
    wait_until(placed,'Guest rack transform/owner disagrees',5)
    other=next(r['ptr'] for r in citizens(h).values() if r['species']=='0680BF38' and r['ptr']!=body and not r['dead'])
    transform(h,tool,other)
    command(h,f'call SporeCoop::Tribe_UpdateToolLayout thiscall {ht:#x} 2')
    wait_until(placed,'Moved rack did not reconcile',5)
    if rack(g,gt,2)!=remote:raise RuntimeError('Moving replaced the rack')
    command(h,f'call Simulator::cTribe::RemoveTool thiscall {ht:#x} {tool:#x}')
    wait_until(lambda:not rack(g,gt,2),'Removed rack remains on guest',10)
    for c in (h,g):
        if 'total faults: 0' not in command(c,'exc'):raise RuntimeError('Native faults occurred')
    print('PASS native rack creation, movement and removal reconcile with correct owner and zero faults.',flush=True)


if __name__=='__main__':main()
