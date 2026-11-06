"""Verify host permissions before, during and after an absent founder's command loan."""
import argparse
import re

from coop_ability_bar import u32
from coop_ui import Ui
from duo_civ_founder_control import cities, own
from duo_civ_native_controls import check_player
from duo_native_ui import dismiss_registration, dismiss_service_error
from duo_orders import wait_until
from duo_tribe_graduation_editor import button
from duo_villages import Console, command


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', required=True, type=int)
    p.add_argument('--guest', required=True, type=int)
    p.add_argument('--endpoint', required=True)
    p.add_argument('--saved-on', action='store_true')
    args = p.parse_args()
    h, g = Console(args.host), Console(args.guest)
    for c in (h, g):
        assert 'duoqa' in command(c, 'coop.savedb').lower()
        assert "world 'Vantiegan.spo'" in command(c, 'coop.avatar')
    initial = command(h, 'coop.founders')
    assert 'host 291489FF, guest 7CF26CCB' in initial
    def setting(): return command(h, 'coop.civ offline_control').strip()
    assert setting() == ('Offline partner control: on' if args.saved_on else 'Offline partner control: off')
    if args.saved_on:
        print('PASS campaign permission survives cold reload.', flush=True)
        return
    rows = cities(h)
    slot, _, partner, _ = next(row for row in rows if row[3] == 1)
    def vehicle(c):
        for identity, ptr, faction, owner in re.findall(r'veh #(\d+) ([0-9A-F]+) pol=([0-9A-F]+) owner=(\d+)', command(c, 'coop.civ')):
            if int(faction,16) == partner: return int(identity), int(ptr,16), int(owner)
        return None
    if not vehicle(h): command(g, f'coop.civ buy {slot} 0')
    wait_until(lambda: vehicle(h) and vehicle(g), 'Guest vehicle was not created on both peers', 30)
    def native_owned(c, spatial):
        func = u32(c,u32(c,spatial)+0x58)
        return bool(own(c,spatial,hex(func)))
    def permissions(c, expected):
        city = next((ptr for _,ptr,pol,_ in cities(c) if pol==partner), None)
        v = vehicle(c)
        return city and v and native_owned(c,city+0x120)==expected and native_owned(c,v[1]+0x34)==expected
    assert permissions(h,False) and permissions(g,True)
    command(g,'coop.leave')
    wait_until(lambda: 'players=1/4' in command(h,'coop.status') and permissions(h,False) and vehicle(h)[2]==254,
               'Absent partner lost its reservation', 20)
    print('PASS absent capital and vehicle reserved by default.',flush=True)
    command(h,'coop.uistate 1'); ui=Ui(h,'host')
    assert ui.wait(lambda s: s.get('space',{}).get('vis'), 8, 'visible Nations button')
    assert ui.click('space') and ui.wait(lambda s: s.get('page')=='space',5,'Nations settings')
    key=next(k for k,v in ui.state().items() if k.startswith('sbtn') and isinstance(v,dict) and v.get('text')=='Disabled')
    assert ui.click(key)
    wait_until(lambda:setting()=='Offline partner control: on','F9 toggle did not save',10)
    command(h,'coop.uistate 0')
    wait_until(lambda:permissions(h,True) and vehicle(h)[2]==0,'Host did not acquire temporary control',10)
    check_player(h,h,0,vehicle(h)[0])
    print('PASS ordinary host box selection and movement of the absent partner vehicle.',flush=True)
    command(g,'coop.join '+args.endpoint+' DuoTribalGuest')
    def rejoined():
        if 'Civ' not in command(g,'game.mode'): return False
        dismiss_registration(g); dismiss_service_error(g);button(g,'0445C99D');button(g,'062EF353')
        return permissions(g,True) and permissions(h,False)
    wait_until(rejoined,'Guest command was not restored on reconnect',100)
    for c in (h,g):
        assert command(c,'coop.founders')==initial
        assert 'total faults: 0' in command(c,'exc')
    print('PASS same species and factions, command returned to the guest.',flush=True)


if __name__ == '__main__': main()
