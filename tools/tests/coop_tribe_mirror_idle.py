#!/usr/bin/env python3
"""Tribe: the guest's mirrors stand, sit and swing their staff as the host's members do (BUG-0936, BUG-0914).

The 0.7.12 final check's same-view key frames: members at rest at the village stood on one game and sat or crouched
at the fire on the other (BUG-0936), and the host's chief swung its staff while the guest's copy held it upright in
the resting grip (BUG-0914). A mirror plays the behaviour animations the host relays (tribe.mirror_anims) and keeps
each until it ends: the host's member, whose behaviour tree cut it short, already stood or walked in its own idle
(probe, lane 4: the host in its idle choice, the guest's mirror the same or in its own idle / walk in 30 % of the
samples). And the staff is held in the hand only while the animation it was taken in plays (the hold modifier,
docs/re/tribe.md#tribe-held-item): the mirror played the animation but never took the item again.

Two games on the tribe fixture, 4 more members, 120 s of play, both games' current animation of every member twice a
second (`coop.tribe anims`: the game's current main animation, the host's last behaviour and idle-refresh plays,
whether the hand holds its item, `hold=`):
--part idle (BUG-0936): samples where the host's member stands or walks in its own idle choice (the same animation in
   two samples in a row, not just started): the guest's mirror must play that or its own idle / walk (the locomotion
   animations), not a behaviour animation, in 80 % of them (62 % without the fix, 96-99 % with it).
--part hold (BUG-0914): the natural holds are printed (each hold of the host's member in an animation and whether the
   mirror held its item in that animation during it or 0.6 s after: the mirror's own game takes the item itself in
   some animations, so the rate is no measure); then the host's chief takes its staff into the hand in the animation
   it plays (`coop.tribe hand <id> <item>`, the game's attach: the hold modifier bound to that animation, what its
   behaviour nodes do in a swing) three times and puts it back (`hand <id> end`): the guest's copy must hold the staff
   in the hand 1.5 s after each take and have it back in the grip 1.5 s after each end.
Key frames of kind sameview: the village's fire with the members at rest (idle), the host's chief with its staff
(hold, shot while the host's chief holds the staff in an animation).

SPORE_FIX_OFF=tribe.mirror_idle fails --part idle; SPORE_FIX_OFF=tribe.mirror_hold fails --part hold.

  SPORE_LANE=4 python3 tools/tests/coop_tribe_mirror_idle.py --part idle
"""
import argparse
import re
import time

from coop_tribe_looks import view_both
from play_tribe import tribe_centres
from twoinst import Checks, game_runs, keyframe, start

ROW = re.compile(r'cur (\d+) anim=([0-9A-F]{8}) bhv=([0-9A-F]{8}) pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\) idle=([0-9A-F]{8}) '
                 r'hold=(\d)')
LOCO = {'02481DE5', '04866D8E', '04866DAB', '04866DD4'}  # the locomotion's idle / walk (docs/re/creature.md)


def anims(con):
    return {int(m.group(1)): dict(anim=m.group(2), bhv=m.group(3), pos=tuple(float(m.group(i)) for i in (4, 5, 6)),
                                  idle=m.group(7), hold=m.group(8) == '1')
            for m in ROW.finditer(con.call('coop.tribe anims')['out'])}


