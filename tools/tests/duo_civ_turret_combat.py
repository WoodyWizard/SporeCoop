"""Qualify one armed city turret on a disposable Clyth save with two founders and a rival."""
import argparse
import json
from pathlib import Path
import re
import struct

from coop_ability_bar import mem, u32
from duo_civ_founder_control import cities
from duo_civ_vehicle_combat import health, hit
from duo_native_ui import dismiss_registration, dismiss_service_error
from duo_orders import wait_until
from duo_tribe_graduation_editor import button
from duo_villages import Console, command


def turret_list(peer, city):
    begin, end = struct.unpack('<II', mem(peer, city + 0x354, 8))
    assert 0 <= end-begin <= 32*4 and (end-begin) % 4 == 0
    return [u32(peer, slot) for slot in range(begin, end, 4)]


def wall(peer, city):
    first, end = struct.unpack('<II',mem(peer,city+0x504,8))
    assert 0 <= end-first <= 32*0x18 and (end-first)%0x18 == 0
    data = mem(peer,first,end-first) if end>first else b''
    return [struct.unpack_from('<I',data,i+4)[0] for i in range(0,len(data),0x18)]


def shoot(peer, turret, target, faction):
    before = health(peer, target, 0x540)
    assert before > 20
    combat = target + 0x508
    damage = u32(peer, u32(peer, combat) + 0x18)
    command(peer, f'call {damage:#x} thiscall {combat:#x} f:10 {faction:#x} 0 '
                  f'{turret+0x38:#x} {turret+0x588:#x}')
    return before - health(peer, target, 0x540)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True, type=int)
    parser.add_argument('--guest', type=int)
    receipts = parser.add_mutually_exclusive_group()
    receipts.add_argument('--save-receipt',type=Path)
    receipts.add_argument('--cold-receipt',type=Path)
    parser.add_argument('--battle', action='store_true', help='Exercise autonomous turret fire after a cold load.')
    args = parser.parse_args()
    if args.battle and not args.cold_receipt: parser.error('--battle requires --cold-receipt')
    peer = Console(args.host)
    wait_until(lambda: peer.call('ping')['ok'], 'No QA console', 60)
    wait_until(lambda: 'GalaxyGameEntry' in command(peer,'game.mode'), 'No galaxy menu', 90)
    assert 'duoqa' in command(peer,'coop.savedb').lower()
    command(peer,'wait.frames 600 120000')
    command(peer,'coop.load Game0 Clyth')
    wait_until(lambda: 'Civilization' in peer.call('coop.avatar')['out'], 'Missing Civilization save', 90)
    location = re.search(r'location=(.+?)\s+access=',command(peer,'coop.savedb'))[1]
    tag, _, _, home, partner = struct.unpack('<4s4I',(Path(location)/'clyth.spo.duonations').read_bytes())
    assert tag == b'CIV1'
    rival, faction = next((ptr,pol) for _,ptr,pol,_ in cities(peer) if pol not in (home,partner))
    if args.cold_receipt:
        saved = json.loads(args.cold_receipt.read_text())
        assert faction == saved['faction']
        turret = wall(peer,rival)[saved['slot']]
        assert turret and u32(peer,turret+0x24) == saved['serial']
        native_hp = health(peer,turret,0x5C0)
        assert abs(health(peer,turret,0x5B4)-saved['max']) < .05
        assert abs(native_hp-saved['hp']) < 2, (native_hp,saved)
        print('PASS cold offline turret, wall slot, base max and wounded HP',saved,native_hp,flush=True)
        if args.battle:
            command(peer,'coop.host 47812 WallBattleHost')
            assert 'Player slots: 4,' in command(peer,'coop.status')
            dismiss_registration(peer)
            dismiss_service_error(peer)
            for wid in ('0445C99D','062EF353'): button(peer,wid)
            rows = command(peer,'coop.civ')
            attacker = int(re.search(rf'veh #\d+ ([0-9A-F]+) pol={partner:08X} ',rows,re.I)[1],16)
            victim_id = u32(peer,attacker+0x24)
            command(peer,'wait.frames 100 20000')
            start = health(peer,attacker,0x540)
            spatial = attacker + 0x34
            teleport = u32(peer,u32(peer,spatial)+0x44)
            command(peer,f'call {teleport:#x} thiscall {spatial:#x} {turret+0x38:#x} {turret+0x44:#x}')
            command(peer,f'call Simulator::cVehicle::SetOrder thiscall {attacker:#x} {turret:#x} 1 1')
            command(peer,f'call Simulator::cVehicle::OrderStamp thiscall {attacker:#x} 0')
            def fired():
                msg = command(peer,'coop.civ defenses')
                row = re.search(r'turret ([0-9A-F]+), victim ([0-9A-F]+), damage ([\d.]+) -> ([\d.]+)',msg)
                if not row or int(row[1],16) != saved['serial'] or int(row[2],16) != victim_id: return None
                raw, applied = float(row[3]), float(row[4])
                if raw > 0 and abs(applied-raw*1.15) < .01 and health(peer,attacker,0x540) < start-1: return msg
            print('PASS autonomous native turret',wait_until(fired,'No confirmed autonomous turret shot',45),flush=True)
        assert 'total faults: 0' in command(peer,'exc')
        return
    rows = command(peer,'coop.civ')
    target = int(re.search(rf'veh #\d+ ([0-9A-F]+) pol={partner:08X} ',rows,re.I)[1],16)
    # Resolve ASLR using the active build's verified vehicle vtable slot.
    base_delta = u32(peer,u32(peer,target+0x508)+0x18) - 0x00CAB6F0
    prior = set(turret_list(peer,rival))
    dismiss_registration(peer)
    dismiss_service_error(peer)
    for wid in ('0445C99D','062EF353'): button(peer,wid)
    assert mem(peer,base_delta+0xBE39A0,5) == bytes.fromhex('83ec445657')
    command(peer,f'call {base_delta+0xBE39A0:#x} thiscall {rival:#x}')
    placed = set(turret_list(peer,rival)) - prior
    assert len(placed) == 1, ('No free wall slot in the disposable rival city',placed)
    turret = placed.pop()
    assert u32(peer,turret+0x688) == rival
    assert u32(peer,turret+0x588) == base_delta+0x01466C28
    assert u32(peer,u32(peer,turret+0x588)+0x18) == base_delta+0xBD0660
    slot = wall(peer,rival).index(turret)
    base = health(peer,turret,0x5B4)
    vanilla = shoot(peer,turret,target,faction)
    rival_vehicle = int(re.search(rf'veh #\d+ ([0-9A-F]+) pol={faction:08X} ',rows,re.I)[1],16)
    vehicle_vanilla = hit(peer,rival_vehicle,target)
    command(peer,'coop.host 47812 TurretHost')
    assert 'Player slots: 4,' in command(peer,'coop.status')
    command(peer,'wait.frames 100 20000')
    guest = Console(args.guest) if args.guest else None
    mirror = None
    if guest:
        assert 'duoqa' in command(guest,'coop.savedb').lower()
        command(guest,'coop.join 127.0.0.1:47812 TurretGuest')
        wait_until(lambda: 'Civ' in command(guest,'game.mode') and
                   'Host campaign rules: 4 player slots' in command(guest,'coop.status'),
                   'Guest did not enter four-seat campaign',100)
        vehicle_id = re.search(rf'veh #(\d+) 0*{target:X} ',command(peer,'coop.civ'),re.I)[1]
        def target_mirror():
            row = re.search(rf'veh #{vehicle_id} ([0-9A-F]+) ',command(guest,'coop.civ'),re.I)
            return int(row[1],16) if row else None
        mirror = wait_until(target_mirror,'Missing founder vehicle on guest',20)
    hosted = shoot(peer,turret,target,faction)
    vehicle_hosted = hit(peer,rival_vehicle,target)
    assert abs(vehicle_hosted-vehicle_vanilla*1.15) < .55, (vehicle_vanilla,vehicle_hosted)
    print('PASS military vehicle hit',vehicle_vanilla,vehicle_hosted,flush=True)
    print('Native rival turret hits', vanilla, hosted, 'owner', hex(faction),
          'base max HP',health(peer,turret,0x5B4),flush=True)
    assert vanilla > 0 and abs(hosted-vanilla*1.15) < .55, (vanilla,hosted)
    boosted = health(peer,turret,0x5B4)
    assert abs(boosted-base*1.45) < .05, ('Turret max',base,boosted)
    if guest:
        guest_city = next(ptr for _,ptr,pol,_ in cities(guest) if pol == faction)
        def synced():
            other = wall(guest,guest_city)[slot]
            return other if other and abs(health(guest,other,0x5B4)-boosted) < .05 else None
        other = wait_until(synced,'Missing rival turret max on guest',20)
        gun = u32(peer,u32(peer,turret+0x588)+0x18)
        prior_hp = health(peer,turret,0x5C0)
        command(peer,f'call {gun:#x} thiscall {turret+0x588:#x} f:30 {partner:#x} 0 '
                     f'{target+0x38:#x} {target+0x508:#x}')
        now_hp = health(peer,turret,0x5C0)
        assert now_hp < prior_hp - 10, (prior_hp,now_hp)
        wait_until(lambda:abs(health(guest,other,0x5C0)-health(peer,turret,0x5C0))<2,
                   'Host turret wound did not reach the guest',20)
        old = set(turret_list(peer,rival))
        command(peer,f'call {base_delta+0xBE39A0:#x} thiscall {rival:#x}')
        extra, = set(turret_list(peer,rival))-old
        extra_slot = wall(peer,rival).index(extra)
        wait_until(lambda:wall(guest,guest_city)[extra_slot],'Missing newly built NPC defense',15)
        command(peer,f'call {base_delta+0xBE21C0:#x} thiscall {rival:#x} {extra:#x} 1')
        wait_until(lambda:not wall(guest,guest_city)[extra_slot],'Removed NPC defense persisted on guest',15)
        print('PASS exact turret wall slot, wound, new placement and removal',flush=True)
        wait_until(lambda:abs(health(guest,mirror,0x540)-health(peer,target,0x540)) < 1,
                   'Guest did not receive the turret hit',15)
        assert 'total faults: 0' in command(guest,'exc')
        print('PASS guest mirrored the damaged founder vehicle',flush=True)
    if args.save_receipt:
        assert not args.save_receipt.exists()
        planet = Path(location) / 'Clyth.spo'
        stamp = planet.stat().st_mtime_ns
        before_save = health(peer,turret,0x5C0)
        assert 0 < before_save < boosted, (before_save,boosted)
        print(command(peer,'coop.save'),flush=True)
        wait_until(lambda:planet.stat().st_mtime_ns != stamp,'No native turret save',30)
        command(peer,'coop.civ')  # Wait until the game thread exits the serializer.
        assert abs(health(peer,turret,0x5B4)-boosted) < .05
        args.save_receipt.write_text(json.dumps(dict(faction=faction,slot=slot,max=base,
                                     serial=u32(peer,turret+0x24),hp=before_save/1.45)))
    command(peer,'coop.leave')
    assert health(peer,turret,0x5B4) == base
    print('PASS rival turret max',base,'->',boosted,'->',base,flush=True)
    assert 'total faults: 0' in command(peer,'exc')


if __name__ == '__main__':
    main()
