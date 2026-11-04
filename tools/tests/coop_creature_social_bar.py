#!/usr/bin/env python3
"""A guest befriends a creature with the MOUSE: a click on the social ability's button of the ability bar
(BUG-0179, two player groups: "the creatures do not want to be friends" with the guest).

The guest's game uses a bar ability, by a key or a click on its button, through AbilityBar_Use 0xD49560
(docs/re/creature.md#creature-guest-social-bar). The mod polled only the keys 1-4: a click on the button
next to an engaged mirror did nothing (no animation, nothing sent to the host).

Real input on the guest: the social stance, a click on a Kook (the game's social stimulus engages the
mirror), then a click on the social ability's button (sing: the first slot of the green row, 1440x1080:
550,1020), never a key. Checked on both games (the game's own state): the host's Kook has its impressed
flag, the guest's proxy of it too, and the species table counts one more impressed Kook on both.
SPORE_FIX_OFF=social.bar_click must FAIL (nothing reaches the host)."""
import re
import time

from coop_creature_social import KOOK, num, social, table
from coop_creature_social_ally import engage, guest_list, host_list, pos, stance
from twoinst import Checks, shot, start

SING = (550, 1020)  # the social row's first button (1440x1080 test window)


def main():
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team hp 1000')
    time.sleep(2)
    gl = guest_list(guest)
    kooks = [i for i, v in host_list(host).items() if v[0] == KOOK and not v[2] and v[1] > 0 and gl.get(i, (False,))[0]]
    ck(kooks, f'the guest mirrors Kooks ({kooks})')
    if not kooks:
        ck.faults(host=host, guest=guest)
        return ck.result()
    host.call(f'coop.npc gonpc {kooks[0]} 10')
    time.sleep(1)
    hp = pos(host)
    guest.call('coop.npc gopos %.2f %.2f %.2f' % (hp[0] + 2, hp[1], hp[2]))
    time.sleep(3)
    imp0 = table(host).get(KOOK, (0, 0, 0))[1]
    done = None
    for nid in kooks[:3]:
        if not engage(guest, nid, 40):
            print(f'  #{nid}: not engaged', flush=True)
            continue
        a0 = num(r'acts_sent=(\d+)', social(guest), 0)
        for _ in range(4):
            guest.call(f'move {SING[0]} {SING[1]}')
            time.sleep(0.3)
            guest.call(f'click {SING[0]} {SING[1]}')  # the mouse, as a player: no key
            time.sleep(3)
            if host_list(host).get(nid, (0, 0, 0, False))[3]:
                break
        g = social(guest)
        print(f'  #{nid}: stance {stance(guest)}, guest {g.strip()}', flush=True)
        print(f'  #{nid}: host {social(host).strip()}', flush=True)
        if host_list(host).get(nid, (0, 0, 0, False))[3]:
            done = nid
            break
        if num(r'acts_sent=(\d+)', g, 0) == a0:
            print(f'  #{nid}: the click sent nothing', flush=True)
    shot(guest, 'social_bar_guest.png')
    ck(done is not None, f'a click on the social ability button next to an engaged Kook impressed it on the host\'s game (#{done})')
    if done is not None:
        time.sleep(2)
        ck(guest_list(guest).get(done, (False, False))[1], f"the guest's game shows Kook #{done} impressed")
        ht, gt = table(host), table(guest)
        ck(ht.get(KOOK, (0, 0, 0))[1] == imp0 + 1, f'the host\'s table counts one more impressed Kook ({imp0} -> {ht.get(KOOK)})')
        ck(gt.get(KOOK) == ht.get(KOOK), f'the guest\'s table says the same ({gt.get(KOOK)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
