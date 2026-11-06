"""Queue a host capacity increase on a disposable Space save, then check a cold reload."""
import argparse
import time

from duo_orders import wait_until
from duo_villages import Console, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--verify', action='store_true')
    parser.add_argument('--graduate', action='store_true',
                        help='Start in the saved Civ victory and qualify the normal graduation boundary.')
    args = parser.parse_args()
    peer = Console(args.host)
    for _ in range(60):
        try:
            if peer.call('ping')['ok']:
                break
        except OSError:
            pass
        time.sleep(1)
    wait_until(lambda: 'GalaxyGameEntry' in command(peer, 'game.mode'), 'No main menu', 90)
    assert 'duoqa' in command(peer, 'coop.savedb').lower()
    command(peer, 'wait.frames 600 120000')
    command(peer, 'coop.load Game0 Vantiegan')
    stage = 'Civ' if args.graduate else 'Space'
    wait_until(lambda: stage in command(peer, 'game.mode') and 'Vantiegan' in command(peer, 'coop.avatar'),
               'Space planet failed to load', 90)
    command(peer, f'coop.host {args.port} SeatHost')
    def status():
        return command(peer, 'coop.status')
    expected = 4 if args.verify else 2
    wait_until(lambda: f'players=1/{expected}' in status() and
               f'Reserved founders: {expected},' in status(), 'Unexpected active capacity', 20)
    if args.graduate:
        from duo_civ_launch_editor import acknowledge, create_first_ship
        from duo_civ_species import native
        from duo_native_ui import dismiss_registration, dismiss_service_error
        from duo_tribe_graduation_editor import button, click
        assert native(peer,'SporeCoop::CivMode_SpaceReady cdecl') & 255
        from coop_ui import Ui
        command(peer,'coop.uistate 1')
        ui = Ui(peer,'host')
        wait_until(lambda:ui.state().get('queueseats',{}).get('vis'), 'Missing host seat control',10)
        assert ui.type_into('nextseats','4') and ui.click('queueseats')
        assert 'queued 4 for next stage' in status()
        command(peer,'coop.uistate 0')
        dismiss_registration(peer)
        dismiss_service_error(peer)
        acknowledge(peer)
        click(peer,1136,648)
        time.sleep(2)
        button(peer,'FFFFFFF1')
        click(peer,1152,641)
        def editor():
            acknowledge(peer)
            return 'Editor' in command(peer,'game.mode')
        wait_until(editor,'Spacecraft editor did not open',45)
        assert 'players=1/2' in status(), 'Seats activated inside the ship editor'
        create_first_ship(peer,'ReserveVoyager',70)
        wait_until(lambda:'players=1/4' in status() and 'queued 4' not in status(),
                   'Civilization graduation did not activate four seats',25)
        print('PASS two seats in Civilization and the editor, four on Space entry.',flush=True)
    elif not args.verify:
        print(command(peer, 'coop.seats 4'), flush=True)
        assert 'players=1/2' in status() and 'queued 4 on next Space load' in status()
        command(peer, 'coop.leave')
        command(peer, f'coop.host {args.port} SeatHost')
        wait_until(lambda: 'queued 4 on next Space load' in status(), 'Rehosting lost the queue', 10)
        assert 'players=1/2' in status()
    else:
        assert 'queued 4 on next Space load' not in status()
    assert 'total faults: 0' in command(peer, 'exc')
    if not args.graduate:
        print('PASS', 'cold Space load applied four seats' if args.verify else
              'Space stayed at two seats through queue and rehost', flush=True)


if __name__ == '__main__':
    main()
