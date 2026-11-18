"""Verify reserved-founder scaling on native Civilization buildings and incoming hits."""
import argparse
import json
from pathlib import Path
import re
import struct

from coop_ability_bar import mem, u32
from duo_civ_founder_control import cities
from duo_civ_vehicle_combat import health
from duo_orders import wait_until
from duo_villages import Console, command


def buildings(peer, city):
    first, end = struct.unpack('<II',mem(peer,city+0x340,8))
    assert 0 < end-first <= 4*64 and (end-first)%4 == 0
    return [u32(peer,slot) for slot in range(first,end,4)]


def building_hit(peer, source, target, amount=10):
    combat = target+0x120
    function = u32(peer,u32(peer,combat)+0x18)
    faction = u32(peer,source+0x30)
    before = health(peer,target,0x158)
    assert before > amount
    command(peer,f'call {function:#x} thiscall {combat:#x} f:{amount} {faction:#x} 0 '
                 f'{source+0x38:#x} {source+0x508:#x}')
    return before-health(peer,target,0x158)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=int,required=True)
    parser.add_argument('--guest',type=int,required=True)
    parser.add_argument('--capture',action='store_true',help='Verify captured hostile buildings lose their bonus.')
    receipts = parser.add_mutually_exclusive_group()
    receipts.add_argument('--save-receipt',type=Path)
    receipts.add_argument('--cold-receipt',type=Path)
    args = parser.parse_args()
    host,guest = Console(args.host),Console(args.guest)
    if args.cold_receipt:
        wait_until(lambda:'GalaxyGameEntry' in host.call('game.mode')['out'],'No galaxy menu',90)
        assert 'duoqa' in command(host,'coop.savedb').lower()
        command(host,'wait.frames 180 60000')
        command(host,'coop.load Game0 Clyth')
        wait_until(lambda:'Civ' in command(host,'game.mode'),'No Civilization save',90)
        saved = json.loads(args.cold_receipt.read_text())
        city = next(city for _,city,pol,_ in cities(host) if pol==saved['faction'])
        target = next(b for b in buildings(host,city) if u32(host,b+0x24)==saved['serial'])
        maximum,hp = health(host,target,0x14C),health(host,target,0x158)
        print('Cold building maximum and wound',maximum,hp,saved,flush=True)
        assert abs(maximum-saved['max']) < .05
        assert abs(hp-saved['hp']) < 2
        assert 'total faults: 0' in command(host,'exc')
        print('PASS cold native building max and wound without compounded scaling',flush=True)
        return
    for peer in (host,guest):
        assert 'duoqa' in command(peer,'coop.savedb').lower()
        assert "world 'Clyth.spo'" in command(peer,'coop.avatar')
    assert 'Player slots: 4,' in command(host,'coop.status')
    rows = cities(host)
    home = next(row for row in rows if row[3] == 0)
    rival = next(row for row in rows if row[3] == -1)
    command(host,f'coop.civ build {home[0]} house')
    def ordinary_building(peer, city):
        hall = u32(peer,city+0x320)
        return next(b for b in buildings(peer,city) if b != hall)
    own_building = ordinary_building(host,home[1])
    enemy_building = ordinary_building(host,rival[1])
    vehicles = [(int(v,16),int(pol,16)) for v,pol in re.findall(
        r'veh #\d+ ([0-9A-F]+) pol=([0-9A-F]+)',command(host,'coop.civ'))]
    enemy = next(v for v,pol in vehicles if pol == rival[2])
    own = next(v for v,pol in vehicles if pol == home[2])
    native_max = health(host,enemy_building,0x14C)
    dealt = building_hit(host,enemy,own_building)
    print('Incoming military hit to founder building',dealt,'expected',11.5,flush=True)
    building_hit(host,own,enemy_building)
    scaled_max = health(host,enemy_building,0x14C)
    print('Enemy building max HP',native_max,'->',scaled_max,'expected',native_max*1.45,flush=True)
    assert abs(dealt-11.5) < .01, 'Enemy building damage ignored reserved seats'
    assert abs(scaled_max-native_max*1.45) < .05, 'Rival building max HP ignored reserved seats'
    for city_row, target in ((home,own_building),(rival,enemy_building)):
        mirror_city = next(c for i,c,_,_ in cities(guest) if i==city_row[0])
        mirror = ordinary_building(guest,mirror_city)
        wait_until(lambda:abs(health(host,target,0x158)-health(guest,mirror,0x158)) < .1 and
                          abs(health(host,target,0x14C)-health(guest,mirror,0x14C)) < .1,
                   'Building wound or maximum differs on guest',15)
    if args.save_receipt:
        assert not args.save_receipt.exists()
        # A deterministic wound isolates persistence from native repair ticks.
        hp = scaled_max*.6
        command(host,f'mem.write {enemy_building+0x158:#x} {struct.pack("<f",hp).hex()}')
        location = re.search(r'location=(.+?)\s+access=',command(host,'coop.savedb'))[1]
        planet = Path(location)/'Clyth.spo'
        stamp = planet.stat().st_mtime_ns
        print(command(host,'coop.save'),flush=True)
        wait_until(lambda:planet.stat().st_mtime_ns != stamp,'No native building save',30)
        command(host,'coop.civ')  # Join the game thread after serialization finishes.
        assert abs(health(host,enemy_building,0x14C)-scaled_max) < .05
        args.save_receipt.write_text(json.dumps(dict(faction=rival[2],max=native_max,
                serial=u32(host,enemy_building+0x24),hp=hp/1.45)))
        print('PASS live maximum restored after native save',scaled_max,flush=True)
    if args.capture:
        command(host,f'call Simulator::cCity::Capture thiscall {rival[1]:#x} {home[2]:#x} 0 0 0 0')
        def unscaled_capture():
            for peer in (host,guest):
                row = next(row for row in cities(peer) if row[0]==rival[0])
                if row[2] != home[2]: return False
                target = ordinary_building(peer,row[1])
                if abs(health(peer,target,0x14C)-native_max) >= .05: return False
            return True
        wait_until(unscaled_capture,'Captured building kept hostile maximum on a peer',30)
        captured_city = next(city for index,city,_,_ in cities(host) if index==rival[0])
        enemy_building = ordinary_building(host,captured_city)
        print('PASS captured building restores native maximum on both peers',flush=True)
    command(host,'coop.leave')
    assert abs(health(host,enemy_building,0x14C)-native_max) < .05
    print('PASS leaving restores native building maximum',native_max,flush=True)
    assert all('total faults: 0' in command(peer,'exc') for peer in (host,guest))
    print('PASS reserved building HP, incoming damage, and both native mirrors',flush=True)


if __name__ == '__main__':
    main()
