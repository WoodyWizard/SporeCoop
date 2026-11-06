"""Take training as the guest while the host skips it on the first Vantiegan launch."""
import argparse
import re
import time

import uifind
from duo_orders import wait_until
from duo_tribe_graduation_editor import button, click
from duo_villages import Console, command


def context(peer):
    result = command(peer, 'call Simulator::GetCurrentContext cdecl')
    return int(re.search(r'eax=0x(\w+)', result)[1], 16)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    args = parser.parse_args()
    h, g = Console(args.host), Console(args.guest)
    skip = 'Been there, done that'
    for peer in (h, g):
        assert 'duoqa' in command(peer, 'coop.savedb').lower()
        assert "world 'Vantiegan.spo'" in command(peer, 'coop.avatar') and context(peer) == 0
        assert uifind.shown(peer,skip)
    click(g,588,307)
    wait_until(lambda:not uifind.shown(g,skip),'Guest training response was not accepted',15)
    assert uifind.shown(h, skip), 'Guest response advanced the host dialogue'
    click(h,588,341)
    wait_until(lambda:uifind.shown(h,'Get my spaceship ready for space travel'),'Host skip confirmation missing',15)
    click(h,588,339)
    time.sleep(2)
    for _ in range(3):
        click(h,588,308)
        time.sleep(2)
    def in_orbit():
        if context(h) in (1,2): return True
        for widget in ('0445C99D','062EF353','FFFFFFF1'): button(h,widget)
        command(h,'wheel 590 332 -25')
        time.sleep(1.5)
        return False
    wait_until(in_orbit,'Host did not depart after skipping training',65)
    time.sleep(12)
    assert context(g) == 0 and 'Space' in command(g,'game.mode')
    for peer in (h,g):
        assert 'total faults: 0' in command(peer,'exc')
    print('PASS guest accepted training; host independently skipped and reached orbit; guest stayed on the homeworld.')


if __name__ == '__main__':
    main()