def leader(con):
    """the player tribe's leader (the host's chief)"""
    player = False
    for line in con.call('coop.tribe')['out'].splitlines():
        if line.startswith('tribe '):
            player = '(player)' in line
        m = re.match(r'\s+#(\d+) .*leader=1', line)
        if m and player and m.group(1) != '0':
            return int(m.group(1))
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--part', choices=('idle', 'hold'), default='idle')
    ap.add_argument('--secs', type=float, default=120)
    a = ap.parse_args()
    host, guest = start('tribe')
    ck = Checks()
    game_runs(guest, 90)
    print(' ', host.call('coop.tribe spawn 4')['out'].strip()[:80], flush=True)
    time.sleep(5)
    idle_n = idle_ok = 0
    prev, stale = {}, {}
    eps, done = {}, []  # BUG-0914: the host's holds
    t0 = time.time()
    while time.time() - t0 < a.secs:
        h, g = anims(host), anims(guest)
        for i, x in h.items():
            if i not in g:
                continue
            y = g[i]
            if x['anim'] != x['bhv'] and (x['anim'] == x['idle'] or x['anim'] in LOCO) and prev.get(i) == x['anim']:
                idle_n += 1
                if y['anim'] == x['anim'] or y['anim'] in LOCO:
                    idle_ok += 1
                else:
                    stale[(x['anim'], y['anim'])] = stale.get((x['anim'], y['anim']), 0) + 1
            # a hold of the host's member in an animation (an episode: hold=1 in the same animation), and whether the
            # mirror holds its item in that animation during it or in the 0.6 s after (the relay's delay)
            ep = eps.get(i)
            if x['hold'] and x['anim'] not in LOCO and not (ep and ep['anim'] == x['anim'] and ep['open']):
                if ep:
                    done.append(ep)
                ep = eps[i] = dict(anim=x['anim'], open=True, after=0, hit=False, id=i)
            elif ep and ep['open'] and (not x['hold'] or x['anim'] != ep['anim']):
                ep['open'] = False
            if ep:
                ep['hit'] = ep['hit'] or (y['hold'] and y['anim'] == ep['anim'])
                if not ep['open']:
                    ep['after'] += 1
                    if ep['after'] > 3:
                        done.append(eps.pop(i))
        prev = {i: x['anim'] for i, x in h.items()}
        time.sleep(0.5)
    print(f'  idle: {idle_ok} of {idle_n} samples the mirror stands or walks as the host\'s member '
          f'(else: {sorted(stale.items(), key=lambda kv: -kv[1])[:8]})', flush=True)
    hold_n, hold_ok = len(done), sum(1 for e in done if e['hit'])
    print(f'  hold: the mirror held its item in {hold_ok} of the host\'s {hold_n} holds in an animation', flush=True)
    per = {}
    for e in done:
        k = per.setdefault(e['anim'], [0, 0])
        k[0] += e['hit']
        k[1] += 1
    print('    by animation (held / holds): ' + ', '.join(f'{k} {v[0]}/{v[1]}' for k, v in sorted(per.items(), key=lambda kv: -kv[1][1])),
          flush=True)
    for con, who in ((host, 'host'), (guest, 'guest')):
        out = con.call('coop.tribe anims')['out']
        print(f'  {who}:', ' | '.join(l for l in out.splitlines() if l.startswith(('idle fix', 'hold fix', 'anims fix'))))

    try:
        if a.part == 'idle':
            c = tribe_centres(host).get('p')
            if c and view_both(host, guest, c, 16.0, 5.0):
                keyframe('rest', 'the same camera on both games 16 m from the village\'s centre: the members at rest '
                         'stand, sit or crouch the same way on both screens', kind='sameview')
            rate = idle_ok / idle_n if idle_n else 0.0
            ck(idle_n >= 40, f'the host\'s members stand or walk in their own idle ({idle_n} samples >= 40)')
            ck(rate >= 0.8, f'the guest\'s mirrors do too, not an animation the host\'s member has ended '
                             f'({100 * rate:.0f} % >= 80 %)')
        else:
            lead = leader(host)
            shot = False
            t1 = time.time()
            while lead and time.time() - t1 < 60 and not shot:
                x = anims(host).get(lead)
                if x and not view_both(host, guest, x['pos'], 12.0, 4.0):
                    break
                t2 = time.time()
                while time.time() - t2 < 8 and not shot:
                    x = anims(host).get(lead)
                    if x and x['hold'] and x['anim'] not in LOCO:
                        time.sleep(0.4)
                        keyframe('swing', f'the same camera on both games 12 m from the host\'s chief #{lead} while it '
                                 f'holds its staff in an animation ({x["anim"]}): the staff in the hand, moving the same '
                                 f'way on both screens (not upright in the resting grip on the guest)', kind='sameview')
                        shot = True
                    time.sleep(0.2)
            print(f'  swing key frame: {shot}', flush=True)
            # The natural holds above are short (0.5-2 s) and partly taken by the mirror's own game (the work and the
            # change animations attach at their own events): printed, not asserted. The check: the host's chief takes
            # its staff into the hand in the animation it plays (`coop.tribe hand <id> <item>`: the game's attach, the
            # hold modifier bound to that animation, as its behaviour nodes do) and puts it back (`hand <id> end`).
            rounds, follow, back = 0, 0, 0
            for k in range(12):
                if rounds >= 3:
                    break
                x = anims(host).get(lead)
                hand = host.call(f'coop.tribe hand {lead}')['out']
                item = re.search(r'item=(\d+)', hand)
                if not x or not item:
                    print(f'    try {k}: not now (anim {x and x["anim"]} hold {x and x["hold"]}; {hand.strip()[:90]})', flush=True)
                    time.sleep(3)
                    continue
                if not x['hold']:  # (holding already: its own behaviour took it, e.g. a long gesture)
                    # its item, or the chief's staff (0x11) when its game has put it away (#tribe-held-item)
                    host.call(f'coop.tribe hand {lead} {int(item.group(1)) or 17}')
                    if not int(item.group(1)):
                        time.sleep(1.0)  # (the item field reaches the mirror twice a second)
                time.sleep(1.5)
                x, y = anims(host).get(lead), anims(guest).get(lead)
                if not x or not x['hold']:
                    print(f'    round {k}: the host\'s chief let go at once (anim {x and x["anim"]})', flush=True)
                    host.call(f'coop.tribe hand {lead} end')
                    time.sleep(2)
                    continue
                rounds += 1
                follow += bool(y and y['hold'])
                if rounds == 1 and view_both(host, guest, x['pos'], 10.0, 3.0):
                    keyframe('hold', f'the same camera on both games 10 m from the host\'s chief #{lead} holding its staff '
                             f'in the hand (taken in its animation {x["anim"]}): the staff held forward in the hand on both '
                             f'screens, not upright in the resting grip on the guest', kind='sameview')
                host.call(f'coop.tribe hand {lead} end')
                time.sleep(1.5)
                x2, y2 = anims(host).get(lead), anims(guest).get(lead)
                back += bool(y2 and y2['hold'] == bool(x2 and x2['hold']))  # (the host's game may take it again at once)
                print(f'    round {k}: host anim {x["anim"]} hold 1; guest anim {y and y["anim"]} hold {y and y["hold"]}; '
                      f'after the host\'s end: host hold {x2 and x2["hold"]}, guest hold {y2 and y2["hold"]}', flush=True)
            ck(rounds >= 2, f'the host\'s chief held its staff in its animation ({rounds} rounds >= 2)')
            ck(rounds and follow == rounds, f'the guest\'s copy of the chief held it in the hand too ({follow} of {rounds})')
            ck(rounds and back == rounds, f'and put it back into the grip when the host\'s did, or held it on when the host\'s game '
               f'took it again ({back} of {rounds})')
    finally:
        for con in (host, guest):
            con.call('coop.view off')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
