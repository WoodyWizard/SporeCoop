#!/usr/bin/env python3
"""BUG-0917: the avatar's combat target names a creature that left the world (Creature, host).

Two long lab tests failed in one night on a caught fault in the game's Ability_TargetGap (0xD99A20), called by the
test command `coop.abilities reach` (coop.dll c_abilities, the play bots poll it): the command took the avatar's
target field (+0xE7C, a combatant) for a creature after a `readable` check and called the game on it. The field
can still name a creature that is gone: the partner's puppet the host clicked, removed when the partner dies or is
hidden (lab 20261111-025527: +0xE7C named the removed puppet 0.2 s after the removal), a proxy the host's game
removed.

Found with it: the game does not clear the avatar's target when the creature it names is destroyed (it does for
NPCs), and the puppet's memory is given to a later puppet: the game's own ability use (a combat key) then reads freed
memory. The mod now clears the avatar's target before a creature is destroyed (game.avatar_stale_target).

Rounds on the host: the combat stance, a real click on the partner's puppet (the game sets the avatar's target to
it, docs/re/creature.md#creature-attack-reach), then the puppet is removed as on the partner's death
(`coop.puppet respawn 3`). Then, while the puppet is away:
* `reach` rounds: `coop.abilities reach` is asked every 50 ms for 1.5 s. It must never report the removed puppet as
  its target (it reports `target none` for anything that is not a live creature of the game's object list);
* `key` rounds: the combat key is pressed (real input) while the field still names the removed puppet: the game's
  own ability use reads the same field.
The setup (BUG-0934): the click goes at the puppet's body, not its feet; a click that picked another object is said,
the target is cleared (the game's SetTarget) and another point is clicked; when no click takes the puppet the game's
SetTarget names it (`coop.abilities target <puppet>`), so that the round still runs. At least 6 of 8 rounds must run
and at least 2 by a real click. STALE_TARGET_MISS=1: every click misses (all rounds by SetTarget: the test FAILS on
the real-click count only); OLD_STALE_TARGET_SETUP=1: the setup before BUG-0934.
Checks: the target was the puppet at the removal (else the test proves nothing); the game's target field of the
avatar (read from the game's memory) does not name the removed puppet 0.3 s after the removal; reach never used the
removed puppet; no fault on either game.

  coop_avatar_stale_target.py [--rounds N]
  SPORE_FIX_OFF=game.avatar_stale_target: the field names the removed puppet for good, FAILS (reach is safe on it);
  SPORE_FIX_OFF=game.avatar_stale_target,test.reach_target: reach also calls the game on the removed puppet, FAILS"""
import argparse
import os
import re
import struct
import time

from coop_creature_kills import read
from twoinst import Checks, start

W, H = 1440, 1080
HIDE_S = 3.0
MIN_ROUNDS = 6  # of 8: the setup must not lose rounds (BUG-0934: 2 of 8 failed the test)
MIN_CLICK_ROUNDS = 2


def u32(con, a):
    b = read(con, a, 4)
    return struct.unpack('<I', b)[0] if len(b) == 4 else None


def puppet(con):
    m = re.search(r'puppet=([0-9A-F]{8})', con.call('coop.status')['out'])
    return int(m.group(1), 16) if m else 0


def avatar(con):
    m = re.search(r'local=([0-9A-Fa-f]{8})', con.call('coop.avatar')['out'])
    return int(m.group(1), 16) if m else 0


def fault_count(con):
    m = re.search(r'total faults: (\d+)', con.call('exc')['out'])
    return int(m.group(1)) if m else -1


# where to click, from the puppet's feet (`coop.targets` projects its position): the body first, then the feet (the
# old point: a carcass, an egg or a fruit lying there takes the click, BUG-0934), then higher and to the sides
OFFSETS = ((0, -22), (0, 0), (0, -45), (-14, -22), (14, -22), (0, -70))


def target_words(host):
    """what the avatar's target is, in the game's words (coop.abilities reach, first line)"""
    out = host.call('coop.abilities reach').get('out', '')
    return out.splitlines()[0].strip() if out else '?'


