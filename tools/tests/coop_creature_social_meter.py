#!/usr/bin/env python3
"""A guest's social game looks like the host's: the game's social meter on the guest's screen and the game's
success message (BUG-0179, players: "the social game of the guest is simplified").

The native meter is one UI object of the creature mode (docs/re/creature.md#creature-guest-meter); a guest's
social game with a mirror never opened it (the NPC's behaviour tree, which opens it, runs on the host) and
the win's message 0x60B4123 (the creature-stage handler rolls the species' part reward on it) was never posted
on the guest. Real input on the guest: the social stance, a click on a Kook, clicks on the sing button.
Checked on the guest's game: the meter object is opened with the Kook's rounds when the Kook is engaged, the
meter's layout is visible when the host's win arrives, hidden after it, and the creature-stage handler of the guest's game got 0x60B4123.
social.guest_meter off failed (retired in 0.7.15, BUG-1138: FAIL run 20261103-234903:creature_social_meter).

--meter-null N (BUG-1058): the guest's first N meter tasks find no meter window (`coop.social meter_null N`, as an
engagement in a frame the creature HUD is not up yet); the meter must still open for the engaged Kook.
SPORE_FIX_OFF=social.meter_retry with --meter-null: never opened for that game (FAIL).

BUG-1515 (CR-13): the finished meter stays up for a moment after the win (the game's own visible byte of the meter
window, read by the guest 0.5 s after the win's Finish, `visible_after_win=1`); the key frame
meter_finish shows the full arc with the face. SPORE_FIX_OFF=social.meter_hold: hidden the frame after
the win (`visible_after_win=0`, FAIL, twin creature_social_meter_hold_off)."""
import re
import sys
import time

from coop_creature_social import KOOK, num, social
from coop_creature_social_ally import engage, guest_list, host_list, pos
from twoinst import Checks, keyframe, shot, start

SING = (550, 1020)  # the social row's first button (1440x1080 test window)


def meter(con):
    return con.call('coop.social meter')['out']


def main():
    host, guest = start('creature')
    ck = Checks()
    if '--meter-null' in sys.argv:  # BUG-1058
        print('  guest:', guest.call(f'coop.social meter_null {sys.argv[sys.argv.index("--meter-null") + 1]}')['out'].strip(), flush=True)
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
    m0 = meter(guest)
    print(f'  guest before: {m0.strip()}', flush=True)
    w0 = num(r'stage_win_msgs=(\d+)', m0, 0)
    o0 = num(r'opened=(\d+)', m0, 0)
    done = opened = first = finish = None
    for nid in kooks[:3]:
        if not engage(guest, nid, 40):
            print(f'  #{nid}: not engaged', flush=True)
            continue
        time.sleep(1)
        m1 = meter(guest)
        print(f'  #{nid} engaged: {m1.strip()}', flush=True)
        if '--meter-null' in sys.argv and first is None:  # BUG-1058: the first engaged Kook's meter, after a missed open
            t1 = time.time()
            while time.time() - t1 < 2 and num(r' visible=(-?\d+)', m1, 0) != 1:
                time.sleep(0.25)
                m1 = meter(guest)
            first = m1
            ck(num(r'missed=(\d+)', m1, 0) >= 1,  # counter: the test hook took effect (the scenario's precondition)
               f"the guest's first meter task found no meter window ({m1.strip()[-30:]})")
            ck(num(r' visible=(-?\d+)', m1, 0) == 1 and num(r'rounds=(\d+)', m1, 0) >= 1,
               f"the game's meter is up on the guest's screen for the first engaged Kook #{nid} after the missed try")
        if num(r'opened=(\d+)', m1, 0) > o0:
            opened = m1
            shot(guest, 'social_meter_guest_open.png')
        for _ in range(4):
            guest.call(f'move {SING[0]} {SING[1]}')
            time.sleep(0.3)
            guest.call(f'click {SING[0]} {SING[1]}')
            t1 = time.time()
            while time.time() - t1 < 3:  # BUG-1515: the frame of the finish, while the meter shows it
                time.sleep(0.2)
                if finish is None and num(r'stage_win_msgs=(\d+)', meter(guest), 0) > w0:
                    finish = keyframe('meter_finish', "the guest's screen: the social meter's full arc with the "
                                      "relation face, the finish of the won game (not hidden yet)", {'guest': guest}, settle=0)
            if host_list(host).get(nid, (0, 0, 0, False))[3]:
                break
        print(f'  #{nid}: guest {social(guest).strip()}', flush=True)
        if host_list(host).get(nid, (0, 0, 0, False))[3]:
            done = nid
            break
    ck(done is not None, f'the guest impressed a Kook on the host\'s game (#{done})')
    ck(opened is not None, "the guest's game opened its social meter for the engaged Kook (%s)" % (opened or meter(guest)).strip())
    if opened:
        ck(num(r'rounds=(\d+)', opened, 0) >= 1, 'the meter shows the Kook\'s rounds')
    if done is not None:
        time.sleep(2)
        m2 = meter(guest)
        print(f'  guest after: {m2.strip()}', flush=True)
        shot(guest, 'social_meter_guest_won.png')
        ck(num(r'visible_at_win=(-?\d+)', m2, 0) == 1, f"the game's meter was up on the guest's screen when the win came ({m2.strip()})")
        ck(num(r'visible_after_win=(-?\d+)', m2, -1) == 1,  # BUG-1515
           f"the game's meter still showed its finish 0.5 s after the win ({m2.strip()[-24:]})")
        ck(num(r'stage_win_msgs=(\d+)', m2, 0) > w0,
           "the guest's creature-stage handler got the game's social win message 0x60B4123")
        ck(num(r'open_for=(-?\d+)', m2, 0) < 0 and num(r' visible=(-?\d+)', m2, 1) == 0, "the game's meter is hidden after the result")
        ck(guest_list(guest).get(done, (False, False))[1], f"the guest's game shows Kook #{done} impressed")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
