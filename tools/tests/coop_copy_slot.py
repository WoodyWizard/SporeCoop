#!/usr/bin/env python3
"""A guest's co-op copy of the host's world that the guest played on in alone or as the host (BUG-1434).

A player report (2026-11-22, coop 0.7.15.5): a guest's received world is a copy of the host's whole galaxy, old
planets too, in a co-op slot (Games\\GameN with sporecoop.txt naming the host and the planet). After the first host
left, the players swapped roles: the new host's galaxy menu showed that copy, nothing said so, and it started an old
planet of the first host's galaxy instead of the world they had played. Its saves went into the copy, whose marker
still named the first host: the next share of the first host's world replaced that slot (the old folder deleted) or
tidied it into Games-sporecoop-backup.

--case keep (world.copy_own_save):
1. Creature fixture: the guest received the host's world into a co-op slot S and plays it.
2. The guest leaves the session and saves its game alone (`coop.save`): the game writes the planet file in S.
3. The guest joins again (the world goes into another slot, S is the one it plays; leaving S for the load, its game
   saves S once more) and the host shares once more (`coop.share`: S is no longer played, the old code took it as the
   slot of that host world).
4. PASS: S is still in the guest's Games folder and its planet file is the guest's save of step 3 (the same bytes),
   the guest's game loaded the host's world from another slot. SPORE_FIX_OFF=world.copy_own_save (copy_slot_keep_off): S is
   replaced by the share, the guest's save is gone: FAIL.

--case notice (world.copy_notice):
1. Creature fixture as above. The host leaves the session (the guest plays on in its copy S).
2. The guest hosts a session of its own from S.
3. PASS: a notification on the guest's screen (the game's UI: the co-op notification panel) says that it hosts the
   planet from its copy of Alice's world and names the planet the copy was made for. SPORE_FIX_OFF=world.copy_notice
   (copy_slot_notice_off): nothing is said: FAIL.

  SPORE_LANE=4 python3 tools/tests/coop_copy_slot.py [--case keep|notice]
"""
import argparse
import hashlib
import os
import re
import time

import lane
from harness import waiter
from twoinst import Checks, keyframe, save_taken, start, wait_guest_in

wait = waiter(step=1)


def copy_slot(guest):
    """('GameN', 'Planet') of the co-op copy the guest's save database points to, (None, None) without one"""
    m = re.search(r"slot \S*\\(Game\d+): a copy of (\S+)'s world, made for (.+?)(, your own save)? \(own marks",
                  guest.call('coop.status')['out'])
    return (m.group(1), m.group(3)) if m else (None, None)


def digest(path):
    try:
        with open(path, 'rb') as f:
            return hashlib.sha256(f.read()).hexdigest()
    except OSError:
        return None


def played_from(guest):
    """the slot folder name the guest's last load of a received world came from ('' if none in the status)"""
    s, _ = copy_slot(guest)
    return s or ''


def keep_case(host, guest, ck):
    slot, planet = copy_slot(guest)
    if not ck(slot, f"the guest plays the host's world from a co-op copy ({slot}, made for {planet})"):
        return
    spo = os.path.join(lane.GUEST_DIR, 'Games', slot, planet + '.spo')
    guest.call('coop.leave')
    time.sleep(3)
    t0 = time.time()
    said, _, _ = save_taken(guest, label='guest save (alone)')
    ck(said.startswith('saved (mode'), f'the guest saves its game alone ({said[:80]})')
    ck(wait(lambda: os.path.exists(spo) and os.path.getmtime(spo) >= t0 - 1, 30), f'the game wrote {slot}/{planet}.spo')
    guest.call('key enter')  # the "game saved" box
    time.sleep(4)  # the mod's slot watch (once a second) sees the save
    print('  guest:', [ln for ln in guest.call('coop.status')['out'].splitlines() if ln.startswith('slot ')], flush=True)
    print('  guest joins again:', guest.call(lane.JOIN)['out'].strip()[:120], flush=True)
    wait(lambda: not guest.call('coop.avatar')['ok'], 40)
    ck(wait_guest_in(host, guest), "the guest is in the host's world again (share 1)")
    s1 = played_from(guest)
    print(f'  share 1: the guest plays {s1}', flush=True)
    time.sleep(5)
    # (the guest's game saved S once more when it left it for share 1's load: S now holds that save)
    mine = digest(spo)
    print('  host:', host.call('coop.share')['out'].strip()[:120], flush=True)
    wait(lambda: not guest.call('coop.avatar')['ok'], 60)
    ck(wait_guest_in(host, guest), "the guest loaded the host's world again (share 2)")
    s2 = played_from(guest)
    print(f'  share 2: the guest plays {s2}', flush=True)
    now = digest(spo)
    ck(mine is not None and now == mine,
       f"the guest's own save in {slot} is still there, the same bytes ({(mine or '')[:12]} -> {(now or 'missing')[:12]})")
    ck(s2 and s2 != slot, f"the host's world went into another slot ({s2}, not {slot})")
    keyframe('copy_keep', "Creature after two shares: both play the host's world; the guest's own save of the copy "
             "is kept in its slot")


def notice_case(host, guest, ck):
    slot, planet = copy_slot(guest)
    if not ck(slot, f"the guest plays the host's world from a co-op copy ({slot}, made for {planet})"):
        return
    host.call('coop.leave')
    time.sleep(3)
    guest.call('coop.leave')
    time.sleep(2)
    print('  guest hosts:', guest.call(f'cheat coop host {lane.PORT} Bob')['out'].strip()[:120], flush=True)
    want = re.compile(r"from your copy of Alice's world \(" + re.escape(slot) + r"\).*" + re.escape(planet))
    got = None
    t0 = time.time()
    while time.time() - t0 < 12 and not got:
        for ln in guest.call('coop.notify')['out'].splitlines():
            if ln.startswith('toast') and want.search(ln):
                got = ln
        time.sleep(0.3)
    print('  guest notification:', got, flush=True)
    ck(got, f"a notification on the new host's screen: it hosts {planet} from its copy of Alice's world ({slot}), "
       f"made for {planet}")
    keyframe('copy_notice', f"Creature on the guest, now the host: a notification says it hosts {planet} from its "
             "copy of Alice's world", players={'guest': guest}, settle=0)
    guest.call('coop.leave')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--case', choices=('keep', 'notice'), default='keep')
    a = ap.parse_args()
    host, guest = start('creature')
    ck = Checks()
    (keep_case if a.case == 'keep' else notice_case)(host, guest, ck)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
