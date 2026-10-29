#!/usr/bin/env python3
"""A guest's "I'm ready" counts while a caption plays on the guest (BUG-0273).

In the four-player campaign the Creature -> Tribe vote started while the guests still had the
brain-upgrade captions of the evolution points on screen; their real clicks on "I'm ready" were
dropped and the vote ended by its timer. Every cinematic (a caption) holds the game's input lock
(cGameInputManager +0x110), and the game's UI drops the clicks on our window while it is held.

1. the guest plays CRG_ClaimNest (its caption keeps it running: the input lock is held)
2. the host starts a vote; the guest's co-op window opens; a real click on "I'm ready"
3. the host's vote ends "everybody is ready" (not by its timer), the caption still plays
4. a second vote right after the first, the caption still up: the click counts again
5. the caption ends; a third vote without a caption: the click counts (no double toggle)

SPORE_FIX_OFF=core.ui_locked_clicks: the clicks are dropped again (the test fails).
"""
import re
import time

from coop_ui import Ui
from twoinst import Checks, start

CLAIM = '63B7060A'  # CRG_ClaimNest


def eax(con, what):
    m = re.search(r'eax=0x([0-9A-Fa-f]+)', con.call('call ' + what)['out'])
    return int(m.group(1), 16) if m else None


def cinematic(con):
    m = re.search(r'cinematic ([0-9A-F]{8}) playing', con.call('coop.status')['out'])
    return m.group(1) if m else None


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        v = cond()
        if v:
            return v
        time.sleep(step)
    return None


def vote(host, gu, ck, what):
    """the host starts a test vote, the guest clicks I'm ready once; True when the vote ended by it"""
    print(' ', host.call('coop.vote test')['out'].splitlines()[0])
    st = gu.wait(lambda s: s['open'] == '1' and s['ready']['vis'] and 'I' in s['ready']['text'], 15, 'ready button')
    ck(st is not None, f'{what}: the guest sees the vote and its I\'m ready button')
    time.sleep(1)
    gu.click('ready')
    ended = wait(lambda: 'vote none' in host.call('coop.vote')['out'], 8)
    status = host.call('coop.status')['out']
    print(f'  {what}: vote ended={bool(ended)} {host.call("coop.vote")["out"].splitlines()[0]}')
    ck(bool(ended) and 'time is up' not in status.split('evolution starts')[-1][:20],
       f'{what}: the host\'s vote ends when the guest is ready (not by its timer)')
    if not ended:
        host.call('coop.vote go')  # end it for the next step
        wait(lambda: 'vote none' in host.call('coop.vote')['out'], 5)
    time.sleep(2)
    return bool(ended)


def main():
    host, guest = start()
    ck = Checks()
    gu = Ui(guest, 'guest')
    time.sleep(2)
    cm = eax(guest, 'Simulator::cCinematicManager::Get cdecl')
    print(' ', guest.call(f'call Simulator::cCinematicManager::PlayCinematic thiscall {cm:#x} s:CRG_ClaimNest 1 1 0 0 0')['out'].strip())
    ck(wait(lambda: cinematic(guest) == CLAIM, 10), 'the guest plays CRG_ClaimNest (a caption, the input lock held)')
    time.sleep(3)
    lock = int(gu.state().get('input_lock', '0'))
    ck(lock > 0, f'the caption holds the game\'s input lock on the guest (cGameInputManager +0x110 = {lock})')

    vote(host, gu, ck, 'vote 1 during a caption')
    ck(cinematic(guest) == CLAIM, 'the caption still plays on the guest after vote 1')
    vote(host, gu, ck, 'vote 2 right after vote 1, during the caption')

    # the caption ends (Enter answers a caption; CRG_ClaimNest started by hand may not take it:
    # then the game's own EndCinematic, which releases the input lock)
    for _ in range(3):
        if not cinematic(guest):
            break
        guest.call('key enter')
        time.sleep(2)
    if cinematic(guest):
        print(' ', guest.call(f'call SporeCoop::cCinematicManager_EndCinematic thiscall {cm:#x}')['out'].strip())
        wait(lambda: not cinematic(guest), 5)
    ck(not cinematic(guest), 'the caption is over on the guest')
    time.sleep(3)
    lock = int(gu.state().get('input_lock', '0'))
    ck(lock == 0, f'the input lock is free on the guest ({lock})')
    vote(host, gu, ck, 'vote 3 without a caption')
    st = gu.state()
    print(f"  guest: locked_clicks={st.get('locked_clicks')} locked_native={st.get('locked_native')}")
    ck(st.get('locked_native') == '0', 'no click was pressed twice (by the game and by the mod)')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
