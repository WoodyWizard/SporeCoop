"""BUG-9074: a captured city stays stable while the guest's native planner is open."""
import argparse
import random
import re
import time

import play_civ
from duo_civ_capture_owners import ownership
from duo_civ_founder_control import cities
from duo_civ_species import native
from duo_orders import wait_until
from duo_villages import Console, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    args = parser.parse_args()
    host, guest = Console(args.host), Console(args.guest)
    for peer in (host,guest):
        assert 'duoqa' in command(peer,'coop.savedb').lower()
    index, city, founder, _ = next(row for row in cities(host) if row[3] == 1)
    if not any(row[3] == -1 for row in cities(host)):
        expansion = native(host,'Simulator::cCivExpansion::Get cdecl')
        command(host,f'call Simulator::cCivExpansion::SpawnNextCity thiscall {expansion:#x} 0 0')
        wait_until(lambda:any(row[3] == -1 for row in cities(host)),
                   'No rival city for the planner capture fixture',20)
    rival = next(pol for _,_,pol,owner in cities(host) if owner == -1)
    before = ownership(guest,index)
    driver = play_civ.Player('Captured planner',guest,1,random.Random(0),[])
    play_civ.W, play_civ.H = map(int,re.search(r'screen (\d+) (\d+)',command(guest,'coop.uistate')).groups())
    play_civ.HUD_BOTTOM = play_civ.H-95

    def editing():
        return driver.where(play_civ.PLANNER_NAME).get(play_civ.PLANNER_NAME)

    town = play_civ.civ_state(guest)[0][index]
    point = driver.look_at(town['pos'],box=(330,240,820,390),tries=30)
    assert point
    for dy in (0,-18,-38):
        command(guest,f'click {int(point[0])} {int(point[1])+dy}')
        time.sleep(.5)
        if editing() or driver.click_button(play_civ.BTN_PLANNER):
            break
    wait_until(editing,'Guest planner did not open',10)
    command(host,f'call Simulator::cCity::Capture thiscall {city:#x} {rival:#x} 0 0 0 0')
    wait_until(lambda:ownership(host,index)[0] == rival,'Rival did not capture the city on host',40)
    time.sleep(2)
    held = ownership(guest,index)
    assert editing(), 'Planner closed unexpectedly during the incoming capture'
    print('Guest ownership while planner holds city',before,held,flush=True)
    assert held == before, 'Incoming capture mutated the city held by the planner'
    command(guest,'click 1150 644')
    wait_until(lambda:not editing(),'Planner did not close normally',10)
    wait_until(lambda:ownership(host,index) == ownership(guest,index),
               'Captured city failed to reconcile after planner close',15)
    assert all('total faults: 0' in command(peer,'exc') for peer in (host,guest))
    print('PASS capture deferred until native planner released city',ownership(guest,index),flush=True)


if __name__ == '__main__':
    main()
