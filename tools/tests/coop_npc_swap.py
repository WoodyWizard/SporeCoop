#!/usr/bin/env python3
"""An NPC tick that does not return leaves no puppet as the game's avatar (BUG-0110).

On the host an NPC near a partner ticks its AI with the partner's puppet written into the game's
avatar slot (cGameNounManager +0x54) and puts the real avatar back afterwards. A fault inside that
tick, caught by the game's own handler, unwinds past the restore: the puppet stayed the game's
avatar for good and no NPC targeted a partner again.

The guest stands next to an NPC far from the host until NPC ticks run with its puppet as the
player (`ai_swaps` grows). Then `coop.npc swapfault` makes the next such tick leave like a fault
would (without its restore). The game's avatar slot must hold the host's own creature again a
moment later, and NPCs go on treating the partner as the player.

  coop_npc_swap.py [--without-fix]   (--without-fix: coop.npc swapguard off, the old behaviour,
                                      so the test must FAIL)"""
import re
import struct
import sys
import time

from harness import eax
from twoinst import Checks, start


def u32(con, a):
    b = bytearray()
    for line in con.call(f'mem.read {a:#x} 4')['out'].splitlines():
        p = line.split()
        if len(p) > 1 and re.fullmatch(r'[0-9A-F]{8}', p[0]):
            b += bytes(int(x, 16) for x in p[1:17] if re.fullmatch(r'[0-9A-F]{2}', x))
    return struct.unpack('<I', bytes(b[:4]))[0] if len(b) >= 4 else None


def game_avatar(con):
    """the game's own avatar slot (cGameNounManager +0x54), read between frames"""
    nm = eax(con, 'Simulator::cGameNounManager::Get cdecl')
    return u32(con, nm + 0x54) if nm else None


def swaps(con):
    m = re.search(r'ai_swaps=(\d+)', con.call('coop.npc')['out'])
    return int(m.group(1)) if m else -1


def npcs(con):
    """host: {id: (dist, dead)}"""
    return {m.group(1): (float(m.group(2)), bool(m.group(3)))
            for m in re.finditer(r'#(\d+) [0-9A-F]{8} species=[0-9A-F]+ hp=[\d.]+ dist=([\d.]+)( DEAD)?',
                                 con.call('coop.npc list')['out'])}


def main():
    without = '--without-fix' in sys.argv
    host, guest = start()
    ck = Checks()
    if without:
        print(' ', host.call('coop.npc swapguard off')['out'].strip())
    time.sleep(3)
    real = game_avatar(host)
    pup = re.search(r'remote 1 .*?puppet=([0-9A-F]{8})', host.call('coop.status')['out'])
    pup = int(pup.group(1), 16) if pup else 0
    ck(bool(real) and bool(pup), f'host avatar {real and hex(real)}, guest puppet {pup and hex(pup)}')
    # the guest walks to NPCs far from the host until NPC ticks see it as the player
    far = sorted((-d, i) for i, (d, dead) in npcs(host).items() if not dead and d > 45)
    s0, ok = swaps(host), False
    for _, nid in reversed(far[-4:]):
        guest.call(f'coop.social goto {nid}')
        t0 = time.time()
        while time.time() - t0 < 6 and not ok:
            ok = swaps(host) > s0
            time.sleep(0.5)
        if ok:
            break
    ck(ok, f'NPC ticks run with the guest\'s puppet as the player (ai_swaps {s0} -> {swaps(host)})')
    print(' ', host.call('coop.npc swapfault')['out'].strip())
    time.sleep(2)
    reads = [game_avatar(host) for _ in range(5)]
    print(f'  game avatar slot after the fault: {[hex(r) for r in reads if r]} (avatar {real:#x}, puppet {pup:#x})')
    ck(all(r == real for r in reads), 'the host\'s own creature is the game\'s avatar again, not the puppet')
    s1 = swaps(host)
    time.sleep(4)
    s2 = swaps(host)
    ck(s2 > s1, f'NPCs still treat the partner as the player afterwards (ai_swaps {s1} -> {s2})')
    print(' ', host.call('coop.npc')['out'].splitlines()[0])
    host.call('coop.npc swapguard on')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
