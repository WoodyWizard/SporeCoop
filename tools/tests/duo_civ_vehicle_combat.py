"""Measure native vehicle damage in a disposable Clyth campaign with two founder nations."""
import argparse
import json
from pathlib import Path
import re
import struct

from coop_ability_bar import mem, u32
from duo_civ_founder_control import cities
from duo_civ_species import native
from duo_native_ui import dismiss_registration, dismiss_service_error
from duo_orders import wait_until
from duo_tribe_graduation_editor import button
from duo_villages import Console, command


def health(peer, vehicle, offset):
    return struct.unpack('<f', mem(peer, vehicle + offset, 4))[0]


def effective_max(peer, vehicle):
    row = re.search(rf'veh #\d+ 0*{vehicle:X} [^\n]*?max_hp=([\d.]+)',command(peer,'coop.civ'),re.I)
    assert row, f'Missing vehicle {vehicle:X}'
    return float(row[1])


def hit(host, source, target, points=10):
    def hp():
        return health(host,target,0x540)
    before = hp()
    assert before > points, (before,points)
    combat = target + 0x508
    func = u32(host, u32(host, combat) + 0x18)
    faction = u32(host, source + 0x30)
    command(host, f'call {func:#x} thiscall {combat:#x} f:{points} {faction:#x} 0 '
                  f'{source+0x38:#x} {source+0x508:#x}')
    return before - hp()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int)
    receipt = parser.add_mutually_exclusive_group()
    receipt.add_argument('--save-receipt', type=Path)
    receipt.add_argument('--cold-receipt', type=Path)
    parser.add_argument('--battle', action='store_true', help='After cold load, order a native vehicle attack.')
    args = parser.parse_args()
    if args.battle and not args.cold_receipt: parser.error('--battle requires --cold-receipt')
    h = Console(args.host)
    wait_until(lambda: h.call('ping')['ok'], 'Missing QA console', 60)
    wait_until(lambda: 'GalaxyGameEntry' in command(h,'game.mode'), 'No galaxy menu', 90)
    assert 'duoqa' in command(h,'coop.savedb').lower()
    command(h,'wait.frames 600 120000')
    command(h,'coop.load Game0 Clyth')
    wait_until(lambda: 'Civilization' in h.call('coop.avatar')['out'], 'Missing Civilization save', 90)
    if args.cold_receipt:
        saved = json.loads(args.cold_receipt.read_text())
        rows = re.findall(r'veh #\d+ ([0-9A-F]+) pol=([0-9A-F]+)',command(h,'coop.civ'),re.I)
        vehicles = [int(v,16) for v,pol in rows if int(pol,16) == saved['pol']]
        enemy = next((v for v in vehicles if u32(h,v+0x24)==saved['serial']),None)
        assert enemy, ('Missing saved military vehicle',saved,vehicles)
        assert health(h,enemy,0x534) == saved['max']
        assert abs(effective_max(h,enemy)-saved['effective']) < .05
        now_hp = health(h,enemy,0x540)
        assert abs(now_hp-saved['hp']) < 2, (now_hp,saved)
        print('PASS cold offline native vehicle max and wounded HP',saved,now_hp,flush=True)
        if args.battle:
            command(h,'coop.host 47812 BattleHost')
            assert 'Player slots: 4,' in command(h,'coop.status')
            dismiss_registration(h)
            dismiss_service_error(h)
            for wid in ('0445C99D','062EF353'): button(h,wid)
            location = re.search(r'location=(.+?)\s+access=',command(h,'coop.savedb'))[1]
            signature, _, _, _, partner = struct.unpack('<4s4I',(Path(location)/'clyth.spo.duonations').read_bytes())
            assert signature == b'CIV1'
            target = next(int(v,16) for v,pol in rows if int(pol,16)==partner)
            command(h,'wait.frames 100 20000')
            initial_hp = health(h,target,0x540)
            spatial = enemy + 0x34
            teleport = u32(h,u32(h,spatial)+0x44)
            command(h,f'call {teleport:#x} thiscall {spatial:#x} {target+0x38:#x} {target+0x44:#x}')
            command(h,f'call Simulator::cVehicle::SetOrder thiscall {enemy:#x} {target:#x} 1 1')
            command(h,f'call Simulator::cVehicle::OrderStamp thiscall {enemy:#x} 0')
            print('Native attack order',hex(enemy),'->',hex(target),'target HP',initial_hp,flush=True)
            def fired():
                # Real native weapon and projectile. Never invoke TakeDamage here.
                if health(h,target,0x540) >= initial_hp-1: return False
                return abs(effective_max(h,enemy)-saved['effective']*1.45) < .05
            wait_until(fired,'Native attack did not damage the partner and activate rival health',35)
            print('PASS native military attack, founder HP',initial_hp,'->',health(h,target,0x540),flush=True)
        assert 'total faults: 0' in command(h,'exc')
        return
    command(h,'coop.host 47812 BalanceHost')
    assert 'Player slots: 4,' in command(h,'coop.status')
    dismiss_registration(h)
    dismiss_service_error(h)
    location = re.search(r'location=(.+?)\s+access=',command(h,'coop.savedb'))[1]
    signature, _, _, home, guest = struct.unpack('<4s4I',(Path(location)/'clyth.spo.duonations').read_bytes())
    assert signature == b'CIV1' and home != guest
    def settled():
        button(h,'0445C99D')
        button(h,'062EF353')
        return 'busy: -' in command(h,'coop.status')
    wait_until(settled,'Unfinished native cinematic',45)
    def nations():
        return {pol:ptr for _,ptr,pol,_ in cities(h)}
    if not (nations().keys() - {home,guest}):
        expansion = native(h,'Simulator::cCivExpansion::Get cdecl')
        command(h,f'call Simulator::cCivExpansion::SpawnNextCity thiscall {expansion:#x} 0 0')
    rival = wait_until(lambda: next(iter(nations().keys()-{home,guest}),None),'No rival nation',30)
    wait_until(settled,'Native rival founding unfinished',45)
    design_id = native(h,'Simulator::VehicleDesignId cdecl 0 0')
    def design(pol):
        nation = u32(h,nations()[pol]+0x590)
        address = native(h,f'Simulator::cCivilization::GetDesignKey thiscall {nation:#x} {design_id:#x}')
        return struct.unpack('<III',mem(h,address,12))
    designs = [design(pol) for pol in (rival,home,guest)]
    shared_design = next((key for key in designs if key[0]),None)
    assert shared_design, ('No native land military design',designs)
    def vehicle(pol):
        key = design(pol)
        if not key[0]: key = shared_design
        model = ' '.join(hex(word) for word in key)
        ptr = native(h,f'Simulator::cCity::SpawnVehicle thiscall {nations()[pol]:#x} 0 0 {model} 0')
        assert u32(h,ptr+0x30) == pol and u32(h,ptr+0xB20) == 0
        return ptr
    enemy, host_vehicle, guest_vehicle = [vehicle(pol) for pol in (rival,home,guest)]
    command(h,'coop.leave')
    base_max = health(h,enemy,0x534)
    base_effective = effective_max(h,enemy)
    founder_max = health(h,guest_vehicle,0x534)
    vanilla = hit(h,enemy,guest_vehicle)
    command(h,'coop.host 47812 BalanceHost')
    wait_until(lambda: 'founder_control=1' in command(h,'coop.civ nations') or
               'nation_pol=' in command(h,'coop.civ nations'),'Founder data unavailable',15)
    # The host restores the saved founder table on its regular two-second tick.
    command(h,'wait.frames 90 20000')
    peer = Console(args.guest) if args.guest else None
    if peer:
        assert 'duoqa' in command(peer,'coop.savedb').lower()
        assert 'GalaxyGameEntry' in command(peer,'game.mode')
        command(peer,'coop.join 127.0.0.1:47812 BalanceGuest')
        wait_until(lambda: 'Civ' in command(peer,'game.mode') and
                   'Host campaign rules: 4 player slots' in command(peer,'coop.status'),
                   'Guest did not enter the host campaign',100)
        wait_until(settled,'Join cinematic unfinished',45)
    serial = u32(h,enemy+0x24)
    scaled = hit(h,enemy,guest_vehicle)
    friendly = hit(h,host_vehicle,guest_vehicle)
    assert vanilla > 0 and abs(scaled-vanilla*1.15) < .05,(vanilla,scaled,friendly)
    assert friendly <= vanilla+.05,(vanilla,scaled,friendly)
    boosted_max = health(h,enemy,0x534)
    assert abs(boosted_max-base_max*1.45) < .05, (boosted_max,base_max)
    boosted_effective = effective_max(h,enemy)
    print('Native effective max', base_effective, '->', boosted_effective,flush=True)
    assert abs(boosted_effective-base_effective*1.45)<.05, (base_effective,boosted_effective)
    assert health(h,guest_vehicle,0x534) == founder_max
    if peer:
        def mirror(vehicle):
            row = re.search(rf'veh #(\d+) 0*{vehicle:X} ',command(h,'coop.civ'),re.I)
            if row and int(row[1]):
                m = re.search(rf'veh #{row[1]} ([0-9A-F]+) ',command(peer,'coop.civ'),re.I)
                if m: return int(m[1],16)
        enemy_mirror = wait_until(lambda:mirror(enemy),'Missing rival mirror',25)
        wait_until(lambda:abs(health(peer,enemy_mirror,0x534)-boosted_max)<.05,
                   'Guest rival max differs from the host',20)
        assert abs(effective_max(peer,enemy_mirror)-boosted_effective)<.05
        founder_mirror = wait_until(lambda:mirror(guest_vehicle),'Missing founder mirror',20)
        assert health(peer,founder_mirror,0x534) == founder_max
        wait_until(lambda:abs(health(peer,founder_mirror,0x540)-health(h,guest_vehicle,0x540))<1,
                   'Guest current HP differs from the host',15)
        assert 'total faults: 0' in command(peer,'exc')
        print('PASS guest rival max and founder current HP',flush=True)
    if args.save_receipt:
        assert not args.save_receipt.exists()
        # Seed one exact wound in this disposable fixture. AI repair can occur
        # between control calls; normal combat was measured separately above.
        hp = boosted_effective * .40
        command(h,f'mem.write {enemy+0x540:#x} {struct.pack("<f",hp).hex()}')
        assert abs(health(h,enemy,0x540)-hp) < 1
        print('Wounded vehicle before save',hex(serial),hp,'of',boosted_effective,flush=True)
        planet_file = Path(location) / 'Clyth.spo'
        old_time = planet_file.stat().st_mtime_ns
        print(command(h,'coop.save'),flush=True)
        wait_until(lambda:planet_file.stat().st_mtime_ns != old_time,'Native save did not finish',25)
        # mem.read can run on the IPC thread while SaveGame is still serializing.
        # coop.civ runs on the game thread, after the save callback has returned.
        assert abs(effective_max(h,enemy)-boosted_effective) < .05
        after_max = health(h,enemy,0x534)
        print('Host after save',hex(u32(h,enemy+0x24)),after_max,health(h,enemy,0x540),flush=True)
        args.save_receipt.write_text(json.dumps(dict(pol=rival,serial=serial,max=base_max,
                                     effective=base_effective,hp=hp/1.45)))
        assert abs(after_max-boosted_max) < .01, (after_max,boosted_max)
    command(h,'coop.leave')
    assert u32(h,enemy+0x24) == serial and health(h,enemy,0x534) == base_max
    print('PASS rival max HP',base_max,'->',boosted_max,'->',base_max,flush=True)
    assert 'total faults: 0' in command(h,'exc')
    print('PASS vanilla rival hit',vanilla,'four-seat hit',scaled,'founder hit',friendly,flush=True)


if __name__ == '__main__':
    main()
