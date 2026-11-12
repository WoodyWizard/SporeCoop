"""O1 of 0.7.13 (BUG-0427's children): a guest that plays a world of its own while it stays in the session.

load_own(guest, planet, name): the guest copies the host's world it got (the newest <planet> under its Games folder)
into the slot Game9 under another planet name and loads it with `coop.worldfix autoload 0` (the session's world
offers are not loaded meanwhile), as coop_world_gate_vote.py and coop_space_own_save.py do. The loaded save is a
copy in a test profile: the player's own profile and the loaded slot of the lane's fixture are never touched.
"""
import glob
import os
import re
import shutil
import time

import lane


def world_id(con):
    """(mode, world crc) of this game as the mod sees them (coop.status's first line)"""
    m = re.search(r'own world: mode ([0-9A-F]{8}) world ([0-9A-F]{8})', con.call('coop.status')['out'])
    return (m.group(1), m.group(2)) if m else (None, None)


def connected(con):
    st = con.call('coop.status')['out'].lower()
    return 'client' in st or 'connected' in st


def copy_world(planet, name, slot='Game9'):
    games = os.path.join(lane.GUEST_DIR, 'Games')
    srcs = sorted(glob.glob(os.path.join(games, '*', planet)), key=os.path.getmtime)
    srcs = [s for s in srcs if os.path.basename(os.path.dirname(s)) != slot]
    if not srcs:
        return False
    src = os.path.dirname(srcs[-1])
    dst = os.path.join(games, slot)
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(src, dst)
    if os.path.exists(os.path.join(dst, 'sporecoop.txt')):
        os.remove(os.path.join(dst, 'sporecoop.txt'))
    os.rename(os.path.join(dst, planet), os.path.join(dst, name + '.spo'))
    return True


def load_own(guest, planet, name, mode_word, timeout=150, slot='Game9'):
    """the guest, still connected, loads a copy of `planet` named `name`; True once it plays it (its mode name has
    `mode_word`, its world id changed and its frames run)"""
    if not copy_world(planet, name, slot):
        print(f'  own world: no {planet} in the guest\'s Games folder', flush=True)
        return False
    _, w0 = world_id(guest)
    guest.call('coop.worldfix autoload 0')
    print(' ', guest.call(f'coop.load {slot} {name}')['out'].strip(), flush=True)
    t0 = time.time()
    while time.time() - t0 < timeout:
        time.sleep(3)
        m = guest.call('game.mode')['out']
        _, w = world_id(guest)
        if mode_word in m and w not in (None, '00000000', w0):
            guest.call('wait.frames 200 60000')
            print(f'  own world: the guest plays {name}.spo (world {w0} -> {w}) after {time.time() - t0:.0f} s', flush=True)
            return True
    print(f'  own world: the guest did not reach {name} ({guest.call("game.mode")["out"].strip()}, {world_id(guest)})', flush=True)
    return False


def net_out(host, pid=1):
    """the bytes the host sent to player `pid` so far (coop.status 'net to N:')"""
    m = re.search(r'net to %d: out_bytes=(\d+) out_packets=(\d+)' % pid, host.call('coop.status')['out'])
    return (int(m.group(1)), int(m.group(2))) if m else (None, None)
