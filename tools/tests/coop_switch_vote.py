#!/usr/bin/env python3
"""The Cell -> Creature switch waits for a vote of everybody (BUG-0315, docs/09-coop-design.md 9.3).

Cell fixture. The host finishes the Cell stage the players' way (food bar full, the cinematic, History, its check).
Until 0.7.10 the check started the Creature stage at once and the guest was pulled after it; only the first Creature
editor waited for a vote. Now the check starts a vote ("wants to move on to the Creature stage") and the switch runs
when it passes:

  1. After the host's check both games stay in the Cell stage (game.mode) and the guest's game shows the vote: its
     co-op window is open with "I'm ready" (coop.uistate).
  2. The guest's own click on History's check (its food bar is the shared one) does not take it to Creature alone.
  3. The guest clicks "I'm ready" (a real click on the widget): both games reach the Creature stage and the guest
     controls its creature there.
  4. The first Creature editor's vote counts the guest that arrived as ready: the host's editor and the guest's own
     editor open without another click of the guest (game.mode Editor on both).

  coop_switch_vote.py                               (PASS)
  SPORE_FIX_OFF=core.switch_vote coop_switch_vote.py  (step 1 FAILS: the host's game enters Creature at once)
"""
import time

from coop_ui import Ui
from twoinst import Checks, start

STAGE_BTN = (1396, 1063)  # the cell HUD button under the portrait: History, then the next stage
HISTORY_OK = (1415, 1055)  # its check: closes History, or starts the next stage
CAPTION_OK = (1410, 1048)


def mode(con):
    return con.call('game.mode')['out']


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None


def vote_open(host):
    return host.call('coop.vote')['out'].startswith('vote active')


def history_check(con):
    con.call('click %d %d' % STAGE_BTN)
    time.sleep(3)
    con.call('click %d %d' % HISTORY_OK)


def f9(con):
    con.call('key 120 down')  # VK_F9
    time.sleep(0.3)
    con.call('key 120 up')


def main():
    host, guest = start('cell')
    ck = Checks()
    gu = Ui(guest, 'guest')
    time.sleep(3)

    # the end of the Cell stage, the players' way (coop_cell_leave.py)
    host.call('coop.pool food 1000')
    t0 = time.time()
    clicked = None
    while time.time() - t0 < 120 and 'Creature' not in mode(host) and not vote_open(host):
        if 'cine=1' in host.call('coop.pool')['out']:
            host.call('click %d %d' % CAPTION_OK)
            time.sleep(3)
            continue
        if time.time() - t0 > 25:  # the cinematic is over: History, then its check = next stage
            history_check(host)
            clicked = clicked or time.time()
        time.sleep(4)
    print('  host after its check:', mode(host).strip(), '|', host.call('coop.vote')['out'].splitlines()[0], flush=True)

    # 1. both games stay in Cell while the vote runs, the guest's game shows it
    time.sleep(8)
    ck('Cell' in mode(host), f"the host's game is still in the Cell stage 8 s after its check ({mode(host).strip()})")
    ck('Cell' in mode(guest), f"the guest's game is still in the Cell stage ({mode(guest).strip()})")
    st = gu.wait(lambda s: s.get('open') == '1' and s['ready']['vis'], 15, 'the vote window')
    ck(st and st['ready']['text'] in ("I'm ready", 'Я готов'),
       f"the guest's game shows the vote: its co-op window with I'm ready ({st and st['ready']['text']!r})")

    # 2. the guest's own check does not start its Creature stage alone
    if 'Cell' in mode(guest):
        if gu.state().get('open') == '1':
            f9(guest)  # (the window covers the HUD's button)
            time.sleep(1)
        history_check(guest)
        time.sleep(6)
        ck('Cell' in mode(guest), f"the guest's own click on History's check keeps it in Cell ({mode(guest).strip()})")
        print('  guest:', guest.call('coop.vote')['out'].splitlines()[-1], flush=True)

    # 3. the guest's I'm ready: the switch runs for everybody
    if vote_open(host):
        st = gu.state()
        if st.get('open') != '1':
            f9(guest)
            gu.wait(lambda s: s['ready']['vis'], 10, 'ready button')
            st = gu.state()
        if st['ready']['text'] in ("I'm ready", 'Я готов'):
            gu.click('ready')
    ck(wait(lambda: 'Creature' in mode(host), 90), f"the host's game enters the Creature stage after the vote ({mode(host).strip()})")
    ck(wait(lambda: 'Creature' in mode(guest) and guest.call('coop.avatar')['ok'], 150),
       f"the guest's game follows into the Creature stage and plays its creature ({mode(guest).strip()})")

    # 4. the first Creature editor's vote: the guest arrived, nobody presses again
    t1 = time.time()
    ck(wait(lambda: 'Editor' in mode(host), 120, 2),
       f"the host's first Creature editor opens without another ready ({mode(host).strip()}, {time.time() - t1:.0f} s)")
    ck(wait(lambda: 'Editor' in mode(guest), 120, 2),
       f"the guest's own first Creature editor opens ({mode(guest).strip()})")
    print('  host vote:', ' | '.join(host.call('coop.vote')['out'].splitlines()[:1] + host.call('coop.vote')['out'].splitlines()[-1:]), flush=True)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
