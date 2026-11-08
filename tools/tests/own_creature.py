"""Test helper (K2b, BUG-0732 / BUG-0734): make the guest play one of its own creatures (not the world's species).

Since BUG-0345 a guest plays the world's species unless it chose another creation in that world; a test of the
rules a guest's OWN creature misses chooses one live (`coop.creations choose creature <inst>`, as the creations
menu does) and waits until its game says the avatar is another species than the world's (`coop.hp`: other).
"""
import re
import time


def play_own(guest, timeout=40):
    """True when the guest's avatar is of another species than the world's player species"""
    def other():
        m = re.search(r'^own: .* (same|other) base_game', guest.call('coop.hp')['out'], re.M)
        return m and m.group(1) == 'other'
    if other():
        return True
    opts = re.findall(r'^creature ([0-9A-F]{8}) ', guest.call('coop.creations list')['out'], re.M)
    for inst in opts:
        print(f'  guest: choose creature {inst}:', guest.call(f'coop.creations choose creature {inst}')['out'].strip()[:80],
              flush=True)
        t0 = time.time()
        while time.time() - t0 < timeout:
            time.sleep(1)
            if other():
                time.sleep(3)  # the new body settles
                return True
    return False


def back_to_world(guest):
    """the lane's guest profile back to the world's species (the choice is recorded per world, worlds.ini)"""
    guest.call('coop.creations choose creature none')
    guest.call('coop.creations worlds forget')
