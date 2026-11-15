#!/usr/bin/env python3
"""BUG-1134: game.cpp's two call guards left set by a fault the game caught do not switch the animation or effect
relay off.

game.cpp holds two flags around its own game calls: `game.puppet_anim` while it plays a partner's animation on the
partner's puppet (the own avatar's animation hook must not send it back) and `game.fx_relay` while it plays a
partner's effect (the world effect hooks must not send it back). A fault inside the call that the game's handler
catches unwinds past the clear: the flag stays set, the host's own animations and world effects are not relayed
until the next such call clears it. call_guard.h registers both since BUG-1134; the frame's swap guard tick clears
one still set.

Two games on the creature fixture:
1. Host: `coop.guard unwind game.puppet_anim N` and `game.fx_relay N`: its next N plays of a partner's animation /
   effect end as a caught fault ends them (the guard stays set).
2. Guest: the mating call (its game plays the animation and makes the effect, both relayed to the host): the host's
   game plays them on the guest's puppet, the guarded calls cut short.
3. Host: its own mating call: the guest's game must play the host's animation on the host's puppet (`coop.npc cur`:
   the game's current animation of the puppet, `cur p<id>`), which needs the host's animation relay.
4. Host: a part found (a world effect of the host's game, pool 5): the guest's game must have the game's effect
   object active (`coop.effects`), which needs the host's world effect relay.
SPORE_FIX_OFF=core.call_guard_reset must FAIL (the host's guards stay set: nothing of 3 and 4 is sent).

  SPORE_LANE=9 python3 tools/tests/coop_call_guard_game.py"""
import re
import time

from coop_creature_effects import busy, case, free_games, my_id
from twoinst import Checks, start

GUARDS = ('game.puppet_anim', 'game.fx_relay')
# holds left set: more than the guest's mating call makes on the host (lab 20261115-025041: 5 animations, 1 effect), so
# that without the reset the guard is still set when the host acts (each hold that is not cut short clears it again)
ARMED = {'game.puppet_anim': 10, 'game.fx_relay': 5}


def guards(con):
    """{name: (set, cleared, unwind left)}"""
    out = con.call('coop.guard')['out']
    r = {}
    for g in GUARDS:
        m = re.search(r'^' + re.escape(g) + r' set=(\d) cleared=(\d+)(?: unwind=(\d+))?$', out, re.M)
        if m:
            r[g] = (int(m.group(1)), int(m.group(2)), int(m.group(3) or 0))
    return r


def cur(con, who):
    m = re.search(r'^cur ' + who + r' anim=([0-9A-F]{8})', con.call('coop.npc cur')['out'], re.M)
    return m.group(1) if m else None


def mating_call(con):
    con.call('key backspace down')
    time.sleep(0.15)
    con.call('key backspace up')


def sample(host, guest, hid, secs):
    """the host avatar's and the guest's puppet of the host's animations, every 0.1 s"""
    a, b = [], []
    t0 = time.time()
    while time.time() - t0 < secs:
        x, y = cur(host, 'avatar'), cur(guest, f'p{hid}')
        if x:
            a.append(x)
        if y:
            b.append(y)
        time.sleep(0.1)
    return a, b


def main():
    host, guest = start('creature')
    ck = Checks()
    ck(guest.call('coop.avatar')['ok'], 'the guest is in the host\'s world')
    for c in (host, guest):
        c.call('coop.dna local 100')  # the mating call needs DNA; local: no brain-upgrade cinematic
    ck(free_games(host, guest), f'both games take input (busy: {busy(host)} / {busy(guest)})')
    hid, gid = my_id(host), my_id(guest)
    time.sleep(3)
    g0 = guards(host)
    print(f'  host ids {hid} / guest {gid}, host guards (set, cleared, unwind) {g0}', flush=True)
    if not ck(len(g0) == 2 and not any(v[0] for v in g0.values()), f'the host has {GUARDS} registered and clear'):
        return ck.result()

    # 1-2: the host's plays of the guest's animation and effect end like a caught fault
    for g in GUARDS:
        print('  host:', host.call(f'coop.guard unwind {g} {ARMED[g]}')['out'].strip(), flush=True)
    used = {}
    for attempt in range(3):
        free_games(host, guest, 8)
        mating_call(guest)
        t0 = time.time()
        while time.time() - t0 < 4:
            time.sleep(0.2)
            used = {g: ARMED[g] - v[2] for g, v in guards(host).items()}
            if all(used.get(g, 0) > 0 for g in GUARDS):
                break
        if all(used.get(g, 0) > 0 for g in GUARDS):
            break
        print(f'  guest mating call {attempt + 1}: holds cut short so far {used}', flush=True)
    ck(all(used.get(g, 0) > 0 for g in GUARDS),
       f'the host\'s game played the guest\'s animation and effect, both calls cut short like a fault ({used})')
    time.sleep(1.5)  # the guest's call ends; a reset (the fix) clears the guards on the next frame
    g1 = guards(host)
    print(f'  host guards 1.5 s later (set, cleared, unwind): {g1}', flush=True)

    # 3: the host's own animation reaches the guest's puppet of the host
    free_games(host, guest, 8)
    before_a, before_b = sample(host, guest, hid, 1.0)
    print(f'  before: host avatar {sorted(set(before_a))}, guest\'s puppet {sorted(set(before_b))}', flush=True)
    seen = set()
    for attempt in range(3):
        mating_call(host)
        a, b = sample(host, guest, hid, 4.0)
        new = set(a) - set(before_a)
        seen = new & set(b)
        print(f'  host mating call {attempt + 1}: host avatar new {sorted(new)}, guest\'s puppet {sorted(set(b))}',
              flush=True)
        if seen or not new:
            break
    ck(seen, f'the guest\'s game plays the host\'s new animation on the host\'s puppet ({sorted(seen)}): '
             'the host\'s animation relay works after the fault')

    # 4: the host's world effect reaches the guest's game
    case(host, guest, hid, gid, ck, 'host part found (world effect)', lambda: host.call('coop.parts find 1'), {5})
    print(f'  host guards at the end (set, cleared, unwind): {guards(host)}', flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
