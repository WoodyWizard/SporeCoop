"""Join an orbital Vantiegan host from an already loaded Tribal founder save.

Use fresh isolated profiles: host EarlyLaunchB/Game0, guest FourWarriorsB/Game4.
Both must be disconnected and loaded before invoking this targeted regression.
No stage victories or tutorial completion are injected.
"""
import argparse

from duo_civ_late_launch import main as launch_check
from duo_orders import wait_until
from duo_villages import Console, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--port', type=int, default=47810)
    args = parser.parse_args()
    host, guest = Console(args.host), Console(args.guest)
    for peer in (host, guest):
        assert 'duoqa' in command(peer, 'coop.savedb').lower()
        assert "world 'Vantiegan.spo'" in command(peer, 'coop.avatar')
        assert 'host 291489FF, guest 7CF26CCB' in command(peer, 'coop.founders')
    assert 'Space' in command(host, 'game.mode')
    assert 'Tribe' in command(guest, 'game.mode')
    original = command(guest, 'coop.founders')
    print('PASS returning founder starts two stages behind with its original species', flush=True)
    command(host, f'coop.host {args.port} LineagesHost')
    command(guest, f'coop.join 127.0.0.1:{args.port} LineagesFounder')
    wait_until(lambda: 'players=2/4' in command(host, 'coop.status'), 'Guest failed to join', 30)
    launch_check(['--host', str(args.host), '--guest', str(args.guest),
                  '--recover-victory', '--rejoin-at-tutorial', str(args.port)])
    assert command(guest, 'coop.founders') == original
    assert 'Space' in command(host, 'game.mode')
    print('PASS Tribal-to-Space catch-up preserves founders, personal setup and reconnect', flush=True)


if __name__ == '__main__':
    main()
