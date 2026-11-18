"""Targeted native guest preaching and final conversion at a saved checkpoint.

Fixture: clear the rival's wall turrets and nearby defenders, spawn a religious vehicle
using its native religious design, place it near the rival and seed followers
just below the native 50% majority. The final conversion
must come from the native AI after guest mouse input, not ConvertCitizens.
"""
import argparse,math,random,re,struct,time
import play_civ
from coop_ability_bar import mem,u32
from duo_civ_capture_owners import ownership
from duo_civ_founder_control import cities
from duo_civ_mouse_combat import current_order,vehicle
from duo_civ_species import native
from duo_civ_takeover import followers
from duo_civ_turret_combat import turret_list
from duo_orders import wait_until
from duo_tribe_graduation_editor import button
from duo_villages import Console,command


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',type=int,required=True);p.add_argument('--guest',type=int,required=True)
    p.add_argument('--preachers',type=int,default=4,choices=range(1,9))
    p.add_argument('--cold-city',action='store_true',help='Require untouched native preaching slots and a stationary host camera')
    a=p.parse_args();h,g=Console(a.host),Console(a.guest)
    for c in(h,g):
        assert 'duoqa' in command(c,'coop.savedb').lower()
        assert "world 'Clyth.spo'" in command(c,'coop.avatar')
    own=next(row for row in cities(h) if row[3]==1)
    rival=next(row for row in cities(h) if row[3]==-1)
    def capacities():
        begin,end=struct.unpack('<II',mem(h,rival[1]+0x618,8))
        assert begin and end-begin==4*128
        return [struct.unpack('<2i',mem(h,at,8)) for at in range(begin,end,128)]
    host_driver=play_civ.Player('Stationary host',h,0,random.Random(0),[])
    host_camera=host_driver.cam()
    assert host_camera, 'Host camera unavailable'
    if a.cold_city:
        assert capacities()==[(0,0)]*4, ('City was already initialized',capacities())
        assert host_camera, 'Host camera unavailable'
        print('PASS cold city has four uninitialized preaching slots',flush=True)
    before=followers(h,rival[1]);total=sum(before.values())
    assert total>=5
    for turret in turret_list(h,rival[1]):
        command(h,f'call SporeCoop::CivCity_RemoveTurret thiscall {rival[1]:#x} {turret:#x} 1')
    design=native(h,'Simulator::VehicleDesignId cdecl 1 0')
    country=u32(h,own[1]+0x590)
    keyptr=native(h,f'Simulator::cCivilization::GetDesignKey thiscall {country:#x} {design:#x}')
    key=struct.unpack('<III',mem(h,keyptr,12))
    print('Native religious design',tuple(hex(k) for k in key),flush=True)
    actors={}
    for _ in range(a.preachers):
        actor=native(h,f'Simulator::cCity::SpawnVehicle thiscall {own[1]:#x} 1 0 '+
                     ' '.join(hex(k) for k in key)+' 0')
        def number():
            row=re.search(rf'veh #(\d+) {actor:08X} .*owner=1 purpose=1 ',command(h,'coop.civ'))
            return int(row[1]) if row else None
        ident=wait_until(number,'Native guest preacher not tracked',12)
        actors[ident]=actor
    wait_until(lambda:all(i in play_civ.civ_state(g)[1] for i in actors),'Preacher mirrors missing',12)
    # Place outside the wall, in the direction of an existing valid native vehicle.
    citypos=struct.unpack('<3f',mem(h,rival[1]+0x124,12))
    enemy=next((v for v in play_civ.civ_state(h)[1].values() if v['owner']==254),None)
    approach=enemy['pos'] if enemy else struct.unpack('<3f',mem(h,own[1]+0x124,12))
    direction=play_civ.norm(play_civ.sub(approach,citypos))
    point=play_civ.norm(tuple(x+50*d for x,d in zip(citypos,direction)))
    xyz=tuple(x*math.sqrt(play_civ.dot(citypos,citypos)) for x in point)
    # Military combat is covered separately. Use normal lethal damage to remove
    # the fixture's defenders so this test measures preaching, not siege balance.
    enemies=[i for i,v in play_civ.civ_state(h)[1].items() if v['owner']==254]
    for enemy_id in enemies:
        target=vehicle(h,enemy_id);combat=target+0x508
        damage=u32(h,u32(h,combat)+0x18)
        command(h,f'call {damage:#x} thiscall {combat:#x} f:50000 {own[2]:#x} 0 {actor+0x38:#x} {actor+0x508:#x}')
    wait_until(lambda:all(i not in play_civ.civ_state(h)[1] for i in enemies),'Fixture defenders survived',10)
    remaining=max(0,total//2-2-before.get(own[2],0))
    if remaining:
        command(h,'coop.fix civ.takeover_resistance off')
        try:command(h,f'call Simulator::cCity::ConvertCitizens thiscall {rival[1]:#x} {own[2]:#x} {remaining}')
        finally:command(h,'coop.fix civ.takeover_resistance on')
    seeded=followers(h,rival[1]);assert seeded.get(rival[2],0)>=1 and u32(h,rival[1]+0x30)==rival[2]
    print('FIXTURE wall cleared, native preacher near city, initial followers',seeded,flush=True)
    # Generic selection was qualified separately. Prepare it here and frame
    # the target before exposing this unit to fire or starting its native path.
    play_civ.W,play_civ.H=map(int,re.search(r'screen (\d+) (\d+)',command(g,'coop.uistate')).groups())
    play_civ.HUD_BOTTOM=play_civ.H-95
    driver=play_civ.Player('Religious command',g,1,random.Random(0),[])
    for ident in actors:command(g,f'coop.civ select {ident} 1')
    point=driver.look_at(citypos,box=(330,240,820,380),tries=30);assert point
    for actor in actors.values():
        command(h,f'mem.write {actor+0xB80:#x} {struct.pack("<3f",*xyz).hex()}')
        spatial=actor+0x34;teleport=u32(h,u32(h,spatial)+0x44)
        command(h,f'call {teleport:#x} thiscall {spatial:#x} {actor+0xB80:#x} {actor+0x44:#x}')
    for dy in(-20,0,-38):
        command(g,f'click {round(point[0])} {round(point[1])+dy}')
        command(g,'wait.frames 10 10000')
        if button(g,'037E95BA'):break
    else:raise AssertionError('Native religious city command unavailable')
    def native_orders():
        fleet=play_civ.civ_state(h)[1]
        begin,end=struct.unpack('<II',mem(h,rival[1]+0x618,8))
        assert 0<=end-begin<=32*128 and (end-begin)%128==0
        slots=[u32(h,at+0x70) for at in range(begin,end,128)]
        objects=u32(h,rival[1]+0x62C)
        targets=[u32(h,objects+4*n) for n in range((end-begin)//128)]
        return all(i in fleet and (current_order(h,ptr)==(3,rival[1]) or
                   current_order(h,ptr)[1] in targets or ptr+0x34 in slots)
                   for i,ptr in actors.items())
    wait_until(native_orders,'Guest preaching orders did not reach native city or preaching slots',12)
    print('PASS guest mouse preaching order reaches native city or its preaching slot',flush=True)
    print('Preacher native health',struct.unpack('<f',mem(h,actor+0x540,4))[0],flush=True)
    last_report=0
    def done():
        nonlocal last_report
        complete=ownership(h,rival[0])[0]==own[2] and ownership(g,rival[0])[0]==own[2]
        if not complete:
            camera=host_driver.cam()
            assert camera and all(abs(x-y)<1 for key in ('pos','fwd')
                                  for x,y in zip(camera[key],host_camera[key])), 'Host camera moved during remote preaching'
        fleet=play_civ.civ_state(h)[1]
        alive={i:v for i,v in fleet.items() if i in actors}
        if not complete and not alive:
            raise RuntimeError('Preachers died before conversion; this does not establish an input failure')
        if not complete and time.monotonic()-last_report>=5:
            last_report=time.monotonic()
            print('Native preachers',alive,'followers',followers(h,rival[1]),flush=True)
            begin,end=struct.unpack('<II',mem(h,rival[1]+0x618,8))
            print('Slot counters',[struct.unpack('<8I',mem(h,at,32)) for at in range(begin,end,128)],flush=True)
        return complete
    try:wait_until(done,'Native preaching did not finish the prepared conversion',60)
    except RuntimeError:
        print('Followers',followers(h,rival[1]),flush=True)
        if ident in play_civ.civ_state(h)[1]:print('Order',current_order(h,actor),flush=True)
        print(command(h,'coop.civ'),flush=True)
        raise
    parts=ownership(h,rival[0]);assert ownership(g,rival[0])==parts
    if a.cold_city:
        initialized=[row for row in capacities() if row!=(0,0)]
        assert initialized and all(row==(10,2) for row in initialized), capacities()
        print('PASS remote capacity matches native visible-city baseline (10, 2)',flush=True)
    assert parts[1] and set(parts[1]+parts[2])=={own[2]}
    for c in(h,g):assert 'total faults: 0' in command(c,'exc')
    print('PASS native AI finishes religious takeover; both peers agree on city and part ownership',flush=True)


if __name__=='__main__':main()
