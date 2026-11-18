"""Cold-restart an interrupted guest flight lesson from its existing, disposable profile."""
import argparse
import time

import uifind
from coop_ability_bar import u32
from duo_civ_launch_editor import acknowledge
from duo_civ_species import native
from duo_native_ui import dismiss_registration, dismiss_service_error
from duo_orders import wait_until
from duo_space_tutorial_choices import context
from duo_tribe_graduation_editor import button, click
from duo_villages import Console, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for arg in ('host', 'guest', 'port'):
        parser.add_argument('--' + arg, type=int, required=True)
    for arg in ('ship', 'captain'):
        parser.add_argument('--' + arg, type=lambda value: int(value, 16), required=True)
    args = parser.parse_args()
    h, g = Console(args.host), Console(args.guest)
    for peer in (h, g):
        assert 'duoqa' in command(peer, 'coop.savedb').lower()
    assert 'GalaxyGameEntry' in command(g, 'game.mode') and 'Space' in command(h, 'game.mode')
    body = command(g, f'coop.creations body_hash {args.captain:X}').strip()
    hull = command(g, f'coop.creations ship_parts {args.ship:X}').strip()
    assert len(body) == 64 and ' parts:' in hull, (body, hull)
    print('Original personal creations:', body, hull, flush=True)
    command(g, f'coop.join 127.0.0.1:{args.port} LateFounder')
    wait_until(lambda: 'players=2/4' in command(h, 'coop.status'), 'Guest did not join', 30)
    def history_ready():
        return ('Civ' in command(g, 'game.mode') and
                'shared orbital save waiting for your first flight' in command(g, 'coop.status'))
    wait_until(history_ready, 'No personal launch checkpoint was recovered', 90)
    dismiss_registration(g)
    dismiss_service_error(g)
    command(g, 'coop.uistate 0')
    acknowledge(g)
    click(g,1136,648)
    time.sleep(2)
    button(g,'FFFFFFF1')
    click(g,1152,641)
    def restored_ship():
        if 'Editor' in command(g,'game.mode'):
            return f"editor 96B24187 key {args.ship:08X}" in command(g,'coop.editor')
        acknowledge(g)
        return False
    wait_until(restored_ship, 'The saved spacecraft was not loaded into the native launch confirmation', 60)
    click(g,1155,645)
    stages = []
    def mission_control():
        mode = command(g, 'game.mode')
        if not stages or stages[-1] != mode:
            stages.append(mode)
            print('Guest mode:', mode, flush=True)
        assert 'Space' in command(h, 'game.mode')
        if 'Civ' in mode or 'Space' in mode:
            dismiss_registration(g)
            dismiss_service_error(g)
            for widget in ('0445C99D', '062EF353', 'FFFFFFF1'):
                button(g, widget)
        if 'Space' not in mode: return False
        assert 'enabled=1' not in command(g, 'coop.ui where 01510D07'), 'The returning guest was asked to name a replacement captain'
        return 'enabled=1' in command(g, 'coop.ui where 05244C68')
    wait_until(mission_control, 'Guest did not reach Mission Control with its saved spacecraft', 150)
    empire = native(g, 'Simulator::GetPlayerEmpire cdecl')
    assert u32(g, empire + 0xB8) == args.ship and u32(g, empire + 0xC4) == args.captain
    assert command(g, 'coop.creations ship_parts').strip() == hull
    assert command(g, f'coop.creations body_hash {args.captain:X}').strip() == body
    def open_comms():
        if uifind.shown(g, 'Been there, done that'): return True
        click(g,289,502)
        return False
    wait_until(open_comms, 'Returned ship did not receive the flight lesson', 30)
    assert uifind.press(g,"I'm in.",expect="I'll be back soon",timeout=25)
    assert uifind.press(g,"I'll be back soon",timeout=25)
    # The native mission counter also appears in a tooltip whose parent is
    # hidden until hover. It changes to six after the first real orb.
    wait_until(lambda:"Visit 7 more cities and fly thru" in command(g,'coop.ui find "more cities"'),
               'The unfinished lesson did not restart at seven cities',30)
    assert context(g) == 0 and context(h) in (1, 2)
    for peer in (h, g):
        assert 'total faults: 0' in command(peer, 'exc')
    print('PASS cold restart kept the original ship and captain and restarted the native lesson.', flush=True)


if __name__ == '__main__':
    main()
