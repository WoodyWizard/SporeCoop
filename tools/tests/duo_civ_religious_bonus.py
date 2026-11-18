"""BUG-9078: native religious governor bonus follows preacher purpose and faction."""
import argparse
import re
import struct

from coop_ability_bar import mem,u32
from duo_civ_founder_control import cities
from duo_civ_species import native
from duo_civ_takeover import preach,followers
from duo_orders import wait_until
from duo_villages import Console,command


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',type=int,required=True)
    p.add_argument('--guest',type=int,required=True)
    p.add_argument('--control',action='store_true')
    p.add_argument('--baseline',action='store_true',help='Unpatched DLL without the new switch.')
    a=p.parse_args(); h,g=Console(a.host),Console(a.guest)
    for peer in(h,g):
        assert 'duoqa' in command(peer,'coop.savedb').lower()
        assert "world 'Clyth.spo'" in command(peer,'coop.avatar')
    if not a.baseline:
        command(h,'coop.fix civ.religious_attribution '+('off' if a.control else 'on'))
    command(h,'coop.fix civ.takeover_resistance off')
    command(g,'coop.role 3')
    wait_until(lambda:re.search(r'^role 1 .* religious',command(h,'coop.role'),re.M),
               'Guest religious role not received',10)
    rows=cities(h)
    own=next(r for r in rows if r[3]==0)
    partner=next(r for r in rows if r[3]==1)
    rival=next(r for r in rows if r[3]==-1)
    city=rival[1]
    print('Native followers',followers(h,city),flush=True)
    design_id=native(h,'Simulator::VehicleDesignId cdecl 0 0')
    country=u32(h,partner[1]+0x590)
    keyptr=native(h,f'Simulator::cCivilization::GetDesignKey thiscall {country:#x} {design_id:#x}')
    key=struct.unpack('<III',mem(h,keyptr,12))
    assert key[0]
    def spawn(purpose):
        # Reuse the available fixture model; SpawnVehicle assigns real native
        # purpose/behavior. This measures conversion attribution, not vehicle art.
        model=' '.join(hex(k) for k in key)
        ptr=native(h,f'Simulator::cCity::SpawnVehicle thiscall {partner[1]:#x} {purpose} 0 {model} 0')
        assert u32(h,ptr+0x30)==partner[2] and u32(h,ptr+0xB20)==purpose
        wait_until(lambda:re.search(rf'veh #\d+ {ptr:08X} .*owner=1 ',command(h,'coop.civ')),
                   'Spawned vehicle was not assigned to the guest',10)
        wait_until(lambda:re.search(rf'owner=1 purpose={purpose} ',command(g,'coop.civ')),
                   'Guest did not receive native purpose',10)
        return ptr
    def place(ptr,position):
        command(h,f'mem.write {ptr+0xB80:#x} {position.hex()}')
        spatial=ptr+0x34
        teleport=u32(h,u32(h,spatial)+0x44)
        command(h,f'call {teleport:#x} thiscall {spatial:#x} {ptr+0xB80:#x} {ptr+0x44:#x}')
    trader=spawn(2)
    place(trader,mem(h,city+0x124,12))
    got=preach(h,city,partner[2])
    print('Trader nearby, converted',got,'expected 1',flush=True)
    assert got==1,'Economic vehicle incorrectly grants religious bonus'
    place(trader,mem(h,partner[1]+0x124,12))
    preacher=spawn(1)
    place(preacher,mem(h,city+0x124,12))
    got=preach(h,city,partner[2])
    print('Own preacher nearby, converted',got,'expected 2',flush=True)
    assert got==2,'Religious vehicle did not grant governor bonus'
    # Native competing preaching first returns the current converter's followers
    # to the city owner. It does not immediately add the new converter's religion.
    before=followers(h,city)
    preach(h,city,own[2])
    after=followers(h,city)
    undone=before[partner[2]]-after[partner[2]]
    print('Unrelated founder undoes',undone,'expected 1',flush=True)
    assert undone==1 and after[rival[2]]-before[rival[2]]==1, \
        'Guest preacher incorrectly boosts another nation'
    command(h,'coop.fix civ.takeover_resistance on')
    command(g,'coop.role 0')
    wait_until(lambda:re.search(r'^role 1 .* none',command(h,'coop.role'),re.M),
               'Guest test role did not reset',10)
    assert all('total faults: 0' in command(peer,'exc') for peer in(h,g))
    print('PASS religious purpose, faction attribution and native guest mirrors',flush=True)


if __name__=='__main__':main()

