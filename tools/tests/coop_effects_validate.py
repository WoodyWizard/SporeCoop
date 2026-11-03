#!/usr/bin/env python3
"""BUG-0451 review items 2 and 3: a received effect message is validated before any game call.

Spoofing: the host accepts a guest's message only when its `player` byte is the sender; a guest
accepts only the host's relay (from 0). Otherwise the message would choose whose puppet plays the
effect. Validation: a start must have an effect id, a pool the sender uses (1, 2, 3, 5), a pool 3
bone index inside the puppet model's bone count, and a pool 5 transform with finite floats and a
position within 1e5. A stop has pool 0 and a key. The drops are counted in coop.effects.

The messages are fed through the same on_message the network uses (`coop.effects inject`; a guest
cannot be made to claim another player over the transport). A valid message still plays, so the
rejections are not a blanket drop.

  coop_effects_validate.py
"""
import re
import time

from twoinst import Checks, start


def busy(con):
    for line in con.call('coop.status')['out'].splitlines():
        if line.startswith('busy:'):
            return line.split(':', 1)[1].strip()
    return '?'


def free_games(h, g, seconds=30):
    t0 = time.time()
    while time.time() - t0 < seconds:
        if busy(h) == '-' and busy(g) == '-':
            return True
        for c in (h, g):
            c.call('key enter')
            c.call('click 1418 1044')
        time.sleep(1)
    return busy(h) == '-' and busy(g) == '-'


KEYS = ('fix', 'sent', 'received', 'played', 'stopped', 'skipped', 'failed', 'dropped', 'world')


def nums(con):
    out = con.call('coop.effects')['out']
    m = re.search(r'effects: fix=(\d) sent=(\d+) received=(\d+) played=(\d+) stopped=(\d+) skipped=(\d+) '
                  r'failed=(\d+) dropped_rate=(\d+) world=(\d+)', out)
    d = {k: int(v) for k, v in zip(KEYS, m.groups())} if m else {}
    m = re.search(r'effects received drops: spoof=(\d+)', out)
    if m:
        d['spoof'] = int(m.group(1))
    m = re.search(r'pool=(\d+) id=(\d+) bone=(\d+) transform=(\d+) short=(\d+)', out)
    if m:
        for k, v in zip(('bad_pool', 'bad_id', 'bad_bone', 'bad_xf', 'bad_short'), m.groups()):
            d[k] = int(v)
    return d


def last_rx(con, who):
    m = re.search(r'effects received (?:start|last) from (\d+): id=([0-9A-F]{8}) key=([0-9A-F]{8}) pool=(\d+)',
                  con.call('coop.effects')['out'])
    return {'id': m.group(2), 'pool': int(m.group(4))} if m and int(m.group(1)) == who else None


def inject(con, *a):
    return con.call('coop.effects inject ' + ' '.join(str(x) for x in a))['out']


def case(ck, con, what, args, key, delta=1, rx_from=None):
    before = nums(con)
    inject(con, *args)
    after = nums(con)
    got = after.get(key, 0) - before.get(key, 0)
    ck(got == delta, f'{what}: {key} {got:+d} (expected {delta:+d})')
    if key.startswith('bad_'):
        ck(after.get('received', 0) == before.get('received', 0), f'{what}: it is not received')
        ck(after.get('played', 0) == before.get('played', 0), f'{what}: it is not played')
    if rx_from is not None:
        rx = last_rx(con, rx_from)
        ck(rx is not None and rx['pool'] in (1, 2, 3, 5), f'{what}: the valid message plays on the puppet')


def main():
    h, g = start('creature')
    ck = Checks()
    ck(g.call('coop.avatar')['ok'], 'the guest is in the host\'s world')
    ck(free_games(h, g), f'both games take input (busy: {busy(h)} / {busy(g)})')
    time.sleep(2)
    n = nums(h)
    ck('spoof' in n and n.get('bad_pool') is not None, f'coop.effects has the drop counters ({n})')
    # ---- item 2: whose message is it ----
    case(ck, h, 'host: a guest names another player', (1, 2, 0, 3, 'E1A4BD93', 0), 'spoof')
    case(ck, h, 'host: a guest names itself (valid)', (1, 1, 0, 3, 'E1A4BD93', 0), 'received', 1, rx_from=1)
    ck(nums(h).get('played', 0) >= 1, 'the valid guest message was played on the guest\'s puppet')
    case(ck, g, 'guest: a message from a guest is not relayed to it', (1, 1, 0, 3, 'E1A4BD93', 0), 'spoof')
    case(ck, g, 'guest: from the host (valid)', (0, 0, 0, 3, 'E1A4BD93', 0), 'received', 1, rx_from=0)
    # ---- item 3: the header and the payload ----
    case(ck, h, 'pool 9 is not used', (1, 1, 0, 9, 'E1A4BD93', 0), 'bad_pool')
    case(ck, h, 'an effect id 0', (1, 1, 0, 3, '00000000', 0), 'bad_id')
    case(ck, h, 'a bone outside the model', (1, 1, 0, 3, 'E1A4BD93', 100000), 'bad_bone')
    case(ck, h, 'a negative bone', (1, 1, 0, 3, 'E1A4BD93', '0xFFFFFFFF'), 'bad_bone')
    case(ck, h, 'a valid bone', (1, 1, 0, 3, 'E1A4BD93', 0), 'received', 1)
    case(ck, h, 'a world position 1e6 out', (1, 1, 0, 5, 'E1B24DDB', 0, 1000000, 0, 0), 'bad_xf')
    case(ck, h, 'a NaN transform', (1, 1, 0, 5, 'E1B24DDB', 0, 'nan', 0, 0), 'bad_xf')
    case(ck, h, 'a valid world effect', (1, 1, 0, 5, 'E1B24DDB', 0, -170, -467, -85), 'received', 1)
    case(ck, h, 'a stop without a key', (1, 1, 1, 0, 0, 0), 'bad_id')
    case(ck, h, 'a stop with a key (valid, nothing to stop)', (1, 1, 1, 0, 0, '0xABCD1234'), 'received', 1)
    ck.faults(host=h, guest=g)
    raise SystemExit(ck.result())


if __name__ == '__main__':
    main()
