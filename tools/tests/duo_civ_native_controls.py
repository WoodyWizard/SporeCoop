"""BUG-9054: native drag selection and mouse orders in separate founder nations.

Run on joined disposable Clyth profiles after duo_civ_founder_control.py.
The only forced selection is deselection before testing real input.
"""
import argparse,random,re,time
from duo_villages import Console,command
from duo_orders import wait_until
from duo_native_ui import dismiss_registration,dismiss_service_error
from duo_tribe_graduation_editor import click,button
import play_civ


def frame(c,owner,vehicle):
    play_civ.W,play_civ.H=map(int,re.search(r'screen (\d+) (\d+)',command(c,'coop.uistate')).groups())
    play_civ.HUD_BOTTOM=play_civ.H-95
    driver=play_civ.Player('Native controls',c,owner,random.Random(0),[])
    pos=play_civ.civ_state(c)[1][vehicle]['pos']
    screen=driver.look_at(pos,box=(330,240,820,380),tries=30)
    assert screen,'Could not frame the native vehicle'
    driver.wait_still()
    row=re.search(rf'^vehicle {vehicle} \d+ (\d+) (\d+)',command(c,'coop.targets'),re.M)
    assert row,'Vehicle absent from native projected targets'
    return driver,tuple(map(int,row.groups()))


def check_player(h,c,owner,vehicle=None):
    state=play_civ.civ_state(c)[1]
    if vehicle is None:
        vehicle=next(i for i,v in state.items() if v['owner']==owner)
    assert state[vehicle]['owner'] == owner
    driver,screen=frame(c,owner,vehicle)
    ptr=re.search(rf'  veh #{vehicle} ([0-9A-F]+)',command(c,'coop.civ'))[1]
    command(c,f'call Simulator::cVehicle::SetIsSelected thiscall 0x{int(ptr,16)+0x34:X} 0')
    assert not play_civ.civ_state(c)[1][vehicle]['sel']
    driver.box_select([screen])
    command(c,'wait.frames 40 10000')
    assert play_civ.civ_state(c)[1][vehicle]['sel'],f'Player {owner}: native drag missed own vehicle'
    before=play_civ.civ_state(h)[1][vehicle]['pos']
    # Nearby ground, away from the hall. Native pathfinding may reject a spot;
    # require actual host motion after at least one of these ordinary clicks.
    moved=False
    for dx,dy in ((0,85),(-90,55),(90,55)):
        driver.right_click(screen[0]+dx,screen[1]+dy)
        command(c,'wait.frames 45 10000')
        deadline=time.monotonic()+3
        while time.monotonic()<deadline:
            actual=play_civ.civ_state(h)[1][vehicle]
            if play_civ.dist(before,actual['pos'])>3:
                moved=True;break
            time.sleep(.2)
        if moved:break
    assert moved,f'Player {owner}: mouse order did not move the native host vehicle'
    wait_until(lambda:play_civ.dist(play_civ.civ_state(h)[1][vehicle]['pos'],
                                   play_civ.civ_state(c)[1][vehicle]['pos'])<8,
               'Guest vehicle failed to follow authoritative motion',10)
    print(f'PASS player {owner}: native drag selection and ground-click movement.',flush=True)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',type=int,required=True);p.add_argument('--guest',type=int,required=True)
    p.add_argument('--guest-only',action='store_true')
    a=p.parse_args();h,g=Console(a.host),Console(a.guest)
    for c in(h,g):
        assert 'duoqa' in command(c,'coop.savedb').lower(),'Requires disposable profiles'
        assert "world 'Clyth.spo'" in command(c,'coop.avatar')
        dismiss_registration(c);dismiss_service_error(c)
    check_player(h,g,1)
    if not a.guest_only:
        # Purchase the host vehicle through the actual native city menu.
        driver=play_civ.Player('Host purchase',h,0,random.Random(0),[])
        cities=play_civ.civ_state(h)[0]
        city=next(c for c in cities.values() if c['owner']==0)
        assert driver.open_city(city['pos']),'Could not open the home city menu'
        assert button(h,'062D6902'),'Native land-vehicle button unavailable'
        wait_until(lambda:any(v['owner']==0 for v in play_civ.civ_state(g)[1].values()),
                   'Native host UI purchase was not mirrored',12)
        check_player(h,h,0)
        # Ordinary clicks on the partner's vehicle must not take control.
        for c,owner in((h,0),(g,1)):
            other=next(i for i,v in play_civ.civ_state(c)[1].items() if v['owner']!=owner)
            _,screen=frame(c,owner,other)
            click(c,*screen);command(c,'wait.frames 20 10000')
            assert not play_civ.civ_state(c)[1][other]['sel'],'Selected partner vehicle'
        print('PASS native host purchase and cross-player selection rejection.',flush=True)
    for c in(h,g):assert 'total faults: 0' in command(c,'exc')


if __name__=='__main__':main()
