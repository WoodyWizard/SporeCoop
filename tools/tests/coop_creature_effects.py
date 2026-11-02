#!/usr/bin/env python3
"""A player's creature effects show on the partner's puppet (BUG-0451, docs/re/creature.md#creature-effects-relay).

The game makes the one-shot effects of a player's own creature (the mating call, ability particles,
...) in cCreatureBase::CreateEffect 0xC150D0 only on that player's machine: the partner's puppet
plays the relayed animation but does not run the local player's ability code, so it never makes
them (the owner's report: the host's mating-call hearts are on the host's screen only). The mod
sends the effect (id, bone, pool) and the partner plays it on that player's puppet with the same
game function.

Checked in the partner's game, not on our counters: `coop.effects` prints the game's effect object
of the last received effect (its vt 0x10 active flag and its position at effect+0x9C). The position
must be at the partner's puppet (`coop.status` remote line), so the effect is on the puppet.

  coop_creature_effects.py   (SPORE_FIX_OFF=creature.effects_relay must FAIL)
"""
import math
import re
import time

from twoinst import Checks, start

CHECK = (1418, 1044)  # the green check of a caption (a brain-upgrade cinematic after DNA, a greeting)


def busy(con):
    for line in con.call('coop.status')['out'].splitlines():
        if line.startswith('busy:'):
            return line.split(':', 1)[1].strip()
    return '?'


def free_games(host, guest, seconds=25):
    """close captions until both games take input (a test bot that starts next to the host may
    arrive in the creature stage greeting)"""
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
    out = con.call('coop.effects')['out']
    m = re.search(r'effects: fix=(\d) sent=(\d+) received=(\d+) played=(\d+) stopped=(\d+) skipped=(\d+) failed=(\d+)',
                  out)
    if not m:
        return {}
    keys = ('fix', 'sent', 'received', 'played', 'stopped', 'skipped', 'failed')
    return {k: int(v) for k, v in zip(keys, m.groups())}


def last_sent(con):
    m = re.search(r'effects sent last: id=([0-9A-F]{8}) key=([0-9A-F]{8}) pool=(\d+) stop=(\d+)',
                  con.call('coop.effects')['out'])
    return {'id': m.group(1), 'key': m.group(2), 'pool': int(m.group(3)), 'stop': int(m.group(4))} if m else None


def last_rx(con, who):
    out = con.call('coop.effects')['out']
    m = re.search(r'effects received last from (\d+): id=([0-9A-F]{8}) key=([0-9A-F]{8}) pool=(\d+) stop=(\d+) '
                  r'game=(\d) active=(\d)', out)
    if not m or int(m.group(1)) != who:
        return None
    p = re.search(r'effects received last at: ([-\d.]+) ([-\d.]+) ([-\d.]+)', out)
    return {'id': m.group(2), 'key': m.group(3), 'pool': int(m.group(4)), 'stop': int(m.group(5)),
            'game': int(m.group(6)), 'active': int(m.group(7)),
            'pos': tuple(float(x) for x in p.groups()) if p else None}


def puppet_pos(con, who):
    """the position of player `who`'s puppet as this game has it (the game's own object)"""
    m = re.search(r"remote %d '.*?' puppet=\S+ state=\S+ age=[\d.]+s pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\)" % who,
                  con.call('coop.status')['out'])
    return tuple(float(x) for x in m.groups()) if m else None


def dist(a, b):
    return math.sqrt(sum((x - y) ** 2 for x, y in zip(a, b))) if a and b else None


def my_id(con):
    m = re.search(r'client id=(\d+) connected', con.call('coop.status')['out'])
    if m:
        return int(m.group(1))
    return 0  # the host


def mating_call(actor, receiver, actor_id, receiver_id, ck, what):
    """press Backspace on `actor` (a player's mating call) until `receiver` plays a one-shot effect
    on the actor's puppet; true when the game's effect object is active at the puppet"""
    before = numbers(actor).get('sent', 0)
    for attempt in range(3):
        actor.call('key backspace')
        t0 = time.time()
        while time.time() - t0 < 5:
            time.sleep(0.15)
            n = numbers(actor)
            rx = last_rx(receiver, actor_id)
            if n.get('sent', 0) > before and rx and rx['pool'] == 3 and rx['game'] == 1 and rx['active'] == 1:
                p = dist(rx['pos'], puppet_pos(receiver, actor_id))
                print(f'  {what}: sent {last_sent(actor)}, received by {receiver_id}: {rx}, '
                      f'{p if p is None else round(p, 2)} m from its puppet', flush=True)
                ck(p is not None and p < 8.0,
                   f'{what}: the partner\'s game plays the effect at the actor\'s puppet ({p and round(p, 2)} m)')
                return True
            if busy(actor) != '-' or busy(receiver) != '-':
                free_games(actor, receiver, 8)
                break
        print(f'  {what}: no one-shot effect after attempt {attempt + 1} '
              f'(busy {busy(actor)} / {busy(receiver)}, sent {numbers(actor).get("sent")} -> '
              f'{numbers(receiver).get("received")})', flush=True)
    return False


def main():
    host, guest = start('creature')
    ck = Checks()
    ck(guest.call('coop.avatar')['ok'], 'the guest is in the host\'s world')
    ck(free_games(host, guest), f'both games take input (busy: {busy(host)} / {busy(guest)})')
    hid, gid = my_id(host), my_id(guest)
    print(f'  host id {hid}, guest id {gid}', flush=True)

    # 1. the guest's creature greets when it appears: a one-shot effect (a different game path
    #    from the mating call) must reach the host and be played on the guest's puppet
    rx = last_rx(host, gid)
    if rx:
        p = dist(rx['pos'], puppet_pos(host, gid))
        print(f'  greeting: host received {rx}, {p if p is None else round(p, 2)} m from the guest puppet',
              flush=True)
        # the greeting comes with the join, before start() applies SPORE_FIX_OFF: it is checked only
        # when the switch was on (the fix-off run proves the mating call checks below, not this one)
        if numbers(host).get('fix', 1) == 1:
            ck(rx['stop'] == 0 and rx['game'] == 1 and rx['active'] == 1 and p is not None and p < 8.0,
               'the guest\'s greeting effect is played on the guest\'s puppet in the host\'s game')
    else:
        print('  greeting: the host received no join effect (not checked)', flush=True)

    # 2. the host's mating call (a one-shot effect of the local avatar's ability code)
    ok = mating_call(host, guest, hid, gid, ck, 'host mating call')
    ck(ok, 'the host\'s mating call reaches the guest and is played on the host\'s puppet')

    # 3. the same from the guest (the other direction, the guest is a client and its effect is relayed)
    free_games(host, guest, 8)
    ok = mating_call(guest, host, gid, hid, ck, 'guest mating call')
    ck(ok, 'the guest\'s mating call reaches the host and is played on the guest\'s puppet')

    ck.faults(host=host, guest=guest)
    raise SystemExit(ck.result())


if __name__ == '__main__':
    main()
