"""Verify the extra pirates in a fresh native colony raid on the Vantiegan QA planet."""
import argparse
import re
import struct

from coop_ability_bar import mem, u32
from duo_civ_species import native
from duo_native_ui import dismiss_registration, dismiss_service_error
from duo_orders import wait_until
from duo_space_tutorial_choices import context
from duo_tribe_graduation_editor import button
from duo_villages import Console, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=int,required=True)
    parser.add_argument('--seats',type=int,choices=(2,4),required=True)
    parser.add_argument('--guest',type=int)
    parser.add_argument('--combat',action='store_true')
    args = parser.parse_args()
    peer = Console(args.host)
    wait_until(lambda:peer.call('ping')['ok'],'No game console',60)
    wait_until(lambda:'GalaxyGameEntry' in command(peer,'game.mode'),'No menu',90)
    assert 'duoqa' in command(peer,'coop.savedb').lower()
    command(peer,'wait.frames 600 120000')
    command(peer,'coop.load Game0 Vantiegan')
    wait_until(lambda:'Space' in command(peer,'game.mode'),'Space load failed',90)
    command(peer,'coop.host 47811 RaidHost')
    wait_until(lambda:f'Reserved founders: {args.seats},' in command(peer,'coop.status'),'Wrong rules',20)
    dismiss_registration(peer)
    dismiss_service_error(peer)
    for wid in ('0445C99D','062EF353','FFFFFFF1'):
        button(peer,wid)
    empire = native(peer,'Simulator::GetPlayerEmpire cdecl')
    home = u32(peer,empire + 0xB4)
    location = command(peer,'coop.space star')
    print('Home',hex(home),'ship',location,flush=True)
    assert home and f'active {home:08X}' in location, 'Start over the homeworld'
    guest = Console(args.guest) if args.guest else None
    if guest:
        assert 'duoqa' in command(guest,'coop.savedb').lower()
        assert 'GalaxyGameEntry' in command(guest,'game.mode')
        command(guest,'coop.join 127.0.0.1:47811 ReturningPilot')
        wait_until(lambda:'Space' in command(guest,'game.mode') and
                   f'Host campaign rules: {args.seats} reserved founders' in command(guest,'coop.status'),
                   'Returning guest did not enter the active Space session',90)
        wait_until(lambda:f'active {home:08X}' in command(guest,'coop.space star'),'Wrong guest system',35)
        if context(guest)==1: print('Guest',command(guest,'coop.space land'),flush=True)
        wait_until(lambda:context(guest)==0,'Guest could not land on homeworld',45)
    if context(peer) == 1:
        print(command(peer,'coop.space land'),flush=True)
    wait_until(lambda:context(peer)==0,'Cannot land at home',45)
    print(command(peer,'coop.space allies raids'),flush=True)
    assert 'no raids' in command(peer,'coop.space allies raids')
    assert 'raid started' in command(peer,f'coop.space allies raid {home:08X} 1')
    def fleet():
        diag = command(peer,'coop.space allies raid_scaling')
        m = re.search(r'scaled raids 1, base ships (\d+), extra ships (\d+)',diag)
        if not m:
            return None
        base, extra = map(int,m.groups())
        assert extra == args.seats - 1
        raid = command(peer,'coop.space allies raids')
        n = re.search(r'bombers 1 ships (\d+)',raid)
        return (base,extra,raid) if n and int(n[1]) == base+extra else None
    print('PASS native base, extras and live hostile ships',wait_until(fleet,'Wrong live pirate count',45),flush=True)
    assert 'scaled raids 1,' in command(peer,'coop.space allies raid_scaling')
    if args.combat:
        rows = command(peer,'coop.fleet list').splitlines()
        ships = [m for line in rows if (m:=re.search(r'ship #(\d+) ([0-9A-Fa-f]+) type=(2|4) pol=([0-9A-F]+)',line))]
        mine = next(line for line in rows if '(me)' in line)
        own = int(re.search(r'ship #-?\d+ ([0-9A-Fa-f]+)',mine)[1],16)
        def value(ptr,offset,client=peer):
            return struct.unpack('<f',mem(client,ptr+offset,4))[0]
        ship = next(m for m in ships if u32(peer,int(m[2],16)+0x800))
        ptr = int(ship[2],16)
        serial = u32(peer,ptr+0x24)
        before, own_max = value(own,0x540),value(own,0x534)
        scaled = value(ptr,0x534)
        assert before > 30
        assert 'hit sent' in command(peer,f'coop.fleet hurt {ship[1]} 0 20')
        damage = before - value(own,0x540)
        assert abs(damage-20*(1+(args.seats-1)*.05)) < .08, ('Native ship HP loss',damage)
        if guest:
            def mirrored():
                result = command(guest,'coop.fleet list')
                match = re.search(rf'ship #{ship[1]} ([0-9A-Fa-f]+) type={ship[3]} ',result)
                return match and abs(value(int(match[1],16),0x534,guest)-scaled)<.02
            wait_until(mirrored,'Native guest pirate max differs from the host',20)
            def guest_hp():
                return float(re.search(r'hp ([\d.]+)',command(guest,'coop.space hp'))[1])
            starting = guest_hp()
            assert 'hit sent' in command(peer,f'coop.fleet hurt {ship[1]} 1 20')
            wait_until(lambda:abs(starting-guest_hp()-20*(1+(args.seats-1)*.05))<.11,
                       'Guest did not receive exactly one scaled pirate hit',10)
            assert 'total faults: 0' in command(guest,'exc')
            print('PASS guest mirrored native max and received a single scaled hit.',flush=True)
        command(peer,'coop.leave')
        assert f'{ptr:08X}' in command(peer,'coop.fleet list').upper()
        assert u32(peer,ptr+0x24) == serial, 'Pirate was removed before checking native HP'
        vanilla = value(ptr,0x534)
        assert abs(scaled - vanilla*(1+(args.seats-1)*.15)) < .05, (scaled,vanilla)
        assert value(own,0x534) == own_max
        print('PASS pirate max HP',vanilla,'->',scaled,'enemy damage 20 ->',damage,flush=True)
    assert 'total faults: 0' in command(peer,'exc')


if __name__ == '__main__':
    main()
