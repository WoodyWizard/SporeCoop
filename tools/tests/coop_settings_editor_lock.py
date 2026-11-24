#!/usr/bin/env python3
"""The session stays started while the host is in its editor (NET-8 of the external audit, BUG-1502).

settings.cpp took the open world from "this game plays a stage", so in the host's editor (and during a load) the world
counted as closed: started() was false, `all.late_join off` let a new player in, and the Session settings could be
changed (for "the next world", which the next world opened then took). Now an editor or a load keeps the open world
(and the session started); only the menu closes it.

Creature fixture, two players (cold: the settings are lane knobs):
  1. The host sets all.late_join off (`coop.settings force`); the guest leaves.
  2. The host evolves alone (`coop.vote evolve`: its editor opens at once) and waits in its editor.
  3. The guest joins as a new player (name Carol): refused ("does not take new players"), the host still alone; key
     frame of the guest's screen (the refusal toast, no host world).
  4. In its editor the host tries to change a Session setting (`coop.settings set all.friendly_fire on`, no force):
     refused ("a session setting").
  5. The host leaves its editor (the red cross); the guest joins as Bob again (a player who was in the session: it
     comes back), the settings back to their defaults.
PASS: 3 and 4 refused. SPORE_FIX_OFF=settings.world_kept: Carol joins during the host's editor and the setting is
taken ("for the next world"): FAIL.

  SPORE_LANE=6 python3 tools/tests/coop_settings_editor_lock.py
"""
import time

import lane
import settings_lib as sl
import twoinst
from harness import mode, waiter
from twoinst import Checks, keyframe, start, wait_guest_in

wait = waiter(step=1.0)
KEYS = ('all.late_join', 'all.friendly_fire')
DONE = (1416, 1062)    # the creature editor's green check (coop_editor_now.py)
CANCEL = (1383, 1062)  # the red cross


def in_editor(con):
    return 'Editor' in mode(con)


def connected(guest):
    return 'connected' in (guest.call('coop.status')['out'].splitlines() or [''])[0]


def main():
    twoinst.forget_settings()
    host, guest = start('creature')
    ck = Checks()
    m0 = mode(host)
    try:
        time.sleep(3)
        ck(sl.force(host, [guest], 'all.late_join', 'off'), 'the running guest got all.late_join off')
        guest.call('coop.leave')
        ck(wait(lambda: 'players=1/' in host.call('coop.status')['out'], 20), 'the guest left: the host is alone')
        print(' ', (host.call('coop.vote evolve')['out'] or '').strip().splitlines()[:1], flush=True)
        if not ck(wait(lambda: in_editor(host), 60), f"the host is in its editor ({mode(host).strip()})"):
            return ck.result()
        time.sleep(5)  # (the editor's interface comes up)
        print('  host settings:', (host.call('coop.settings')['out'] or '').splitlines()[:1], flush=True)

        # 3. a new player during the host's editor (fresh token so old lane kept-tokens never match Bob)
        guest.call('coop.space keptkey other on')
        guest.call(lane.JOIN.rsplit(' ', 1)[0] + ' Carol')
        told = wait(lambda: 'does not take new players' in guest.call('coop.status')['out'], 15)
        time.sleep(4)
        hs = host.call('coop.status')['out']
        print('  guest:', (guest.call('coop.status')['out'].splitlines() or [''])[0], flush=True)
        keyframe('editor_join', "the guest's screen while the host is in its editor: the refusal toast "
                 "(the host takes no new players), no host world joined", players={'guest': guest})
        ck(not connected(guest) and 'players=1/' in hs,
           "late join off: a new player is refused while the host is in its editor")
        ck(told, 'the refused player is told why')

        # 4. a session setting during the host's editor
        r = host.call('coop.settings set all.friendly_fire on')
        out = (r.get('out') or '').strip()
        print('  host set all.friendly_fire on:', out[:160], flush=True)
        ck(sl.get(host, 'all.friendly_fire') == 0 and not r.get('ok'),
           f"in the host's editor a session setting cannot be changed ({out[:80]!r})")

        # 5. back
        guest.call('coop.leave')
        guest.call('coop.space keptkey other off')
        host.call('click %d %d' % DONE)
        if not wait(lambda: mode(host) == m0 and host.call('coop.avatar')['ok'], 20):
            host.call('click %d %d' % CANCEL)
        ck(wait(lambda: mode(host) == m0 and host.call('coop.avatar')['ok'], 70),
           f"the host leaves its editor and plays again ({mode(host).strip()})")
        guest.call(lane.JOIN)
        ck(wait_guest_in(host, guest, 200), 'the guest comes back as Bob (a player who was in the session)')
    finally:
        try:
            guest.call('coop.space keptkey other off')
        except OSError:
            pass
        sl.reset([host], KEYS)
        twoinst.forget_settings()
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
