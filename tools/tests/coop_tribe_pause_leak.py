#!/usr/bin/env python3
"""Tribe: a guest's game time left standing by a caption's pause that nothing holds goes on again (BUG-1101).

campaign_tribe_noclick of the 0.7.14 final check: two guests' game time stood (`busy=paused`, no cinematic, no input
lock) from a village caption's forced end (BUG-0983) to the end of Tribe (30 / 16 min). The game's way to leave such a
pause counted (docs/re/tribe.md#tribe-caption-pause-leak): the cinematic manager's own pause (+0x160, a cinematic
script's "pause" step, message 0x46AC0AC) taken while no cinematic plays is never lifted, and the next cinematic's
start clears the slot without a Resume.

1. The guest's game plays TRG_TotemPole_Conquer (`coop.tribe cine`); its forced end after 8 s (BUG-0983) is followed
   by the script's pause step (`coop.tribe caption_race 46AC0AC send after`): the pause 0x4BF38A5 left with no
   cinematic. A second caption plays and ends: its start cleared the slot, the count stays (as Carol's next conquest).
   The guest's game time must run again within 35 s. SPORE_FIX_OFF=tribe.guest_pause_net: it stands: FAIL.
2. A raw leak of the UI-modal reason 0x4BF38A7 (`coop.pauses add`) is released too; its owner's late Resume
   (`coop.pauses sub`) does not leave the count under 0 (it would swallow the reason's next pause).
3. The player's own pause (P, reason 0x4BF38A8) is never released: still standing after 30 s, runs after P again.
"""
import re
import time

from twoinst import Checks, keyframe, start


def busy_of(con):
    m = re.search(r'^busy: (\S+)', con.call('coop.status')['out'], re.M)
    return m.group(1) if m else '-'


def pauses(con):
    return con.call('coop.pauses')['out'].strip()


def wait_cine_end(con, t=20):
    t0 = time.time()
    while time.time() - t0 < 3 and 'cinematic' not in busy_of(con):
        time.sleep(0.2)
    while time.time() - t0 < t and 'cinematic' in busy_of(con):
        time.sleep(0.5)


def runs_within(con, t):
    """seconds until the game time runs (None: still standing after t s)"""
    t0 = time.time()
    while time.time() - t0 < t:
        if 'paused' not in busy_of(con):
            return time.time() - t0
        time.sleep(1)
    return None


def main():
    host, guest = start('tribe')
    ck = Checks()
    print('  guest: ' + guest.call('coop.pauses trace')['out'].strip(), flush=True)
    if not ck(runs_within(guest, 60) is not None, f"the guest's game time runs at the start ({pauses(guest)})"):
        return ck.result()
    time.sleep(3)

    # 1. the caption's own pause left counted after its end
    print('  ' + guest.call('coop.tribe caption_race 46AC0AC send after')['out'], flush=True)
    print('  ' + guest.call('coop.tribe cine TRG_TotemPole_Conquer')['out'], flush=True)
    wait_cine_end(guest)
    time.sleep(1)
    left = pauses(guest)
    print(f'  after the forced end: busy {busy_of(guest)} | {left}', flush=True)
    ck('04BF38A5=1' in left, f"the caption's pause is left counted after its end (the planted leak: {left})")
    guest.call('coop.tribe cine TRG_TotemPole_Conquer')  # the next caption: its start clears the slot, not the count
    wait_cine_end(guest)
    print(f'  after the next caption: busy {busy_of(guest)} | {pauses(guest)}', flush=True)
    took = runs_within(guest, 35)
    print(f'  the guest\'s game time runs {took and round(took)} s after the next caption | {pauses(guest)} | '
          + next((ln for ln in guest.call('coop.status')['out'].splitlines() if 'pause net' in ln), ''), flush=True)
    ck(took is not None, f"the guest's game time runs again after a caption's pause was left counted ({busy_of(guest)}, "
                         f"{pauses(guest)})")
    keyframe('released', "the guest's Tribe running again after the leaked caption pause (members walk, no caption)",
             players={'guest': guest})

    # 2. a leaked UI-modal pause; its owner's Resume afterwards
    guest.call('coop.pauses add 4BF38A7')
    took = runs_within(guest, 30)
    ck(took is not None and took > 15, f"a leaked 0x4BF38A7 is released after the net's 20 s ({took and round(took)} s)")
    guest.call('coop.pauses sub 4BF38A7')  # the owner's own Resume, late
    time.sleep(3)
    p = pauses(guest)
    ck('04BF38A7' not in p, f'the late Resume of the owner leaves no count under 0 ({p})')
    guest.call('coop.pauses add 4BF38A7')  # the reason's next pause stands the game again
    time.sleep(1.5)
    ck('paused' in busy_of(guest), f"the reason's next pause stands the game again ({busy_of(guest)}, {pauses(guest)})")
    guest.call('coop.pauses sub 4BF38A7')
    time.sleep(1.5)

    # 3. the player's own pause stays
    guest.call('key p')
    time.sleep(30)
    b, p = busy_of(guest), pauses(guest)
    ck('paused' in b and '04BF38A8=1' in p, f"the guest's own pause (P) is not released after 30 s ({b}, {p})")
    guest.call('key p')
    took = runs_within(guest, 10)
    ck(took is not None, f"the guest's game runs after P again ({busy_of(guest)}, {pauses(guest)})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
