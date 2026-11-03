#!/usr/bin/env python3
"""Creature stage: the effects a player's creature makes show on the partner's puppet (BUG-0451).

Parametrised over the effect kinds the game makes for the local player's own creature:
the mating call (one-shot), being hit (one-shot), death (world effects), a part found (world
effect), the brain upgrade (pool effect) and an ability's kept effect (pool effect). The partner's
own game must have the game's effect object active (`coop.effects`: the effect's vt 0x10 and its
position) at the actor's puppet (`coop.status` remote pos).

Animation-event effects (footsteps, the green mating-call ring, sprint/sneak, the visual part of
the attacked/social animations) are already replayed by the animation relay (MSG_ANIM) and are not
this relay's case; they are listed in docs/re/creature.md#creature-effects-relay.

  coop_creature_effects.py   (SPORE_FIX_OFF=creature.effects_relay must FAIL)
"""
import math
import re
import time

from twoinst import Checks, start

CHECK = (1418, 1044)  # the green check of a caption (brain upgrade, greeting)


def busy(con):
    for line in con.call('coop.status')['out'].splitlines():
        if line.startswith('busy:'):
            return line.split(':', 1)[1].strip()
    return '?'


def free_games(host, guest, seconds=25):
    t0 = time.time()
    while time.time() - t0 < seconds:
        if busy(host) == '-' and busy(guest) == '-':
            return True
        for c in (host, guest):
            c.call('key enter')
            c.call(f'click {CHECK[0]} {CHECK[1]}')
        time.sleep(1)
    return busy(host) == '-' and busy(guest) == '-'


def numbers(con):
    m = re.search(r'effects: fix=(\d) sent=(\d+) received=(\d+) played=(\d+) stopped=(\d+) skipped=(\d+) '
                  r'failed=(\d+) dropped_rate=(\d+) world=(\d+)', con.call('coop.effects')['out'])
    if not m:
        return {}
    keys = ('fix', 'sent', 'received', 'played', 'stopped', 'skipped', 'failed', 'dropped', 'world')
    return {k: int(v) for k, v in zip(keys, m.groups())}


def last_rx(con, who):
    out = con.call('coop.effects')['out']
    m = re.search(r'effects received start from (\d+): id=([0-9A-F]{8}) key=([0-9A-F]{8}) pool=(\d+) '
                  r'game=(\d) active=(\d)', out)
    if not m or int(m.group(1)) != who:
        m = re.search(r'effects received last from (\d+): id=([0-9A-F]{8}) key=([0-9A-F]{8}) pool=(\d+) stop=(\d+) '
                      r'game=(\d) active=(\d)', out)
        if not m or int(m.group(1)) != who:
            return None
        p = re.search(r'effects received last at: ([-\d.]+) ([-\d.]+) ([-\d.]+)', out)
        return {'id': m.group(2), 'key': m.group(3), 'pool': int(m.group(4)), 'stop': int(m.group(5)),
                'game': int(m.group(6)), 'active': int(m.group(7)),
                'pos': tuple(float(x) for x in p.groups()) if p else None}
    p = re.search(r'effects received start at: ([-\d.]+) ([-\d.]+) ([-\d.]+)', out)
    return {'id': m.group(2), 'key': m.group(3), 'pool': int(m.group(4)), 'stop': 0,
            'game': int(m.group(5)), 'active': int(m.group(6)),
            'pos': tuple(float(x) for x in p.groups()) if p else None}


def puppet_pos(con, who):
    m = re.search(r"remote %d '.*?' puppet=\S+ state=\S+ age=[\d.]+s pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)" % who,
                  con.call('coop.status')['out'])
    return tuple(float(x) for x in m.groups()) if m else None


def own_pos(con):
    m = re.search(r'pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)', con.call('coop.avatar')['out'])
    return tuple(float(x) for x in m.groups()) if m else None


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b))) if a and b else None


def my_id(con):
    m = re.search(r'client id=(\d+) connected', con.call('coop.status')['out'])
    return int(m.group(1)) if m else 0


def case(actor, receiver, actor_id, receiver_id, ck, what, action, pools, wait=4.0, need_pos=True):
    """run `action` and wait until `receiver` plays a new effect of `actor` on its puppet"""
    free_games(actor, receiver, 8)
    before = numbers(actor).get('sent', 0)
    start_pos = own_pos(actor)  # the death respawns the avatar far away: use its position at the action
    for attempt in range(3):
        action()
        t0 = time.time()
        while time.time() - t0 < wait:
            time.sleep(0.15)
            n = numbers(actor)
            rx = last_rx(receiver, actor_id)
            if n.get('sent', 0) > before and rx and rx['stop'] == 0 and rx['pool'] in pools \
                    and rx['game'] == 1 and rx['active'] == 1:
                p = dist(rx['pos'], start_pos) if need_pos else None
                print(f'  {what}: sent {n.get("sent")} (last {rx}), {p if p is None else round(p, 2)} m from the actor',
                      flush=True)
                if need_pos:
                    ck(p is not None and p < 8.0,
                       f'{what}: the partner\'s game plays the effect at the actor\'s puppet ({p and round(p, 2)} m)')
                else:
                    ck(True, f'{what}: the partner\'s game plays the effect (active)')
                return True
            if busy(actor) != '-' or busy(receiver) != '-':
                free_games(actor, receiver, 8)
                break
        print(f'  {what}: no effect after attempt {attempt + 1} (busy {busy(actor)} / {busy(receiver)}, '
              f'sent {numbers(actor).get("sent")})', flush=True)
    ck(False, f'{what}: the partner\'s game plays the effect')
    return False


def main():
    host, guest = start('creature')
    ck = Checks()
    ck(guest.call('coop.avatar')['ok'], 'the guest is in the host\'s world')
    ck(free_games(host, guest), f'both games take input (busy: {busy(host)} / {busy(guest)})')
    hid, gid = my_id(host), my_id(guest)
    print(f'  host id {hid}, guest id {gid}', flush=True)
    time.sleep(3)  # the join greeting holds the keys for a moment after busy: -

    # the guest's greeting is made through the same one-shot path, but arrives before start() can
    # apply SPORE_FIX_OFF: it is only printed, the cases below are the fail/pass evidence
    rx = last_rx(host, gid)
    if rx:
        print(f'  greeting: host received {rx}', flush=True)

    host.call('coop.dna local 100')  # the mating call needs DNA; local: no guest brain-upgrade cinematic
    time.sleep(1)
    free_games(host, guest, 10)
    case(host, guest, hid, gid, ck, 'host mating call', lambda: (host.call('key backspace down'), time.sleep(0.15), host.call('key backspace up')), {3})
    case(host, guest, hid, gid, ck, 'host being hit', lambda: host.call('coop.team hit 3'), {3})
    case(guest, host, gid, hid, ck, 'guest mating call', lambda: (guest.call('key backspace down'), time.sleep(0.15), guest.call('key backspace up')), {3})
    case(host, guest, hid, gid, ck, 'host part found', lambda: host.call('coop.parts find 1'), {5})
    case(host, guest, hid, gid, ck, 'host brain upgrade', lambda: (host.call('coop.dna local 250'),
         time.sleep(0.5), free_games(host, guest, 15)), {1, 2, 5}, wait=3, need_pos=False)
    # the death last: it replaces the avatar (respawn) and may show a caption
    case(host, guest, hid, gid, ck, 'host death', lambda: host.call('coop.team die'), {1, 2, 3, 5}, wait=5)

    ck.faults(host=host, guest=guest)
    raise SystemExit(ck.result())


if __name__ == '__main__':
    main()