def click_puppet(host, av, pup, tries=len(OFFSETS)):
    """real clicks on the partner's puppet until the game's target of the avatar is its combatant: 'click'.
    A click that picked another object (BUG-0934: six rounds in a row on one, lab 20261111-090547) is said with what
    it picked, the target is cleared through the game's own SetTarget and the next point on the body is clicked.
    When no click takes the puppet, the game's SetTarget names it ('set'): the round still tests the removal, and
    main() asks for rounds by a real click too. None: the puppet could not be made the target at all."""
    if os.environ.get('OLD_STALE_TARGET_SETUP') == '1':  # the setup before BUG-0934 (to see it fail): the feet, 4 times
        for _ in range(4):
            for line in host.call('coop.targets')['out'].splitlines():
                f = line.split()
                if len(f) == 5 and f[0] == 'player' and 20 < int(f[3]) < W - 20 and 90 < int(f[4]) < H - 170:
                    host.call(f'click {f[3]} {f[4]}')
                    break
            time.sleep(0.4)
            if u32(host, av + 0xE7C) == pup + 0x5A8:
                return 'click'
        return None
    for i in range(tries):
        dx, dy = OFFSETS[i % len(OFFSETS)]
        if os.environ.get('STALE_TARGET_MISS') == '1':
            dx, dy = 0, -400  # tests of this setup: every click goes over the puppet's head
        clicked = False
        for line in host.call('coop.targets')['out'].splitlines():
            f = line.split()
            if len(f) == 5 and f[0] == 'player':
                x, y = int(f[3]) + dx, int(f[4]) + dy
                if 20 < x < W - 20 and 90 < y < H - 170:
                    host.call(f'click {x} {y}')
                    clicked = True
                break
        time.sleep(0.4)
        t = u32(host, av + 0xE7C)
        if t == pup + 0x5A8:
            return 'click'
        if t:
            print(f'    (click {i} at the puppet {dx:+d} {dy:+d} px picked another object: +0xE7C = {t:08X}, '
                  f'{target_words(host)}: target cleared)', flush=True)
            host.call('coop.abilities target none')
            time.sleep(0.2)
        elif not clicked:
            print(f'    (click {i}: the puppet is not on the screen at {dx:+d} {dy:+d} px)', flush=True)
    out = host.call(f'coop.abilities target {pup:08X}').get('out', '').strip()
    time.sleep(0.2)
    if u32(host, av + 0xE7C) == pup + 0x5A8:
        print(f'    (no click took the puppet: the game\'s SetTarget names it: {out})', flush=True)
        return 'set'
    print(f'    (SetTarget did not name the puppet either: {out})', flush=True)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--rounds', type=int, default=8)
    a = ap.parse_args()
    host, guest = start()
    ck = Checks()
    time.sleep(2)
    host.call(f'click {W // 2 + 43} {H - 82}')  # the combat stance
    time.sleep(0.3)
    taken = by_click = used_removed = asked = keys = 0
    stale_ms, faults_by = [], {'reach': 0, 'key': 0}
    for rnd in range(a.rounds):
        av, pup = avatar(host), puppet(host)
        if not av or not pup:
            time.sleep(1)
            continue
        how = click_puppet(host, av, pup)
        if not how:
            print(f'  round {rnd}: the click did not make the puppet {pup:08X} the target '
                  f'(+0xE7C = {u32(host, av + 0xE7C) or 0:08X})', flush=True)
            continue
        taken += 1
        by_click += how == 'click'
        phase = 'reach' if rnd % 2 == 0 else 'key'
        f0 = fault_count(host)
        host.call(f'coop.puppet respawn {HIDE_S}')
        t0 = time.time()
        stale = None  # ms the field kept naming the removed puppet (None: still does at the end)
        used = n = 0
        while time.time() - t0 < 1.5:
            if phase == 'reach':
                out = host.call('coop.abilities reach')['out']
                n += 1
                m = re.search(r'target ([0-9A-Fa-f]{8}) dead=', out)
                if m and int(m.group(1), 16) == pup and puppet(host) != pup:
                    used += 1
            elif n < 3 and u32(host, av + 0xE7C) == pup + 0x5A8:
                host.call('key 1')  # the game's ability use on the field as it is
                n += 1
                time.sleep(0.25)
            if stale is None and u32(host, av + 0xE7C) != pup + 0x5A8:
                stale = int((time.time() - t0) * 1000)
            time.sleep(0.05)
        f1 = fault_count(host)
        faults_by[phase] += max(0, f1 - f0)
        asked += n if phase == 'reach' else 0
        keys += n if phase == 'key' else 0
        used_removed += used
        stale_ms.append(stale)
        print(f'  round {rnd} ({phase}, target by {how}): puppet {pup:08X} removed; the target field named it for '
              f'{"more than 1500" if stale is None else stale} ms; {n} {"asks" if phase == "reach" else "keys"}, '
              f'reach used the removed puppet {used} times, new faults {f1 - f0}', flush=True)
        time.sleep(HIDE_S + 1.0)  # the puppet is back
    print(f'  the target field after the removal: {stale_ms} ms (None = named the removed puppet for the whole 1.5 s); '
          f'faults in reach rounds {faults_by["reach"]}, in key rounds {faults_by["key"]}', flush=True)
    ck(taken >= MIN_ROUNDS, f'the puppet was the avatar\'s target at its removal {taken} times (>= {MIN_ROUNDS})')
    ck(by_click >= MIN_CLICK_ROUNDS, f'{by_click} of these rounds had the target from a real click (>= {MIN_CLICK_ROUNDS}; the others '
                                     f'from the game\'s SetTarget after the clicks picked another object)')
    ck(asked >= 20, f'coop.abilities reach was asked {asked} times while the puppet was away')
    late = [s for s in stale_ms if s is None or s > 300]
    ck(not late, f'the game\'s target field of the avatar did not name the removed puppet 0.3 s after the removal '
                 f'({len(late)} of {len(stale_ms)} rounds did)')
    ck(used_removed == 0, f'coop.abilities reach never took the removed puppet for its target ({used_removed} times)')
    ck(keys >= 1 or all(s is not None for s in stale_ms),
       f'the combat key was pressed {keys} times on the field naming the removed puppet (or the game cleared it at once)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
