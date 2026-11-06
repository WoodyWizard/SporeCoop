"""BUG-9053: both native cultures retain the graduation designs after world load."""
import argparse,re,struct
from duo_villages import Console,command
from duo_civ_species import native
from coop_ability_bar import u32


LAND_VEHICLE_EDITORS = {'economic':0xF670AA43, 'military':0x7D433FAD, 'religious':0x9AD7D4AA}


def designs(c,vehicle_editor=0xF670AA43):
    assert 'Civ' in command(c,'game.mode')
    row=re.search(r'^city -?\d+ ([0-9A-F]+).*?\(player\)',command(c,'coop.civ'),re.M)
    assert row, 'No native player city'
    nation=u32(c,int(row[1],16)+0x590)
    result={}
    for kind,design in [('building',0x99E92F05),('vehicle',vehicle_editor)]:
        p=native(c,f'Simulator::cCivilization::GetDesignKey thiscall 0x{nation:X} 0x{design:X}')
        raw=command(c,f'mem.read 0x{p:X} 12').split()[1:]
        key=struct.unpack('<III',bytes.fromhex(''.join(raw)))
        result[kind]=key
        print(kind,tuple(f'{x:08X}' for x in key),flush=True)
    return result


def verify(h,g,vehicle_editor=0xF670AA43):
    expected,actual=designs(h,vehicle_editor),designs(g,vehicle_editor)
    assert all(k[0] for k in expected.values()), f'Host designs missing: {expected}'
    assert actual==expected, f'Native culture designs differ: host={expected}, guest={actual}'
    for c in(h,g):
        for kind,key in expected.items():
            assert 'status=same' in command(c,f'coop.creations content {kind} {key[0]:08X}'), 'Native model unavailable'
        assert 'total faults: 0' in command(c,'exc')
    print('PASS native cultures match, both custom models available, zero faults.',flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',required=True,type=int);p.add_argument('--guest',required=True,type=int)
    p.add_argument('--vehicle-role',choices=tuple(LAND_VEHICLE_EDITORS),default='economic')
    a=p.parse_args();verify(Console(a.host),Console(a.guest),LAND_VEHICLE_EDITORS[a.vehicle_role])
