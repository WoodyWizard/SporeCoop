"""BUG-9052: advance the visible first-use pan/zoom lessons using native input.

Start at the arrow-key lesson in a disposable 1176x664 Civilization profile.
This checks the lesson transition, not completion of the entire tutorial.
No tutorial state, camera transforms or progression values are written.
"""
import argparse
import time

from duo_orders import wait_until
from duo_villages import Console, command
from uifind import shown


PAN = 'Use the arrow keys to pan the camera'
ZOOM = 'Use your mouse wheel or the +/- keys to zoom the camera'
SELECT = 'Click on your vehicle with the left mouse button'


def verify(c, control=False):
    assert 'duoqa' in command(c, 'coop.savedb').lower(), 'Disposable profile required'
    assert 'Civ' in command(c, 'game.mode')
    assert 'screen 1176 664' in command(c, 'coop.uistate'), 'Unsupported fixture size'
    assert shown(c, PAN), 'Start at the visible native panning lesson'
    command(c, 'move 580 330')
    try:
        if control:
            command(c, 'key 0x26 down')
            time.sleep(2)
        else:
            # Polling a held key can move the camera without satisfying the
            # lesson. Repeat normal key messages, keeping the cursor in-world.
            for _ in range(15):
                command(c, 'key 0x26 down')
                time.sleep(.1)
                if shown(c, ZOOM):
                    break
    finally:
        command(c, 'key 0x26 up')
    wait_until(lambda: shown(c, ZOOM), 'Native panning lesson did not advance', 5)
    assert not shown(c, PAN)
    print('PASS native pan lesson advanced to zoom.', flush=True)
    command(c, 'wheel 580 330 3')
    wait_until(lambda: shown(c, SELECT), 'Native zoom lesson did not advance', 15)
    assert not shown(c, ZOOM)
    assert 'total faults: 0' in command(c, 'exc')
    print('PASS native zoom advanced to vehicle selection; zero faults.', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--control', action='store_true', help='Use one held key message instead of repeats.')
    args = parser.parse_args()
    verify(Console(args.host), args.control)
