#!/usr/bin/env python3
"""BUG-1252 (players' video): a thought bubble with a gnawed bone hung over the guest's creature in the host's game
for a long time.

Measured first (lab 20261118-123004): the host's puppet has no hunger of its own (its meter's change +0xBB8 is 0, its
mood +0xCC0 is never computed), so the bubble on it is the guest's OWN hunger bubble: the guest's game shows it over
its own creature when its meter is low (mood 2, pool 2 key 062EDBA6, Creature_ThoughtBubbleUpdate 0xE8BF00) and the
effects relay starts and stops it on the puppet (docs/re/creature.md#creature-puppet-hunger). The bubble stays on the
puppet only when the stop is lost: the relay's rate limit (40 a second) dropped a stop like a start, and the stop's
key had already left the list of sent keys, so it was never sent again.
Creature fixture, the guest 4 m in front of the host's camera:
  1. the host's puppet's meter set low (`coop.puppet hunger 10`): no bubble (its own meter shows none);
  2. the guest's own creature gets hungry (`coop.puppet ownhunger 10`, its own game's meter as minutes leave it): the
     bubble on both games;
  3. a burst of effects at the guest's creature (`coop.effects load 40 50`, as a fight's hits make them) while the guest
     eats (`ownhunger 100`): its own bubble goes; the burst ends;
  4. 10 s later the host's puppet must have no bubble either (`coop.effects map`, active); key frames.
SPORE_FIX_OFF=effects.stop_always must FAIL (the bubble stays on the puppet)."""
import re
import time

from coop_puppet_after_social import cur, pools
from harness import avatar_pos as pos
from twoinst import Checks, keyframe, shot, start

BUBBLE = '062EDBA6'  # the hunger bubble's pool key (0xE8BF00)


def hunger(con, who):
    m = re.search(rf'^cur {who} .*hunger=([-\d.]+) hunger_delta=([-\d.]+) mood=(-?\d+)', con.call('coop.npc cur')['out'], re.M)
    return (float(m.group(1)), float(m.group(2)), int(m.group(3))) if m else (None, None, None)


def watch(host, guest, gid, secs, label, rows):
    t0 = time.time()
    while time.time() - t0 < secs:
        hp_, gp = pools(host, gid), pools(guest, 'avatar')
        rows.append((label, round(time.time() - t0, 1), hunger(host, 'p' + gid), hp_.get(BUBBLE), hunger(guest, 'avatar'),
                     gp.get(BUBBLE)))
        time.sleep(0.5)


def main():
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team hp 1000')
    gid = re.search(r'client id=(\d+)', guest.call('coop.status')['out'])
    gid = gid.group(1) if gid else '1'
    time.sleep(3)
    hp = pos(host)
    guest.call('coop.npc gopos %.2f %.2f %.2f' % (hp[0] + 4, hp[1], hp[2]))
    time.sleep(4)
    rows = []
    watch(host, guest, gid, 3, 'start', rows)
    # 1. the puppet's own meter in the host's game, as minutes of the game's update would leave it
    print('  host: ' + host.call('coop.puppet hunger 10')['out'].strip(), flush=True)
    watch(host, guest, gid, 8, 'pup10', rows)
    keyframe('puppet_low', 'the host\'s screen, the guest\'s creature 4 m ahead: NO thought bubble over it (the guest\'s '
             'own creature is not hungry)', settle=0)
    # 2. the guest's own creature gets hungry: its own bubble, relayed
    print('  guest: ' + guest.call('coop.puppet ownhunger 10')['out'].strip(), flush=True)
    watch(host, guest, gid, 8, 'own10', rows)
    keyframe('guest_hungry', 'the guest\'s own creature is hungry: the thought bubble over it on both screens', settle=0)
    # 3. it eats during a burst of effects at it
    print('  guest: ' + guest.call('coop.effects load 40 50')['out'].strip(), flush=True)
    time.sleep(1.5)
    print('  guest: ' + guest.call('coop.puppet ownhunger 100')['out'].strip(), flush=True)
    watch(host, guest, gid, 3, 'burst', rows)
    guest.call('coop.effects load 0')
    # 4. later
    watch(host, guest, gid, 10, 'after', rows)
    keyframe('guest_fed', 'the guest\'s own creature ate (full): NO thought bubble over it on either screen', settle=0)
    for r in rows:
        print(f'    {r[0]:6} {r[1]:5.1f} host puppet {r[2]} bubble {r[3]} | guest avatar {r[4]} bubble {r[5]}', flush=True)
    m = re.search(r'remote animations.*', host.call('coop.status')['out'])
    print('  host: ' + (m.group(0) if m else '?'), flush=True)
    print('  host: ' + host.call('coop.effects')['out'].strip().splitlines()[0], flush=True)
    print('  guest: ' + guest.call('coop.effects')['out'].strip().splitlines()[0], flush=True)
    pup = [(r[0], r[1]) for r in rows if r[0] == 'pup10' and r[1] > 3 and r[3] and not r[5]]
    ck(not pup, f'the host\'s puppet shows no bubble of its own meter ({pup[:3]})')
    both = [r for r in rows if r[0] == 'own10' and r[1] > 3]
    ck(both and any(r[3] for r in both) and any(r[5] for r in both),
       'the guest\'s own hunger bubble is on its creature in both games (the relay starts it)')
    stays = [(r[0], r[1]) for r in rows if r[0] == 'after' and r[1] > 4 and r[3] and not r[5]]
    ck(not stays, f'after the guest ate the host\'s puppet has no bubble either ({len(stays)} samples: {stays[:3]})')
    shot(host, 'puppet_hunger_host.png')
    shot(guest, 'puppet_hunger_guest.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
