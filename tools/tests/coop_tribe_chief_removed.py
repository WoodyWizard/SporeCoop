#!/usr/bin/env python3
"""Tribe: a chief that leaves the tribe in one step (BUG-0081).

A chief that stops being one gets its own size back through a virtual call on the citizen. Before
the fix the only check was object_alive, which reads the citizen's own memory: a chief removed
without a frame where it was still a member (a guest mirror removed by a snapshot, a load) was
called through freed memory.

1. Both players have a chief.
2. The host adds adults (`coop.tribe spawn 4`) and removes the chief of a player who has another
   adult from the tribe at once (`coop.tribe drop_chief <owner>`, the
   game's RemoveMember(c, destroy) as guest_state does for a mirror); the guest's next full
   snapshot removes its mirror the same way.
3. Both games keep running without a fault, neither called into a citizen that is no longer a
   member (`chief_bad_calls=0`), and that player has a new chief with 1.3x its size in the host's game
   (spatial GetScale / its own scale) and, on the guest's screen, the size it has in the host's game (BUG-0911:
   the guest's own ratio is no measure, its base is its own game's pick while the size comes from the host).
4. Forced address reuse: another tribe's member is put where a former chief was (`coop.tribe
   stale_chief <id>`); it keeps its own size in the game (before the fix: the old chief's size).

--nofix: the old check (`coop.tribe fix chiefs 0`): must FAIL.

  SPORE_LANE=5 python3 tools/tests/coop_tribe_chief_removed.py [--nofix]
"""
import argparse
import re
import time

from coop_tribe import members
from twoinst import Checks, start


def line(con, key):
    m = re.search(key + r'.*', con.call('coop.status')['out'])
    return m.group(0) if m else ''


def chiefs(con):
    m = re.search(r'chiefs=(.*)', line(con, 'tribe co-op:'))
    return dict(re.findall(r'(\d+):#(\d+)', m.group(1))) if m else {}


def lifetime(con):
    out = line(con, 'tribe lifetime:')
    vals = {k: int(v) for k, v in re.findall(r'(\w+)=(\d+) ', out + ' ')}
    scales = dict(re.findall(r' (\d+):([\d.]+)', out.split('chief_scale=')[-1])) if 'chief_scale=' in out else {}
    return vals, {k: float(v) for k, v in scales.items()}, out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--nofix', action='store_true')
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    if a.nofix:
        for c in (host, guest):
            print(' ', c.call('coop.tribe fix chiefs 0')['out'].strip())
    t0 = time.time()
    while time.time() - t0 < 90:  # the split of the player tribe arrives with the first states
        if any(v[0] == 1 for v in members(host).values()) and len(chiefs(host)) >= 2 and len(chiefs(guest)) >= 2:
            break
        time.sleep(2)
    time.sleep(2)
    hc, gc = chiefs(host), chiefs(guest)
    print('  chiefs host', hc, 'guest', gc)
    ck('1' in hc and hc == gc, 'both players have a chief, the same on both machines')
    if '1' not in hc:
        return ck.result()
    # an owner with a second adult, who becomes the new chief (the fixture's guest may have one only)
    print(' ', host.call('coop.tribe spawn 4')['out'].strip())
    time.sleep(6)
    adults = {}
    for ln in host.call('coop.tribe')['out'].splitlines():
        m = re.match(r'\s+#(\d+) \S+ owner=(\d+) age=1 ', ln)
        if m and m.group(1) != '0':
            adults.setdefault(m.group(2), []).append(int(m.group(1)))
    hc, gc = chiefs(host), chiefs(guest)
    who = next((o for o in ('1', '0') if len(adults.get(o, [])) >= 2 and o in hc), None)
    print('  adults by owner', adults, 'chiefs', hc, '-> drop the chief of', who)
    ck(who is not None, 'an owner has a chief and a second adult')
    if who is None:
        return ck.result()
    print(' ', host.call(f'coop.tribe drop_chief {who}')['out'].strip())
    time.sleep(8)  # the guest's next full snapshots, a new chief on both
    for name, c in (('host', host), ('guest', guest)):
        vals, scales, out = lifetime(c)
        print(f'  {name}: {out}')
        ck(vals.get('chief_bad_calls', 1) == 0, f'{name}: no call into a citizen that left the tribe')
    hc2, gc2 = chiefs(host), chiefs(guest)
    print('  chiefs now host', hc2, 'guest', gc2)
    ck(who in hc2 and hc2.get(who) != hc.get(who) and hc2 == gc2, f'player {who} has a new chief, the same on both machines')
    _, scales, _ = lifetime(host)
    s = scales.get(who, 0)
    ck(abs(s - 1.3) < 0.05, f'host: the new chief is 1.3x its size in the game ({s:.2f})')
    # BUG-0911: on the guest the chief's size is the HOST's member's (the members' scale field, BUG-0475), not 1.3x
    # of the size the guest's own game would give its mirror: the guest's ratio against its own base read 1.58 / 1.20
    # while the size on its screen was right. What the guest shows is compared with the host's game.

    def size(c, i):
        m = re.search(r'scale ([\d.]+)', c.call(f'coop.tribe scale {i}')['out'])
        return float(m.group(1)) if m else -1.0

    for what, i in (('the new chief', hc2.get(who)), ('the other player\'s chief', hc2.get('0' if who == '1' else '1'))):
        if i is None:
            continue
        hs = gs = -1.0
        t1 = time.time()
        while time.time() - t1 < 10:
            hs, gs = size(host, i), size(guest, i)
            if hs > 0 and abs(gs - hs) <= 0.03 * hs:
                break
            time.sleep(1)
        ck(hs > 0 and abs(gs - hs) <= 0.03 * hs,
           f'guest: {what} #{i} has the size it has in the host\'s game (guest {gs:.3f}, host {hs:.3f})')
    # the freed block of a former chief reused by another citizen (forced: `coop.tribe stale_chief`
    # puts another tribe's member where the old chief was): it must keep its own size
    other = next(iter(adults.get('254', [])), None)
    if ck(other is not None, 'another tribe has a member'):
        def scale():
            m = re.search(r'scale ([\d.]+)', host.call(f'coop.tribe scale {other}')['out'])
            return float(m.group(1)) if m else -1
        s0 = scale()
        print(' ', host.call(f'coop.tribe stale_chief {other}')['out'].strip())
        time.sleep(2)
        s1 = scale()
        vals, _, out = lifetime(host)
        print(f'  #{other} scale {s0:.2f} -> {s1:.2f}; {out}')
        ck(s0 > 0 and abs(s1 - s0) < 0.01 and vals.get('chief_bad_calls', 1) == 0,
           'a citizen at a former chief\'s address keeps its own size (no call into it)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
