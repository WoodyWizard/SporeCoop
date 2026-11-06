"""Finish the native graduation city hall in an isolated 1176x664 campaign.

Run after duo_tribe_graduation_editor.py. All design and confirmation actions
use native UI. The design is a disposable three-part fixture.
"""
import argparse,time
from duo_villages import Console,command
from duo_orders import wait_until
from coop_campaign import drag_slow
from coop_ui import Ui
from duo_tribe_graduation_editor import click,button
from duo_native_ui import dismiss_registration,dismiss_service_error
from duo_civ_graduation_designs import verify as verify_designs, LAND_VEHICLE_EDITORS
from duo_civ_auto_founders import verify as verify_capitals
from coop_ability_bar import u32
from duo_tribe_rebuild_checkpoint import objects, members


def retire_local_guest_member(h, g):
    """Explicit lifetime fixture: native removal while host editing is prolonged.

    The host's authoritative villager remains alive. The local guest world
    loses one member through the same native removal operation used by play.
    The cached mirror must not be treated as a surviving object on return.
    """
    assert 'Editor' in command(h, 'game.mode') and 'Tribe' in command(g, 'game.mode')
    village = next(t for t, _ in objects(g, 0x18C6D19) if u32(g, t+0x30)==0x21CB)
    before = members(g, village)
    assert len(before)>=3, 'Lifetime fixture needs spare guest citizens'
    victim = before[-1]
    remove = u32(g, u32(g, village)+0x9C)
    command(g, f'call {remove:#x} thiscall {village:#x} {victim:#x} 1')
    assert victim not in members(g, village), 'Native removal did not retire the member'
    print(f'Lifetime fixture: native guest member {victim:08X} retired while host edits.', flush=True)
    for second in range(90):
        time.sleep(1)
        for c in (h,g):
            assert c.call('ping')['ok']
            assert 'total faults: 0' in command(c,'exc')
        if second in (29,59,89): print(f'Editor lifetime hold: {second+1}s, both games alive.', flush=True)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',type=int,required=True)
    p.add_argument('--guest',type=int,required=True)
    p.add_argument('--control',action='store_true')
    p.add_argument('--vehicle-control',action='store_true',help='Disable BUG-9060 for the native role-specific pause regression.')
    p.add_argument('--vehicle-role',choices=tuple(LAND_VEHICLE_EDITORS),default='economic')
    p.add_argument('--resume-vehicle',action='store_true',help='Continue a previously logged native City Hall sequence from its first vehicle editor.')
    recovery=p.add_mutually_exclusive_group()
    recovery.add_argument('--retire-guest-member',action='store_true',help='Exercise cached citizen lifetime during a prolonged editor.')
    recovery.add_argument('--guest-offline',action='store_true',help='Leave during vehicle editing, then rejoin after host reaches Civilization.')
    recovery.add_argument('--guest-already-offline',action='store_true',help='Continue the editor test whose guest left before History advancement.')
    p.add_argument('--join',default='127.0.0.1:47810',help='Disposable host address for the offline regression.')
    p.add_argument('--species',nargs=2,default=['067B3E4A','0680BF38'],metavar=('HOST','GUEST'),help='Current outfit species expected in the two capitals.')
    a=p.parse_args();h,g=Console(a.host),Console(a.guest)
    original=command(h,'coop.founders')
    assert command(g,'coop.founders')==original
    vehicle_editor=LAND_VEHICLE_EDITORS[a.vehicle_role]
    first_editor=vehicle_editor if a.resume_vehicle else 0x99E92F05
    assert f'editor {first_editor:08X} key 00000000' in command(h,'coop.editor')
    print(command(h,'coop.fix tribe.graduation_vehicle_types '+('off' if a.vehicle_control else 'on')),flush=True)
    print(command(h,'coop.fix tribe.graduation_pen '+('off' if a.control else 'on')),flush=True)
    if not a.resume_vehicle:
        for sx,sy,tx,ty in((72,110,792,474),(170,110,735,465),(270,110,775,410)):
            drag_slow(h,sx,sy,tx,ty)
        click(h,1155,645);time.sleep(1)
        click(h,590,396);command(h,'text DuoCapital');time.sleep(.5)
        click(h,1155,645);time.sleep(.5);click(h,1155,645)
        wait_until(lambda:'Editor' not in command(h,'game.mode'),'City hall did not save',15)
    last=None
    vehicle_done=False
    guest_offline=a.guest_already_offline
    for _ in range(180):
        for c in ((h,) if guest_offline else (h,g)):
            dismiss_registration(c);dismiss_service_error(c)
            button(c,'0445C99D');button(c,'062EF353')
        if not guest_offline and command(h,'coop.vote').startswith('vote active'):
            command(g,'coop.uistate 1');time.sleep(.3);assert Ui(g,'guest').click('ready')
            wait_until(lambda:not command(h,'coop.vote').startswith('vote active'),'Vehicle ready vote did not finish',10)
            wait_until(lambda:'Editor' in command(h,'game.mode'),'Native first vehicle remains held after ready',12)
        mode=command(h,'game.mode').strip()
        if mode!=last:print('Native host mode:',mode,flush=True);last=mode
        if guest_offline and 'Civ' in mode and 'busy: -' in command(h,'coop.status'):
            assert command(h,'coop.founders')==original, 'Host advancement lost founder identities'
            command(g,'coop.join '+a.join+' DuoTribalGuest')
            guest_offline=False
            print('Guest reconnecting after offline host advancement.',flush=True)
        if 'Civ' in mode and 'Civ' in command(g,'game.mode') and all('busy: -' in command(c,'coop.status') for c in(h,g)):break
        if 'Editor' in mode:
            detail=command(h,'coop.editor');print(detail,flush=True)
            assert not vehicle_done and f'editor {vehicle_editor:08X} key 00000000' in detail
            if a.retire_guest_member: retire_local_guest_member(h,g)
            if a.guest_offline:
                command(g,'coop.leave')
                wait_until(lambda:'players=1/4' in command(h,'coop.status'),'Host did not acknowledge guest departure',10)
                guest_offline=True
                print('Guest left during native vehicle editing.',flush=True)
            # Native hull, wheels and role part; no design resource injection.
            drag_slow(h,70,110,775,440)
            click(h,200,44);drag_slow(h,70,267,755,438)
            click(h,270,44);drag_slow(h,70,110,775,390)
            click(h,1155,645);time.sleep(1)
            click(h,590,396);command(h,'text DuoRover');time.sleep(.5)
            click(h,1155,645);time.sleep(.5);click(h,1155,645)
            wait_until(lambda:'Editor' not in command(h,'game.mode'),'Vehicle did not save',20)
            vehicle_done=True
        time.sleep(1)
    else:raise RuntimeError('Graduation did not become playable on both games: '+command(h,'game.mode')+' / '+command(g,'game.mode'))
    verify_capitals(h,g,a.species)
    for c in(h,g):
        assert command(c,'coop.founders')==original, 'Graduation changed immutable founder identities'
        assert 'total faults: 0' in command(c,'exc')
        nations=command(c,'coop.civ nations')
        assert f'nation_species={int(a.species[0],16):08X}' in nations
        print(command(c,'game.mode'),nations,command(c,'exc'),flush=True)
    verify_designs(h,g,vehicle_editor)
    print('PASS both clients reach native Civilization through matching designs, original founders retained, zero faults.',flush=True)


if __name__=='__main__':main()

