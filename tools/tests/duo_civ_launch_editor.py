"""Open the first spacecraft editor from the ten-city Vantiegan qualification save."""
import argparse
import re
import time

import uifind
from coop_campaign import CIV_TALK, drag_slow
from coop_ability_bar import mem
from duo_civ_species import native
from duo_orders import wait_until
from coop_ui import Ui
from duo_civ_founder_control import cities
from duo_native_ui import dismiss_registration, dismiss_service_error
from duo_tribe_graduation_editor import button, click
from duo_villages import Console, command


def acknowledge(peer):
    for text in CIV_TALK:
        if uifind.shown(peer, text):
            uifind.press(peer, text)
    for widget in ('0445C99D', '062EF353', 'FFFFFFF1'):
        button(peer, widget)


def create_first_ship(peer, name, hull_x):
    for sx, sy, tx, ty in ((hull_x,110,756,452), (170,307,749,406), (265,307,758,468)):
        drag_slow(peer,sx,sy,tx,ty)
    click(peer,1155,645)
    time.sleep(1)
    click(peer,590,396)
    command(peer,'text ' + name)
    click(peer,1155,645)
    time.sleep(.5)
    click(peer,1155,645)
    wait_until(lambda:'Editor' not in command(peer,'game.mode'), name + ' did not save', 20)
    wait_until(lambda:'Space' in command(peer,'game.mode'), name + ' did not enter Space', 60)
    empire = native(peer,'Simulator::GetPlayerEmpire cdecl')
    key = mem(peer,empire + 0xB8,12)
    assert len(key) == 12 and any(key[:4]), 'No native spacecraft model'
    return key


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--control', action='store_true')
    parser.add_argument('--both-ships', action='store_true',
                        help='After the host opens its editor, launch the guest through its own Civilization History.')
    args = parser.parse_args()
    h, g = Console(args.host), Console(args.guest)
    for peer in (h,g):
        assert 'duoqa' in command(peer,'coop.savedb').lower()
        assert "world 'Vantiegan.spo'" in command(peer,'coop.avatar')
        assert 'host 291489FF, guest 7CF26CCB' in command(peer,'coop.founders')
        rows = cities(peer)
        assert [sum(r[3] == owner for r in rows) for owner in (0,1)] == [5,5]
        readiness = command(peer, 'call SporeCoop::CivMode_SpaceReady cdecl')
        assert int(re.search(r'eax=0x(\w+)', readiness)[1],16) & 255 == 1
        dismiss_registration(peer)
        dismiss_service_error(peer)
        acknowledge(peer)
    print(command(h, 'coop.fix civ.launch_editor ' + ('off' if args.control else 'on')), flush=True)
    assert 'on BUG-0404' in command(h, 'coop.fix core.editor_cinematic on')
    click(h,1136,648)
    time.sleep(2)
    button(h,'FFFFFFF1')
    click(h,1152,641)
    deadline = time.monotonic() + 55
    voted = False
    while time.monotonic() < deadline:
        details = command(h,'coop.vote')
        if args.control and 'held native editor=96B24187' in details and 'input_lock,paused' in details:
            assert voted
            print('Control reproduced the held native spacecraft request.', details, flush=True)
            break
        if 'Editor' in command(h,'game.mode'):
            assert voted and not args.control
            request = command(h,'coop.editor')
            assert 'editor 96B24187 key 00000000' in request, request
            print('PASS native Space launch vote opened its spacecraft editor.', request, flush=True)
            break
        if details.startswith('vote active') and not voted:
            command(g,'coop.uistate 1')
            time.sleep(.3)
            voted = Ui(g,'guest').click('ready')
        for peer in (h,g):
            acknowledge(peer)
        time.sleep(.3)
    else:
        raise RuntimeError(command(h,'coop.vote'))
    for peer in (h,g):
        assert 'total faults: 0' in command(peer,'exc')
    if args.both_ships:
        assert not args.control and 'Editor' in command(h, 'game.mode')
        assert 'Civ' in command(g, 'game.mode')
        command(g, 'coop.uistate 0')
        acknowledge(g)
        click(g,1136,648)
        time.sleep(2)
        button(g,'FFFFFFF1')
        click(g,1152,641)
        deadline = time.monotonic() + 40
        while time.monotonic() < deadline and 'Editor' not in command(g, 'game.mode'):
            acknowledge(g)
            time.sleep(.3)
        for peer in (h, g):
            assert 'Editor' in command(peer, 'game.mode'), 'Both local spacecraft editors must be open'
            request = command(peer, 'coop.editor')
            assert 'editor 96B24187 key 00000000' in request, request
            assert 'total faults: 0' in command(peer, 'exc')
        print('PASS both players independently opened a blank native spacecraft editor.', flush=True)
        guest_ship = create_first_ship(g, 'DuoGuestVoyager', 170)
        host_ship = create_first_ship(h, 'DuoHostVoyager', 70)
        print('Native empire ship keys:',guest_ship.hex(),host_ship.hex(),flush=True)
        def naming(peer):
            for widget in ('0445C99D','062EF353'):
                button(peer,widget)
            return 'enabled=1' in command(peer,'coop.ui where 01510D07')
        for peer, key in ((g,guest_ship), (h,host_ship)):
            wait_until(lambda:naming(peer),'Personal captain name prompt missing',55)
            assert native(peer,'Simulator::GetPlayerEmpireID cdecl') == 0x800021CA
            assert mem(peer,native(peer,'Simulator::GetPlayerEmpire cdecl')+0xB8,12) == key
            assert 'total faults: 0' in command(peer,'exc')
        print('PASS both native captain name prompts.',flush=True)
        ship_parts = [command(peer, 'coop.creations ship_parts') for peer in (g,h)]
        print('Local models:',ship_parts,flush=True)
        bodies = []
        for peer, name, model in ((g,'GuestCaptain','29148A01'), (h,'HostCaptain','291489FF')):
            click(peer,576,347)
            command(peer,'text ' + name)
            click(peer,805,423)
            wait_until(lambda:'none' in command(peer,'coop.ui where 01510D07'),
                       name + ' was not accepted', 15)
            def comm_ready():
                acknowledge(peer)
                return 'enabled=1' in command(peer,'coop.ui where 05244C68')
            wait_until(comm_ready, name + ' did not reach Mission Control', 55)
            click(peer,289,502)
            wait_until(lambda:uifind.shown(peer,'Been there, done that'),
                       name + ' did not receive the native tutorial choice', 20)
            empire = native(peer,'Simulator::GetPlayerEmpire cdecl')
            captain = mem(peer,empire+0xC4,12)
            print(name,'native captain resource',captain.hex(),flush=True)
            captain_id = int.from_bytes(captain[:4], 'little')
            source = command(peer, 'coop.creations body_parts '+model)
            created = command(peer, f'coop.creations body_parts {captain_id:X}')
            bodies.append((source,created))
        print('PASS both players reached their own native flight tutorial choice.',flush=True)
        assert all(' parts:' in row for row in ship_parts), 'Unreadable active ship model'
        assert ship_parts[0].split(' parts:')[1] != ship_parts[1].split(' parts:')[1], 'The native active ships used the same parts despite different hull selections'
        for source, created in bodies:
            assert ' parts:' in source and ' parts:' in created and source.split(' parts:')[1] == created.split(' parts:')[1], (source,created)
        assert bodies[0][0].split(' parts:')[1] != bodies[1][0].split(' parts:')[1]
        print('PASS both native captains match their respective founder bodies.',flush=True)


if __name__ == '__main__':
    main()
