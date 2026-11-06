"""Play the last three objectives in the opt-in six-rival Tribal qualification."""
import argparse
import json
import struct
import subprocess
import sys
from pathlib import Path

from coop_ability_bar import eax, mem, u32
from duo_orders import wait_until
from duo_tribe_sixth_site import villages, objective_count
from duo_villages import Console, command


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',required=True,type=int); p.add_argument('--guest',required=True,type=int)
    p.add_argument('--record',required=True,type=Path)
    p.add_argument('--through',required=True,type=int,choices=(4,5,6))
    p.add_argument('--feed-first', action='store_true', help='Feed the band before each remaining alliance using its native mat.')
    a = p.parse_args(); h,g = Console(a.host), Console(a.guest)
    extra = json.loads(a.record.read_text())['faction']
    for c in (h,g):
        assert "world 'Vantiegan.spo'" in command(c,'coop.avatar')
        assert 'host 291489FF, guest 7CF26CCB' in command(c,'coop.founders')
        assert len(villages(c)) == 8
    reply = command(h, f'coop.tribe six_rival_trial {extra:X}').strip()
    assert reply in ('Six-rival trial already active.','Six-rival trial active.'), reply

    def state(c):
        tribes = villages(c); allied = 0
        for faction in tribes.keys() - {0x800021CA,0x21CB}:
            if all(float(command(c,f'coop.tribe relation {faction:X} {founder:X}').split('=')[-1])>=7
                   for founder in (0x800021CA,0x21CB)): allied+=1
        player = u32(c,eax(c,'Simulator::cGameNounManager::Get cdecl')+0x74)
        points,total=struct.unpack('<ff',mem(c,player+0x10F0,8))
        assert total==5
        return allied,points,objective_count(c)

    earned,points,tier=state(h)
    assert state(g)==(earned,points,tier) and 3<=earned<=a.through
    for victory in range(earned+1,a.through+1):
        faction = (extra,0x21D1,0x21D8)[victory-4]
        assert faction in villages(h)
        game=eax(h,'SporeCoop::TribalGame_Get cdecl')
        instruments=[str(k) for k in (4,5,6) if eax(h,f'SporeCoop::TribalGame_IsToolUnlocked thiscall {game:#x} {k}')&255]
        cap=eax(h,f'SporeCoop::Tribe_MaxPopulation thiscall {villages(h)[0x21CB][0]:#x}')
        print('Earning actual victory',victory,'against',hex(faction),flush=True)
        subprocess.run([sys.executable,'-u','tools/tests/duo_tribe_performance.py','--host',str(a.host),'--guest',str(a.guest),
                        '--planet','Vantiegan.spo','--species','291489FF','7CF26CCB','--alliance','--six-rival-trial','--trace-dispatch',
                        *(['--feed-first'] if a.feed_first else []),
                        '--faction',f'{faction:X}','--band-size',str(cap),'--instruments',*instruments],check=True)
        current=state(h)
        wait_until(lambda:state(g)==current and current[0]==victory,'NPC allies diverged',30)
        assert (current[1]>=5)==(victory==6) and current[2]==(3,4,5)[victory-4],current
        print('PASS allies, native progress and totem milestones:',current,flush=True)
    for c in (h,g): assert 'total faults: 0' in command(c,'exc')


if __name__=='__main__': main()
