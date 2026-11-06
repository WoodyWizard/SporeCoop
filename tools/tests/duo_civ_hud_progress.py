"""Read actual Civilization HUD calls before and after enabling founder team credit."""
import argparse
import re

from duo_civ_founder_control import cities
from duo_orders import wait_until
from duo_villages import Console, command


def snapshot(peer):
    text = command(peer, 'coop.civ hud_progress')
    match = re.fullmatch(r'samples=(\d+) home=([\d.]+) displayed=([\d.]+) final_cities=(\d+)', text.strip())
    assert match, text
    count, home, displayed, total = match.groups()
    return int(count), float(home), float(displayed), int(total)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    args = parser.parse_args()
    peers = [Console(args.host), Console(args.guest)]
    for peer in peers:
        assert 'duoqa' in command(peer, 'coop.savedb').lower()
    try:
        for enabled in (False, True):
            for peer in peers:
                command(peer, 'coop.fix civ.team_progress ' + ('on' if enabled else 'off'))
            for peer in peers:
                rows = cities(peer)
                home = sum(row[3] == 0 for row in rows)
                team = sum(row[3] in (0, 1) for row in rows)
                assert 0 < home < team, rows
                previous = snapshot(peer)[0]
                def updated():
                    sample = snapshot(peer)
                    return sample if sample[0] > previous else None
                _, original, shown, total = wait_until(updated, 'No fresh native HUD sample', 20)
                print('Team setting', enabled, 'home', original, 'HUD', shown, 'target', total, flush=True)
                assert abs(original - home/total) < .00001
                assert abs(shown - (team if enabled else home)/total) < .00001
                assert 'total faults: 0' in command(peer, 'exc')
        print('PASS both games displayed the earned team fraction.')
    finally:
        for peer in peers:
            command(peer, 'coop.fix civ.team_progress on')


if __name__ == '__main__':
    main()
