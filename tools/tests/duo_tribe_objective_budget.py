"""A surviving partner must not prevent the fifth normal NPC objective.

Requires the disposable Clyth four-victory checkpoint. This test never spawns
a village or grants progress. Disable tribe.founder_npc_budget before loading
that checkpoint to reproduce the exhausted four-NPC budget.
"""
import argparse
import struct
from coop_ability_bar import mem,u32
from duo_orders import wait_until
from duo_tribe_rebuild_checkpoint import objects
from duo_villages import Console,command


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',type=int,required=True)
    p.add_argument('--guest',type=int,required=True)
    args=p.parse_args()
    h,g=Console(args.host),Console(args.guest)
    for c in(h,g):
        if "world 'Clyth.spo'" not in command(c,'coop.avatar'):
            raise RuntimeError('Use the four-victory disposable Clyth campaign')
    def state(c):
        if 'Tribe' not in command(c,'game.mode') or 'busy: -' not in command(c,'coop.status'):return None
        ts={u32(c,t+0x30):t for t,_ in objects(c,0x18C6D19)}
        if 0x800021CA not in ts or 0x21CB not in ts:return None
        totem=u32(c,ts[0x800021CA]+0x19D0)
        if not totem:return None
        earned=(u32(c,totem+0x120)-u32(c,totem+0x11C))//4
        if earned!=4:raise RuntimeError(f'Expected exactly four earned victories, got {earned}')
        remaining=[]
        for faction,t in ts.items():
            if faction in(0x800021CA,0x21CB):continue
            hut=u32(c,t+0x368)
            try:relation=float(command(c,f'coop.tribe relation {faction:X} 800021CA').split('=')[-1])
            except ValueError:return None  # an automatic checkpoint began during this read
            if hut and struct.unpack('<f',mem(c,hut+0x158,4))[0]>0 and relation<7:remaining.append(faction)
        return len(ts),tuple(sorted(remaining))
    print('Before:',state(h),state(g),command(h,'coop.tribe npc_budget'),flush=True)
    def ready():
        a,b=state(h),state(g)
        return a and a==b and a[0]==7 and len(a[1])==1
    try:wait_until(ready,'Native controller did not provide a fifth NPC objective',45)
    finally:print('After:',state(h),state(g),command(h,'coop.tribe npc_budget'),flush=True)
    for c in(h,g):
        if 'total faults: 0' not in command(c,'exc'):raise RuntimeError('Native objective-budget fault')
    print('PASS five NPC objectives plus two surviving founders, with no free victory.',flush=True)


if __name__=='__main__':main()
