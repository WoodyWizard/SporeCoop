"""Exercise the native military AI purchase loop on a disposable, loaded Clyth save."""
import argparse
import json
from pathlib import Path
import re
import struct
import time
import uifind

from coop_ability_bar import mem, u32
from coop_sync import game_paused
from duo_civ_founder_control import cities
from duo_civ_team_progress import country
from duo_native_ui import dismiss_registration, dismiss_service_error
from duo_orders import wait_until
from duo_tribe_graduation_editor import button
from duo_villages import Console, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int)
    parser.add_argument('--seats', type=int, choices=(2,4), default=4)
    parser.add_argument('--scaling', type=int, default=100)
    parser.add_argument('--mixed-fleet', action='store_true')
    parser.add_argument('--live', action='store_true', help='Let the normal unpaused nation AI replace a second loss.')
    receipts = parser.add_mutually_exclusive_group()
    receipts.add_argument('--save-receipt',type=Path)
    receipts.add_argument('--cold-receipt',type=Path)
    args = parser.parse_args()
    if args.save_receipt and not args.mixed_fleet: parser.error('--save-receipt requires --mixed-fleet')
    c = Console(args.host)
    db = command(c,'coop.savedb')
    assert 'duoqalineagescivprod' in db.lower()
    if 'GalaxyGameEntry' in command(c,'game.mode'):
        command(c,'coop.load Game0 Clyth')
    wait_until(lambda:'Civ' in command(c,'game.mode'),'Missing Civilization stage',90)
    db = command(c,'coop.savedb')
    path = Path(re.search(r'location=(.+?)\s+access=',db)[1])
    sig, _, _, home, partner = struct.unpack('<4s4I',(path/'clyth.spo.duonations').read_bytes())
    assert sig == b'CIV1' and home != partner
    dismiss_registration(c)
    dismiss_service_error(c)
    for wid in ('0445C99D','062EF353'): button(c,wid)
    command(c,'coop.leave')
    nations = {pol:city for _,city,pol,_ in cities(c)}
    rival = next(pol for pol in nations if pol not in (home,partner))
    n = country(c,rival)
    cap = 'SporeCoop::CivVehicleLimit'
    def call(func, obj, *values):
        text = command(c,' '.join(['call',str(func),'thiscall',hex(obj),*map(str,values)]))
        return int(re.search(r'eax=0x([0-9a-f]+)',text,re.I)[1],16)
    def put(ptr, fmt, *values):
        command(c,f'mem.write {ptr:#x} {struct.pack(fmt,*values).hex()}')
    def limits(pol):
        nation = country(c,pol)
        return [call(cap,nation,purpose) for purpose in (0,1,2)]
    def fleet():
        start,end = struct.unpack('<II',mem(c,n+0xB0,8))
        assert 0 <= end-start <= 47*4 and (end-start)%4 == 0
        return struct.unpack(f'<{(end-start)//4}I',mem(c,start,end-start)) if end > start else ()
    def military():
        return {v for v in fleet() if u32(c,v+0xB20) == 0 and not mem(c,v+0xBCD,1)[0]}
    def slots():
        return call('Simulator::cCivilization::VehicleSlotsLeft',n,0)
    def fleet_receipt():
        return sorted((u32(c,v+0x24),u32(c,v+0xB20)) for v in fleet())
    if args.cold_receipt:
        expected = json.loads(args.cold_receipt.read_text())
        assert rival == expected['rival'] and len(expected['vehicles']) > 36
        assert [list(row) for row in fleet_receipt()] == expected['vehicles']
        assert slots() == 0
        assert 'total faults: 0' in command(c,'exc')
        print('PASS cold native save restored every vehicle serial and purpose:',len(fleet()),flush=True)
        return
    base = {pol:limits(pol) for pol in nations}
    assert 0 < base[rival][0] < 30 and u32(c,nations[rival]+0x540) == 0
    command(c,'coop.host 47812 ProductionHost')
    assert f'Player slots: {args.seats}, difficulty scaling {args.scaling}%' in command(c,'coop.status')
    scaling = (args.seats-1)*args.scaling
    maximum = min(47,base[rival][0] + (base[rival][0]*scaling+999)//1000)
    threshold = max(1,40000 - 40*scaling)
    wait_until(lambda:limits(rival)[0] == maximum,'Rival production cap did not increase',30)
    assert limits(home) == base[home] and limits(partner) == base[partner]
    assert limits(rival)[1:] == base[rival][1:]
    print('PASS military allowance',base[rival][0],'->',maximum,flush=True)
    guest = Console(args.guest) if args.guest else None
    if guest:
        assert 'duoqalineagescivprod' in command(guest,'coop.savedb').lower()
        command(guest,'coop.join 127.0.0.1:47812 ProductionGuest')
        wait_until(lambda:'Civ' in command(guest,'game.mode') and
                   f'Host campaign rules: {args.seats} player slots' in command(guest,'coop.status'),
                   'Guest did not join the reserved campaign',100)

    def vehicle_ids(peer):
        return {int(i) for i in re.findall(rf'veh #(\d+) \w+ pol={rival:08X} [^\n]*?purpose=0 ',command(peer,'coop.civ'),re.I) if int(i)}

    # Supply a developed opponent, a military capital with a reachable target,
    # and a fixed native expansion budget. The AI chooses and pays for every vehicle.
    put(country(c,home)+0x8D,'<B',1)
    if not game_paused(c):
        command(c,'key esc')
    wait_until(lambda:game_paused(c),'Simulation must be paused for exact boundary and budget readings',8)
    target = nations[partner]
    assert mem(c,country(c,partner)+0x8A,1) == b'\x01'  # native neutral target avoids urgent replanning
    for field in (0x458,0x460):
        existing = u32(c,n+field)
        if existing == target: continue
        assert existing == 0, ('Use a fresh rival fixture',field,existing)
        call(hex(u32(c,u32(c,target))),target)  # city AddRef for this intrusive pointer
        put(n+field,'<I',target)
    put(n+0x298,'<I',3)  # air access makes city targeting independent of coastlines
    delta = int(command(c,'sym '+cap).split()[0],16) - 0xBF2110
    ai = hex(0xBFA7A0 + delta)
    assert mem(c,int(ai,16),4) == bytes.fromhex('83ec2453')
    def set_clock(ms):
        put(n+0x148+8,'<QQB',ms,0,0)
    def budget():
        return struct.unpack('<f',mem(c,n+0x4B0,4))[0]
    set_clock(0)
    put(n+0x4B0,'<f',100000.)
    before = military()
    assert len(before) < base[rival][0]
    set_clock(threshold)
    call(ai,n,0)
    assert military() == before, ('Purchased too soon',len(before),len(military()),budget())
    paid_from = budget()
    set_clock(threshold+1)
    call(ai,n,0)
    bought = military()
    spent = paid_from - budget()
    assert len(bought) == maximum and slots() == 0 and spent > 0, (len(before),len(bought),maximum,spent)
    price = spent / (len(bought)-len(before))
    print('PASS native purchases at',threshold+1,'ms',len(before),'->',len(bought),'price',price,flush=True)
    if guest:
        wait_until(lambda:len(vehicle_ids(c)) == maximum and vehicle_ids(c) == vehicle_ids(guest),
                   'Guest did not mirror the purchased military vehicles',20)
    victim = next(iter(bought-before))
    old_serial = u32(c,victim+0x24)
    old_ids = vehicle_ids(c)
    put(n+0x4B0,'<f',0.)
    founder_vehicle = int(re.search(rf'veh #\d+ ([0-9A-F]+) pol={partner:08X} ',command(c,'coop.civ'),re.I)[1],16)
    def lethal_hit(vehicle):
        combat = vehicle+0x508
        call(hex(u32(c,u32(c,combat)+0x18)),combat,'f:50000',partner,0,hex(founder_vehicle+0x38),hex(founder_vehicle+0x508))
    lethal_hit(victim)
    print('After native lethal hit: slots',slots(),'alive',len(military()),'max',limits(rival)[0],flush=True)
    if slots() == 0:
        assert uifind.press(c,'Resume Game'), 'Native pause menu did not resume'
        def retired():
            current = slots()
            if current > 1: raise AssertionError(('Unexpected extra slots',current,len(military()),limits(rival)))
            return current == 1
        try:
            wait_until(retired,'Native vehicle death did not reopen one slot',30)
        finally:
            print('After cleanup: slots',slots(),'alive',len(military()),'max',limits(rival)[0],
                  'paused',game_paused(c),'budget',budget(),flush=True)
        command(c,'key esc')
        wait_until(lambda:game_paused(c),'Pause after native death',8)
    put(n+0x4B0,'<f',price)
    paid_from = budget()
    set_clock(threshold+1)
    call(ai,n,0)
    assert len(military()) == maximum and old_serial not in {u32(c,v+0x24) for v in military()} and slots() == 0
    assert abs((paid_from-budget())-price) < 2, ('Replacement price',price,paid_from-budget())
    if guest:
        wait_until(lambda:len(vehicle_ids(c)-old_ids) == 1 and vehicle_ids(c) == vehicle_ids(guest),
                   'Guest did not converge on the replacement fleet',20)
        assert 'total faults: 0' in command(guest,'exc')
    def autonomous_refill(expected_military):
        lethal_hit(next(iter(military())))
        assert slots() == 1
        put(n+0x4B0,'<f',price*3)
        call(hex(0xBC32A0+delta),n+0x148)  # native stopwatch reset/start
        if args.mixed_fleet:
            # Isolate a scheduled purchase before a massed economic fleet can
            # capture a city and inherit its vehicles, changing the premise.
            put(n+0x148+8,'<Q',max(0,threshold-1000))
        old_serials = {u32(c,v+0x24) for v in military()}
        expected_total = len(fleet())+1
        assert uifind.press(c,'Resume Game')
        peak_elapsed = 0
        def rebuilt():
            nonlocal peak_elapsed
            peak_elapsed = max(peak_elapsed,call('SporeCoop::GonzagoElapsedMs',n+0x148))
            if len(fleet()) > expected_total:
                raise AssertionError(('Fleet composition changed during the live purchase',
                                      len(fleet()),len(military()),slots()))
            return len(military()) == expected_military
        start = time.monotonic()
        wait_until(rebuilt,'Unpaused nation AI did not refill the open military slot',150)
        assert len(fleet()) == expected_total
        assert len({u32(c,v+0x24) for v in military()} - old_serials) == 1
        assert any(pol == partner for _,_,pol,_ in cities(c)), 'Target was captured during purchase test'
        print('PASS autonomous host refill after',round(time.monotonic()-start,1),
              'real seconds; highest observed native clock',peak_elapsed,flush=True)
        command(c,'key esc')
        wait_until(lambda:game_paused(c),'Pause after autonomous purchase',8)
    if args.live and not args.mixed_fleet:
        autonomous_refill(maximum)
    if args.mixed_fleet:
        # Fill the national limit using nonmilitary vehicles from a stock
        # design. Only this capacity fixture bypasses the purchase routine.
        extra = maximum-base[rival][0]
        for vehicle in list(military())[:extra]:
            lethal_hit(vehicle)
        assert slots() == extra
        did = command(c,'call Simulator::VehicleDesignId cdecl 2 0')
        design_id = re.search(r'eax=(0x[0-9a-f]+)',did,re.I)[1]
        key = call('Simulator::cCivilization::GetDesignKey',country(c,home),design_id)
        model = struct.unpack('<III',mem(c,key,12))
        assert model[0]
        for expected in range(len(fleet())+1,37):
            call('Simulator::cCity::SpawnVehicle',nations[rival],2,0,*map(hex,model),0)
            assert len(fleet()) == expected and slots() == (min(extra,11) if expected == 36 else extra)
        overflow = min(extra,11)
        expected_military = base[rival][0]+overflow
        def civilian_slots():
            return [call('Simulator::cCivilization::VehicleSlotsLeft',n,p) for p in (1,2)]
        assert civilian_slots() == [0,0]
        put(n+0x4B0,'<f',price*(overflow+2))
        set_clock(threshold+1)
        call(ai,n,0)
        assert len(fleet()) == 36+overflow and len(military()) == expected_military and budget() == price*2
        assert slots() == 0 and civilian_slots() == [0,0]
        set_clock(threshold+1)
        call(ai,n,0)
        assert len(fleet()) == 36+overflow and budget() == price*2
        def synced():
            ids = vehicle_ids(c)
            if len(ids) != expected_military: return False
            if not guest: return True
            def census(peer):
                return sorted(int(net_id) for net_id in re.findall(
                    rf'^obj v(-?\d+) key={rival:08X} alive=1 ',command(peer,'coop.census'),re.M))
            all_ids = census(c)
            return (len(all_ids) == len(set(all_ids)) == 36+overflow and all_ids[0] > 0
                    and all_ids == census(guest) and ids == vehicle_ids(guest))
        wait_until(synced,'Overflow military fleet did not replicate',30)
        previous = vehicle_ids(c)
        lethal_hit(next(iter(military())))
        assert slots() == 1 and civilian_slots() == [0,0]
        set_clock(threshold+1)
        call(ai,n,0)
        assert len(fleet()) == 36+overflow and slots() == 0 and budget() == price
        wait_until(lambda:synced() and len(vehicle_ids(c)-previous) == 1,'Missing overflow replacement',30)
        print('PASS paid military overflow, replacement, civilian exclusion and ceiling:',36+overflow,flush=True)
        if args.live:
            autonomous_refill(expected_military)
            assert slots() == 0 and civilian_slots() == [0,0]
        if args.save_receipt:
            assert not args.save_receipt.exists()
            planet = path/'Clyth.spo'
            stamp = planet.stat().st_mtime_ns
            receipt = fleet_receipt()
            print(command(c,'coop.save'),flush=True)
            wait_until(lambda:planet.stat().st_mtime_ns != stamp,'Native overflow save did not finish',30)
            command(c,'coop.civ')
            args.save_receipt.write_text(json.dumps(dict(rival=rival,vehicles=receipt)))
    assert 'total faults: 0' in command(c,'exc')
    print('PASS lost vehicle replaced with a paid purchase at full native capacity plus bonus.',flush=True)


if __name__ == '__main__':
    main()
