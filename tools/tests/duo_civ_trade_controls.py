"""Exercise native guest trader purchase, selection and a city trade command."""
import argparse
from pathlib import Path
import random
import re
import time

import play_civ
from coop_ability_bar import u32
from duo_civ_founder_control import cities
from duo_civ_mouse_combat import current_order, vehicle
from duo_civ_native_controls import frame
from duo_orders import wait_until
from duo_tribe_graduation_editor import button
from duo_villages import Console, command


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',type=int,required=True)
    p.add_argument('--guest',type=int,required=True)
    a=p.parse_args(); h,g=Console(a.host),Console(a.guest)
    for peer in (h,g):
        assert 'duoqa' in command(peer,'coop.savedb').lower()
        assert "world 'Clyth.spo'" in command(peer,'coop.avatar')
    home=next(row for row in cities(h) if row[3]==1)
    print('Guest city native specialty',u32(h,home[1]+0x540),flush=True)
    assert u32(h,home[1]+0x540)==2,'Fixture needs a native economic city'
    play_civ.W,play_civ.H=map(int,re.search(r'screen (\d+) (\d+)',command(g,'coop.uistate')).groups())
    play_civ.HUD_BOTTOM=play_civ.H-95
    driver=play_civ.Player('Guest trader',g,1,random.Random(0),[])
    towns,before,_=play_civ.civ_state(g)
    point=driver.look_at(towns[home[0]]['pos'],box=(330,240,820,390),tries=30)
    assert point,'Guest city could not be framed'
    for dy in (0,-18,-38):
        command(g,f'click {int(point[0])} {int(point[1])+dy}')
        # Injected clicks release after six rendered frames. At the 10 FPS lab
        # limit, half a second reads the menu during the unfinished click.
        command(g,'wait.frames 8 10000')
        if driver.where('062D6902').get('062D6902'): break
    print(command(g,'coop.ui where 062D6902 062D5618 272EB68E'),flush=True)
    bought=button(g,'062D6902')
    if not bought:
        print(command(g,'coop.ui tree 6'),flush=True)
        output=Path('build/local/lineages-trade-purchase-failure.png').resolve().as_posix()
        print(g.call(f'shot "{output}"'),flush=True)
    assert bought,'Native land vehicle purchase unavailable'
    def purchased():
        fleet=play_civ.civ_state(g)[1]
        return next((i for i,v in fleet.items() if i not in before and v['owner']==1 and v['purpose']==2),None)
    trader=wait_until(purchased,'Guest native trader purchase did not reach both games',15)
    actor=vehicle(h,trader)
    assert u32(h,actor+0xB20)==2 and u32(h,actor+0x30)==home[2]
    print('PASS native guest economic vehicle purchase',trader,flush=True)
    command(g,'wait.frames 20 10000')
    driver,point=frame(g,1,trader)
    # A newly spawned trader can sit beside another vehicle or the city hall;
    # starting a broad drag there can grab that object instead of a selection box.
    command(g,f'move {point[0]} {point[1]}')
    command(g,'wait.frames 8 10000')
    command(g,f'click {point[0]} {point[1]}')
    command(g,'wait.frames 12 10000')
    wait_until(lambda:play_civ.civ_state(g)[1][trader]['sel'],'Guest could not select trader',8)
    rival=next(row for row in cities(h) if row[3]==-1)
    target=play_civ.civ_state(g)[0][rival[0]]
    point=driver.look_at(target['pos'],box=(330,240,820,380),tries=30)
    assert point,'Rival city could not be framed'
    driver.right_click(*point)
    time.sleep(1)
    print('After right click, host native order',current_order(h,actor),flush=True)
    print(command(g,'coop.ui where 037E95BC 037E95BD'),flush=True)
    order=current_order(h,actor)
    if order[0] not in (5,9):
        for wid in ('037E95BC','037E95BD'):
            if button(g,wid): break
    wait_until(lambda:current_order(h,actor)[0] in (5,9),
               'Guest trade command did not reach native host vehicle',10)
    assert current_order(h,actor)[1]==rival[1]
    assert all('total faults: 0' in command(peer,'exc') for peer in (h,g))
    print('PASS native guest selection and trade order target on host',flush=True)


if __name__=='__main__':
    main()
