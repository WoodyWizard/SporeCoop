"""Guest drag-selects a founder vehicle and right-clicks a rival in joined disposable Clyth."""
import argparse
import math
import re
import struct
import time

import play_civ
import netem
from coop_ability_bar import mem, u32
from duo_civ_native_controls import frame
from duo_civ_vehicle_combat import health
from duo_native_ui import dismiss_registration, dismiss_service_error
from duo_orders import wait_until
from duo_tribe_graduation_editor import button
from duo_villages import Console, command


def vehicle(peer, number):
    row = re.search(rf'veh #{number} ([0-9A-F]+) ',command(peer,'coop.civ'),re.I)
    assert row, f'Missing live vehicle #{number}'
    return int(row[1],16)


def current_order(peer, actor):
    first, end = struct.unpack('<II',mem(peer,actor+0xB68,8))
    return (u32(peer,first+0x10),u32(peer,first)) if first and end > first else (-1,0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=int,required=True)
    parser.add_argument('--guest',type=int,required=True)
    parser.add_argument('--proxy-port',type=int,help='Join a local host through a proxy and test a late attack.')
    args = parser.parse_args()
    h, g = Console(args.host), Console(args.guest)
    for peer in (h,g):
        assert 'duoqalineagescivmousecombat' in command(peer,'coop.savedb').lower()
    proxy = None
    if args.proxy_port:
        assert 'GalaxyGameEntry' in command(g,'game.mode')
        proxy = netem.Proxy(args.proxy_port,47812,'delay=0')
        command(g,f'coop.join 127.0.0.1:{args.proxy_port} MouseGuest')
        wait_until(lambda:'Civ' in command(g,'game.mode') and 'Host campaign rules: 4' in command(g,'coop.status'),
                   'Guest did not load through the proxy',100)
        dismiss_registration(g)
        dismiss_service_error(g)
        for wid in ('0445C99D','062EF353'): button(g,wid)
    for peer in (h,g):
        assert "world 'Clyth.spo'" in command(peer,'coop.avatar')
    towns, fleet, _ = play_civ.civ_state(g)
    own = next(i for i,v in fleet.items() if v['owner']==1 and v['purpose']==v['loco']==0)
    rival_pol = next(t['pol'] for t in towns.values() if t['owner']==-1)
    enemy = max((i for i,v in fleet.items() if v['pol']==rival_pol and v['loco']==0),key=lambda i:fleet[i]['hp'])
    actor, target = vehicle(h,own), vehicle(h,enemy)
    mirror = vehicle(g,enemy)
    driver, screen = frame(g,1,own)
    driver.box_select([screen])
    wait_until(lambda:play_civ.civ_state(g)[1][own]['sel'],'Guest could not select its own unit',8)

    # Put the rival on nearby ground, out from the guest's city wall. Only
    # the fixture uses Teleport; every attack order below comes from a mouse click.
    pos = struct.unpack('<3f',mem(h,actor+0x38,12))
    city = next(t['pos'] for t in towns.values() if t['owner']==1)
    outward = play_civ.norm(play_civ.sub(pos,city))
    point = play_civ.norm(tuple(a+25*b for a,b in zip(pos,outward)))
    xyz = tuple(a*math.sqrt(play_civ.dot(pos,pos)) for a in point)
    command(h,f'mem.write {target+0xB80:#x} {struct.pack("<fff",*xyz).hex()}')
    spatial = target+0x34
    teleport = u32(h,u32(h,spatial)+0x44)
    command(h,f'call {teleport:#x} thiscall {spatial:#x} {target+0xB80:#x} {target+0x44:#x}')
    wait_until(lambda:play_civ.dist(play_civ.civ_state(g)[1][enemy]['pos'],xyz)<6,'Rival did not reach guest',10)
    original_max = health(h,target,0x534)
    fraction_before = health(h,target,0x540)/original_max
    assert current_order(h,actor) != (1,target)
    commands = play_civ.stat(play_civ.civ_state(h)[2],'cmds_done')
    for dy in (-3,0,-6,3,-9):
        row = re.search(rf'obj 0*{mirror:X} -?\d+ -?\d+ -?\d+ (-?\d+) (-?\d+) pol=',command(g,'coop.civ nouns 18C6DE8'),re.I)
        if not row:
            view = driver.wait_still()
            point, visible = driver.project(view,play_civ.civ_state(g)[1][enemy]['pos'])
            assert visible and point, 'Rival is outside the guest camera'
            x,y = point
        else:
            x,y = map(int,row.groups())
        driver.right_click(x,y+dy)
        deadline = time.monotonic()+2
        while time.monotonic()<deadline and current_order(h,actor) != (1,target): time.sleep(.1)
        if current_order(h,actor) == (1,target): break
    assert current_order(h,actor) == (1,target), ('Wrong native host target',current_order(h,actor),hex(target))
    assert play_civ.stat(play_civ.civ_state(h)[2],'cmds_done') > commands
    def mirrored_wound():
        maximum = health(h,target,0x534)
        hp = health(h,target,0x540)
        return hp/maximum < fraction_before-.01 and abs(health(g,mirror,0x540)-hp) < 1 and abs(health(g,mirror,0x534)-maximum)<.1
    wait_until(mirrored_wound,'Aimed shot did not produce the same native wound on both peers',40)
    if proxy:
        # The reliable request is real. Kill its target while the request is
        # travelling, so the host must discard it instead of assigning it.
        other = next(i for i,v in fleet.items() if i != enemy and v['pol']==rival_pol)
        dead, guest_target = vehicle(h,other), vehicle(g,other)
        guest_actor = vehicle(g,own)
        rejected = play_civ.stat(play_civ.civ_state(h)[2],'rejected')
        proxy.p['delay'] = 1500
        command(g,f'call Simulator::cVehicle::SetOrder thiscall {guest_actor:#x} {guest_target:#x} 1 1')
        fn = u32(h,u32(h,dead+0x508)+0x18)
        command(h,f'call {fn:#x} thiscall {dead+0x508:#x} f:50000 {u32(h,actor+0x30):#x} '
                  f'0 {actor+0x38:#x} {actor+0x508:#x}')
        wait_until(lambda:other not in play_civ.civ_state(h)[1],'Second target survived',10)
        wait_until(lambda:play_civ.stat(play_civ.civ_state(h)[2],'rejected') > rejected,
                   'Late order was not rejected',10)
        wait_until(lambda:other not in play_civ.civ_state(g)[1],'Retired mirror remained on guest',12)
        assert current_order(h,actor) != (1,dead)
        print('PASS 1.5-second delayed guest attack rejected after target death',flush=True)
        proxy.p['delay'] = 0
    for peer in (h,g): assert 'total faults: 0' in command(peer,'exc')
    print('PASS guest drag and right-click, host native attack on selected rival, same wound on both peers',flush=True)


if __name__ == '__main__':
    main()
