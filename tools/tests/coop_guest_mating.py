#!/usr/bin/env python3
"""WP-K checks of two Lineages fork findings (PR #196) in our code: a guest's own mating at the nest, the players' way.

* BUG-1305 (fork BUG-9175): in the fork a guest's own mating never finished: its mate is a copy of the host's family
  member, run by the host, whose own AI must answer the pairing (0xD6D360), so the guest's creature stood in its mating
  action for ever.
* BUG-1304 (fork BUG-9161): in the fork's co-op runs the egg scene after a mating (cinematic CRG_LaidEgg B6A1DBE1)
  never ended by itself; the game ends it from its key handler only (Escape / T). On our host it ends by itself
  (creature_mating, editor_now: the editor request ~25 s after the click with no key pressed, lab
  rel/0.7.15.2 20261118-144132); this test checks the guest's.

Creature fixture (the guest next to the host at the nest). The guest does what a player does, with real input and no
key during the egg scene:
  1. the guest's game names a mate near the nest (`coop.drama mate` on the guest: +0xB58 & 0x10);
  2. the guest walks up to it, presses Backspace (the mating call) and left-clicks it where its game draws it
     (`coop.cocoon matexy`): its creature goes to mate (its behaviour node 02D852F0);
  3. within 90 s the pairing reaches the egg scene on the guest's game (cinematic B6A1DBE1, `coop.status`), the scene
     ends by itself, and the guest's own evolution editor opens (game.mode Editor), all without a key press.
Printed for the record: the mate's pointer and whether it is one of the host's copies (a proxy), the guest's
avatar node over time, the egg scene's length.

  coop_guest_mating.py
"""
import re
import time

from harness import mode, waiter
from coop_creature_mating import MATE_NODE, mate
from twoinst import Checks, keyframe, start

W, H = 1440, 1080
LAID_EGG = 'B6A1DBE1'
wait = waiter(step=1.0)


def matexy(con):
    m = re.search(r'mate (\S+) at (-?[\d.]+) (-?[\d.]+) (-?[\d.]+) screen (off )?(-?\d+) (-?\d+) dist (-?[\d.]+)',
                  con.call('coop.cocoon matexy')['out'])
    if not m:
        return None
    return dict(ptr=m.group(1), pos=tuple(float(m.group(i)) for i in (2, 3, 4)), on=not m.group(5),
                x=int(m.group(6)), y=int(m.group(7)), dist=float(m.group(8)))


def cine(con):
    m = re.search(r'cinematic ([0-9A-F]{8}) playing', con.call('coop.status')['out'])
    return m and m.group(1)


def in_editor(con):
    return 'Editor' in mode(con)


def main():
    host, guest = start('creature')
    ck = Checks()
    m0 = mode(guest)
    mt = None
    t0 = time.time()
    while time.time() - t0 < 60:
        mt = mate(guest)
        if mt and mt['ptr'] and mt['flags'] & 0x10:
            break
        time.sleep(1)
    print(f'  guest: {guest.call("coop.drama mate")["out"].strip()}', flush=True)
    ck(mt and mt['ptr'] and mt['flags'] & 0x10, "the guest's game picked a mate near the nest")
    if not (mt and mt['ptr']):
        ck.faults(host=host, guest=guest)
        return ck.result()
    proxies = guest.call('coop.npc list')['out']
    print(f"  the mate {mt['ptr']:08X} is {'one of the host copies (listed by coop.npc)' if ('%08X' % mt['ptr']) in proxies else 'not listed as a host copy'}",
          flush=True)

    xy = matexy(guest)
    if xy:  # next to it, as a player walks up
        guest.call('coop.npc gopos %.2f %.2f %.2f' % (xy['pos'][0] + 3, xy['pos'][1], xy['pos'][2]))
        time.sleep(2)
    guest.call('key 0x08')  # Backspace: the mating call
    time.sleep(3)
    going = False
    for i in range(8):
        xy = matexy(guest)
        if not xy or not xy['on'] or not (20 < xy['x'] < W - 20 and 90 < xy['y'] < H - 170):
            guest.call(f'drag {W // 2} {H // 2} {W // 2 + 350} {H // 2} right')
            time.sleep(0.5)
            continue
        guest.call(f"click {xy['x']} {xy['y']}")
        t1 = time.time()
        while time.time() - t1 < 3 and not going:
            m = mate(guest)
            going = bool(m and m['av_node'] == MATE_NODE)
            time.sleep(0.3)
        print(f'  click {i + 1} at {xy["x"]},{xy["y"]}: avatar node {mate(guest)["av_node"] if mate(guest) else None}', flush=True)
        if going:
            break
    ck(going, f"a left click on the mate: the guest's creature goes to mate (node {MATE_NODE})")

    # the pairing, the egg scene and the editor, with no key pressed
    egg_from = egg_to = None
    nodes = []
    t0 = time.time()
    shot = False
    while time.time() - t0 < 90 and not in_editor(guest):
        c = cine(guest)
        if c == LAID_EGG and egg_from is None:
            egg_from = time.time()
            print(f'  {time.time() - t0:5.1f} s the egg scene {LAID_EGG} plays on the guest', flush=True)
        if egg_from and egg_to is None and c != LAID_EGG:
            egg_to = time.time()
            print(f'  {time.time() - t0:5.1f} s the egg scene ended ({egg_to - egg_from:.1f} s)', flush=True)
        if egg_from and not shot:
            shot = True
            keyframe('guest_egg', "guest's screen: the egg scene after its mating (its creature and the mate at the "
                     'nest, an egg)', players={'guest': guest}, settle=0)
        m = mate(guest)
        n = m and m['av_node']
        if not nodes or nodes[-1][1] != n:
            nodes.append((round(time.time() - t0), n))
        time.sleep(1)
    print(f'  guest avatar nodes (s, node): {nodes[:12]}', flush=True)
    ck(egg_from, f"the pairing reached the egg scene {LAID_EGG} on the guest's game")
    ck(in_editor(guest), f"the guest's own evolution editor opened without a key press ({mode(guest).strip()}, "
       f"egg scene {('%.0f s' % ((egg_to or time.time()) - egg_from)) if egg_from else 'none'})")
    if in_editor(guest):
        guest.call('wait.frames 200 60000')
        time.sleep(8)
        guest.call('click 1383 1062')  # the red cross
        ck(wait(lambda: mode(guest) == m0 and guest.call('coop.avatar')['ok'], 90),
           f'the guest is back in the Creature stage ({mode(guest).strip()})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
