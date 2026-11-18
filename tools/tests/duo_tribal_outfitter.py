"""Native planner vote: two distinct species, identical earned clothing palettes.

Requires fresh disposable Vantiegan Tribal profiles at 1176x664, connected and
past the entry tutorial. --control must fail because the guest editor never opens.
This checks editor entry only, not outfit adoption or persistence.
"""
import argparse
import math
import re
import time

from duo_orders import inspect, wait_until
from duo_villages import Console, command
from coop_campaign import drag_slow


def restored_outfits(h, g, expect_changed=True):
    """Assert native models, both original identities, ownership, and actual walking."""
    original = command(h, 'coop.founders')
    roots = re.search(r'host ([0-9A-F]+), guest ([0-9A-F]+)', original).groups()
    last_wait = ''

    def waiting(reason):
        nonlocal last_wait
        if reason != last_wait:
            print('Waiting:', reason, flush=True)
            last_wait = reason
        return False

    def ready():
        keys = None
        for peer, c in enumerate((h, g)):
            mode = command(c, 'game.mode')
            busy = re.search(r'^busy:.*', command(c, 'coop.status'), re.M)[0]
            if 'Tribe' not in mode or busy != 'busy: -': return waiting(f'{peer} {mode} {busy}')
            row = command(c, 'coop.tribe outfits')
            match = re.search(r'readable=1 home=([0-9A-F]+) guest=([0-9A-F]+).*pending=0', row)
            if not match: return waiting(f'{peer} {row.strip()}')
            current = match.groups()
            wrong_models = any(a == b for a,b in zip(current,roots)) if expect_changed else current != roots
            if wrong_models or (keys and current != keys): return waiting(f'{peer} models {current}')
            groups = inspect(c)[0]
            if any(len(groups.get(key, {})) != 3 or any(member[0] != seat for member in groups[key].values())
                   for seat, key in enumerate(current)): return waiting(f'{peer} citizens {groups}')
            keys = current
            if command(c, 'coop.founders') != original: raise RuntimeError('Original founding identities changed')
        return keys

    home, guest = wait_until(ready, 'Saved outfits did not restore native models and ownership on both peers', 90)
    print('Native saved outfit models:', home, guest, flush=True)
    groups, done = inspect(h)
    start = {i: row[1] for i, row in groups[guest].items()}
    center = [sum(pos[j] for pos in start.values()) / len(start) for j in range(3)]
    tangent = [-center[2], 0, center[0]]
    length = math.hypot(*tangent)
    if length < 1: raise RuntimeError('Fixture tangent unavailable')
    target = [center[i] + 35*tangent[i]/length for i in range(3)]
    scale = math.hypot(*center)/math.hypot(*target)
    target = [x*scale for x in target]
    command(g, 'coop.tribe select_village')
    command(g, 'coop.tribe walk ' + ' '.join(f'{x:.2f}' for x in target))
    wait_until(lambda: inspect(h)[1] >= done+3, 'Outfitted guest lost order authority')
    for c in (h, g):
        def moved():
            current = inspect(c)[0].get(guest, {})
            return all(i in current and math.dist(pos, target)-math.dist(current[i][1], target)>3 for i,pos in start.items())
        wait_until(moved, 'Outfitted villagers did not walk on both peers', 30)
        if 'total faults: 0' not in command(c, 'exc'): raise RuntimeError('Native outfit fault')
    print('PASS saved outfits, immutable founders, native ownership and walking on both peers.', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--control', action='store_true')
    parser.add_argument('--leave-open', action='store_true')
    parser.add_argument('--save-outfits', action='store_true')
    parser.add_argument('--cancel-outfits', action='store_true')
    parser.add_argument('--previous-outfit-control', action='store_true')
    parser.add_argument('--restored', action='store_true')
    args = parser.parse_args()
    h, g = Console(args.host), Console(args.guest)
    passed = False
    try:
        if args.previous_outfit_control: command(h, 'coop.fix tribe.outfit_recovery off')
        if args.restored:
            restored_outfits(h, g)
            passed = True
            return
        for c in (h, g):
            if 'Tribe' not in command(c, 'game.mode') or 'busy: -' not in command(c, 'coop.status'):
                raise RuntimeError('Fixture is not playable Tribe')
            command(c, 'coop.uistate 0')
        home = re.search(r'host ([0-9A-F]{8})', command(h, 'coop.founders'))[1]
        own = re.search(r'mine creature: ([0-9A-F]{8})', command(g, 'coop.creations'))[1]
        if own == home: raise RuntimeError('Fixture needs distinct species')
        command(g, 'coop.fix tribe.own_outfitter ' + ('off' if args.control else 'on'))

        def tap(c, x, y):
            command(c, f'move {x} {y}')
            time.sleep(.25)
            command(c, f'click {x} {y}')
            time.sleep(1)

        # Native hut -> Planner tutorial -> hat tab -> Edit Outfit portrait.
        for x, y in ((589, 244), (760, 527), (205, 41), (49, 425)):
            tap(h, x, y)
        wait_until(lambda: 'vote active' in command(g, 'coop.vote') or
                   'wants to evolve' in command(g, 'coop.vote'), 'Outfit vote did not start', 10)
        tap(g, 347, 540)  # F9 Ready button, not a test-only vote command.

        def editor(c):
            result = c.call('coop.editor')
            return result['out'] if result['ok'] and 'editor 156276D1 ' in result['out'] else ''

        wait_until(lambda: bool(editor(h)), 'Host outfitter did not open', 80)
        wait_until(lambda: bool(editor(g)), 'Guest outfitter did not open', 15)
        rows = [editor(c) for c in (h, g)]
        for row, key in zip(rows, (home, own)):
            print(row, flush=True)
            if f'key {key} ' not in row: raise RuntimeError('Outfitter replaced a player species')
        palettes = [re.search(r'unlockable (\d+) locked (\d+).*available=(\d+):([0-9A-F]+)', row).groups()
                    for row in rows]
        if palettes[0] != palettes[1] or int(palettes[1][1]) == 0:
            raise RuntimeError('Native outfit palettes do not share earned unlocks')
        if args.save_outfits:
            drag_slow(h, 70, 111, 755, 300)
            drag_slow(g, 70, 111, 755, 335)
            tap(g, 1155, 645)
            wait_until(lambda: 'Tribe' in command(g, 'game.mode'), 'Guest did not leave its outfitter', 20)
            tap(h, 1155, 645)
            wait_until(lambda: 'Tribe' in command(h, 'game.mode'), 'Host did not leave its outfitter', 20)
            tap(h, 1155, 645)  # leave Planner: native host applies its new species here
            restored_outfits(h, g)
        elif args.cancel_outfits:
            tap(g, 1119, 645)  # native red cross, cancel without a saved creation
            wait_until(lambda: 'Tribe' in command(g, 'game.mode'), 'Guest did not cancel its outfitter', 20)
            tap(h, 1119, 645)
            wait_until(lambda: 'Tribe' in command(h, 'game.mode'), 'Host did not cancel its outfitter', 20)
            tap(h, 1155, 645)
            wait_until(lambda: 'token=0 ' in command(h, 'coop.tribe outfits'), 'Cancelled outfit vote remained pending', 45)
            if re.search(r'mine creature: ([0-9A-F]{8})',command(g,'coop.creations'))[1] != own:
                raise RuntimeError('Cancelling changed the guest campaign choice')
            restored_outfits(h, g, expect_changed=False)
        for c in (h, g):
            if 'total faults: 0' not in command(c, 'exc'): raise RuntimeError('Native fault occurred')
        passed = True
        print('PASS distinct native Tribal outfitters and matching earned clothing palettes.', flush=True)
    finally:
        if not passed or not args.leave_open:
            for c in (h, g):
                try:
                    print(c.call('exc'), flush=True)
                except Exception as error:
                    print('Diagnostics:', error, flush=True)
                finally:
                    try: c.call('quit')
                    except Exception as error: print('Cleanup:', error, flush=True)


if __name__ == '__main__': main()
