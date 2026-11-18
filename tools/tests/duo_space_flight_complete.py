"""Complete the seven-orb flight objective with normal input on a disposable guest.

This checks the flight objective, not the subsequent scan/abduction/combat lessons.
Run after duo_space_training_restart.py. --resume continues a partially flown route.
"""
import argparse
import math
import re
import struct
import time

from coop_ability_bar import mem,u32
from duo_civ_species import native
from duo_orders import wait_until
from duo_space_captain_outfit import identity
from duo_space_tutorial_choices import context
from duo_villages import Console,command
from play_civ import Player,dot,norm,sub
import uifind
from duo_tribe_graduation_editor import click


def remaining(peer):
    result=peer.call('coop.ui find "more cit"')
    if not result['ok']:
        assert "no window with caption" in result['out'],result
        return None
    match=re.search(r'Visit (\d+) more cit',result['out'])
    return int(match[1]) if match else None


def position(peer):
    row=re.search(r'pos=\(([-0-9. ]+)\)',command(peer,'coop.avatar'))
    assert row,'No native spacecraft position'
    return tuple(map(float,row[1].split()))


def cities(peer):
    manager=native(peer,'Simulator::cGameNounManager::Get cdecl')
    vector=native(peer,f'Simulator::cGameNounManager::GetAllCities thiscall {manager:#x}')
    lo,hi=struct.unpack('<II',mem(peer,vector,8))
    assert lo and 0<hi-lo<=64*4 and (hi-lo)%4==0
    result=[]
    for ptr in struct.unpack('<'+'I'*((hi-lo)//4),mem(peer,lo,hi-lo)):
        point=struct.unpack('<3f',mem(peer,ptr+0x124,12))
        assert all(map(math.isfinite,point))
        result.append(point)
    return result


def distance(a,b):
    return 500*math.acos(max(-1,min(1,dot(norm(a),norm(b)))))


def fly(peer,target,seconds=35):
    camread=object.__new__(Player);camread.c=peer
    deadline=time.monotonic()+seconds
    while time.monotonic()<deadline:
        pos=position(peer);d=distance(pos,target)
        if d<5:return True
        cam=camread.cam()
        if not cam:return False
        up=norm(pos)
        right=norm(sub(cam['right'],tuple(dot(cam['right'],up)*k for k in up)))
        front=norm(sub(cam['up'],tuple(dot(cam['up'],up)*k for k in up)))
        dest=sub(norm(target),up)
        x,y=dot(dest,right),dot(dest,front)
        keys=[]
        if abs(x)>.45*abs(y):keys.append('0x44' if x>0 else '0x41')
        if abs(y)>.45*abs(x):keys.append('0x57' if y>0 else '0x53')
        try:
            for key in keys:command(peer,'key '+key+' down')
            time.sleep(max(.12,min(.7,d/120)))
        finally:
            for key in keys:command(peer,'key '+key+' up')
        time.sleep(.2)
    return False


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',type=int,required=True)
    p.add_argument('--guest',type=int,required=True)
    p.add_argument('--resume',action='store_true')
    a=p.parse_args();h,g=Console(a.host),Console(a.guest)
    for peer in(h,g):assert 'duoqa' in command(peer,'coop.savedb').lower()
    assert context(g)==0 and context(h) in(1,2)
    original=identity(g)
    assert original,'Personal captain and ship were not readable'
    initial=remaining(g)
    assert initial is not None and (a.resume or initial==7),'Requires a native flight lesson'
    pending=list(enumerate(cities(g)))
    seen=initial
    while pending and seen:
        index,target=min(pending,key=lambda row:distance(position(g),row[1]))
        pending.remove((index,target))
        reached=fly(g,target)
        command(g,'wait.frames 12 10000')
        value=remaining(g)
        if reached and value==seen:
            # Orbs have different altitudes. Sweep down through the city, then
            # restore cruising height if that did not finish the objective.
            command(g,'wheel 800 300 3')
            command(g,'wait.frames 25 10000')
            value=remaining(g)
            if value is not None:
                command(g,'wheel 800 300 -3')
                command(g,'wait.frames 25 10000')
        print('City',index,'reached',reached,'surface distance',round(distance(position(g),target)),
              'ship radius',round(math.dist(position(g),(0,0,0)),1),'remaining',value,flush=True)
        if value is not None:
            assert value<=seen
            seen=value
        else:break
    assert remaining(g) is None,'Not all seven native flight orbs were completed'
    def acknowledged():
        if uifind.shown(g,'So, what do you want me to do next?'):return True
        click(g,289,502)
        return False
    wait_until(acknowledged,'Mission Control did not acknowledge the completed flight',25)
    assert identity(g)==original,'Lesson changed personal creations'
    for peer in(h,g):assert 'total faults: 0' in command(peer,'exc')
    assert context(g)==0 and context(h) in (1,2),'Flight changed the partner tutorial state'
    print('PASS seven native flight orbs and Mission Control acknowledgment; personal creations retained. '
          'Scanner, abduction, combat and orbit are separate checks.',flush=True)


if __name__=='__main__':main()

