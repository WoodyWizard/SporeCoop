#!/usr/bin/env python3
"""The host's evolution editor opens at once; the guest evolves when it wants (BUG-0516).

Creature fixture. Until 0.7.10 the host's evolution (the mating call's request) waited for the vote: every guest's
"I'm ready" or 60 s, and every guest's own editor opened at the vote's end. No world reload follows the host's editor
since BUG-0508, so the host goes on at once and each guest gets an offer ("Evolve now") instead of the ready vote:

  1. `coop.vote evolve` on the host (the creature stage's own evolution request, as the game builds it): the host's
     game is in its editor within 15 s although the guest pressed nothing.
  2. The guest's game meanwhile stays in the Creature stage (no editor forced on it) and its co-op window shows
     "Evolve now" (coop.uistate).
  3. The host leaves its editor (done): back in its Creature stage, while the guest has not evolved.
  4. The guest evolves later: a real click on "Evolve now" opens its own editor (game.mode Editor) while the host
     plays; the guest leaves it with done and plays its creature again.

  coop_editor_now.py                                 (PASS)
  SPORE_FIX_OFF=core.editor_now coop_editor_now.py   (step 1 FAILS: the host's editor waits for the vote)
"""
import time

from coop_ui import Ui
from twoinst import Checks, start

DONE = (1416, 1062)    # the creature editors' green check (1440x1080 test window)
SAVE_NEW = (926, 604)  # the own editor's exit dialog: "Save New"
EVOLVE = ('Evolve now', 'Эволюция')


def mode(con):
    return con.call('game.mode')['out']


def in_editor(con):
    return 'Editor' in mode(con)


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def back(con, m0, secs=90):
    return wait(lambda: mode(con) == m0 and con.call('coop.avatar')['ok'], secs)


def main():
    host, guest = start()
    ck = Checks()
    gu = Ui(guest, 'guest')
    time.sleep(3)
    m0 = mode(host)

    # 1. the host's editor at once
    t0 = time.time()
    print(' ', host.call('coop.vote evolve')['out'].strip().splitlines()[0], flush=True)
    ck(wait(lambda: in_editor(host), 15), f"the host's game is in its evolution editor without the guest's ready "
       f"({mode(host).strip()}, {time.time() - t0:.0f} s)")

    # 2. the guest is offered the evolution, not pulled into it
    st = gu.wait(lambda s: s.get('open') == '1' and s['ready']['vis'], 10, 'the offer')
    ck(st and st['ready']['text'] in EVOLVE, f"the guest's co-op window offers Evolve now ({st and st['ready']['text']!r})")
    time.sleep(10)
    ck(not in_editor(guest) and guest.call('coop.avatar')['ok'],
       f"the guest's game plays on in the Creature stage while the host edits ({mode(guest).strip()})")

    # 3. the host is done first
    if in_editor(host):
        time.sleep(5)  # (the editor's interface comes up)
        host.call('click %d %d' % DONE)
    ck(back(host, m0), f"the host is back in its stage after done ({mode(host).strip()})")
    ck(not in_editor(guest), f"the guest has still not evolved ({mode(guest).strip()})")

    # 4. the guest evolves later
    st = gu.state()
    if st.get('open') != '1':
        guest.call('key 120 down')  # F9
        time.sleep(0.3)
        guest.call('key 120 up')
        st = gu.wait(lambda s: s.get('open') == '1', 6, 'the co-op window')
    ck(st and st['ready']['text'] in EVOLVE, f"the offer still holds after the host's editor ({st and st['ready']['text']!r})")
    if st and st['ready']['text'] in EVOLVE:
        gu.click('ready')
    ck(wait(lambda: in_editor(guest), 90), f"the guest's own editor opens on Evolve now ({mode(guest).strip()})")
    ck(not in_editor(host), f"the host plays on meanwhile ({mode(host).strip()})")
    if in_editor(guest):
        # the own editor is built like the game's evolution request (creations.editor_request, BUG-0720): it has no
        # red cross, the guest leaves it with done like the host (and Save New if its exit dialog asks)
        time.sleep(8)
        guest.call('click %d %d' % DONE)
        time.sleep(4)
        if in_editor(guest):
            guest.call('click %d %d' % SAVE_NEW)
    ck(back(guest, m0), f"the guest leaves its editor and plays its creature again ({mode(guest).strip()})")
    print('  guest vote:', guest.call('coop.vote')['out'].splitlines()[-1], flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
