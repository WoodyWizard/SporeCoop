#!/usr/bin/env python3
"""Civilization -> Space: a guest of the shared nation opens its own spaceship editor cleanly after the stage-advance
vote (BUG-1531).

At the end of Civ the host's game submits EditorRequest (0x96B24187, tag 0x0116D51D) through the co-op vote. Guests
receive city captures via cCity::ChangeOwner and never run PlanetWon themselves, so without `civ.guest_ship_editor`
no spaceship editor opens on a guest. With `civ.guest_ship_editor` on, when the host's Civ -> Space vote passes, each
guest in the host's Civ world opens its own spaceship editor (0x96B24187, tag 0x0C0B1531 so cGameModeCiv does not
launch a local Space stage, red cross enabled so the guest can decline) and unpauses cleanly on return to Civ.

Checked (fixture `civ`, 2 players):
  1. Host runs `coop.vote ship` (native Civ -> Space EditorRequest through the Submit hook): vote activates on host
     and guest.
  2. Guest votes ready (`I'm ready` in the co-op window): vote passes; BOTH host and guest enter the spaceship editor
     (`0x00DBDBA1 Editor`); guest `coop.civship` shows `opened=1`.
  3. Guest places three spaceship parts (`EDITOR_PARTS` -> `EDITOR_PLACES`), rolls a random name (`EDITOR_DICE`), and
     clicks Done (`EDITOR_DONE`): guest returns cleanly to `Civilization` (`0x01654C04`), `coop.civship` shows
     `phase=done`, `results=1`, `cancelled=0`, non-zero creation key, and the Civ stage is not stuck paused.

SPORE_FIX_OFF=civ.guest_ship_editor (suite `civ_ship_editor_off`): step 2 FAILs (guest stays in Civilization,
`opened=0`).

  SPORE_LANE=9 python3 tools/tests/coop_civ_ship_editor.py
"""
import re
import time

from coop_campaign import EDITOR_DICE, EDITOR_DONE, EDITOR_PARTS, EDITOR_PLACES, drag_slow, editor_click
from coop_ui import Ui
from harness import waiter
from twoinst import Checks, keyframe, shot, start

CIV, EDITOR = '0x01654C04', '0x00DBDBA1'
SHIP_MOVES = list(zip(EDITOR_PARTS, EDITOR_PLACES))
wait = waiter(step=0.5, final=True)


def mode_id(con):
    try:
        out = con.call('game.mode')['out'].strip()
    except OSError:
        return ''
    return (out.split() or [''])[0]


def civship(con):
    out = con.call('coop.civship')['out'] or ''
    lines = out.splitlines()
    st = dict(re.findall(r'(\w+)=(\S+)', lines[0])) if lines else {}
    res = {}
    if len(lines) > 1:
        m = re.search(r'seen=(\d) type=(\w+) key=(\w+)/(\w+)/(\w+) cancelled=(\d)', lines[1])
        if m:
            res = dict(seen=m.group(1) == '1', type=int(m.group(2), 16), key=int(m.group(3), 16),
                       ktype=int(m.group(4), 16), kgroup=int(m.group(5), 16), cancelled=m.group(6) == '1')
    return st, res, out


def busy_flags(con):
    out = con.call('coop.status')['out'] or ''
    m = re.search(r'busy=(\S+)', out)
    return m.group(1) if m else ''


def drive_ship_editor(con, tries=12):
    """Place three spaceship parts, click Done, roll a random name, click Done until back in Civ."""
    for n in range(tries):
        if mode_id(con) != EDITOR:
            return True
        pace = 1.0 + n // 3
        if n % 3 == 0:
            for (sx, sy), (tx, ty) in SHIP_MOVES:
                drag_slow(con, sx, sy, tx, ty, pace)
        editor_click(con, *EDITOR_DONE)
        time.sleep(1.5)
        if mode_id(con) != EDITOR:
            break
        editor_click(con, *EDITOR_DICE)
        editor_click(con, *EDITOR_DONE, settle=0)
        t0 = time.time()
        while time.time() - t0 < 8 and mode_id(con) == EDITOR:
            time.sleep(0.5)
        if n == 1:
            shot(con, 'civ_ship_guest_editor.png')
    return wait(lambda: mode_id(con) == CIV, 60, 1.0)


def main():
    ck = Checks()
    host, guest = start('civ')
    ck(wait(lambda: mode_id(host) == CIV and mode_id(guest) == CIV, 30, 1.0),
       f'both games start in Civ (host={mode_id(host)} guest={mode_id(guest)})')

    # 1. Host submits the native Civ -> Space spaceship EditorRequest through the vote hook
    out = host.call('coop.vote ship')['out']
    print('  host coop.vote ship:', out.strip(), flush=True)
    ck(wait(lambda: host.call('coop.vote')['out'].startswith('vote active'), 10, 0.5),
       'host Civ -> Space spaceship vote is active')
    ck(wait(lambda: guest.call('coop.vote')['out'].startswith('vote active'), 10, 0.5),
       'guest sees the Civ -> Space spaceship vote')

    # 2. Guest votes ready -> both host and guest enter the spaceship editor
    gu = Ui(guest, 'guest')
    if gu.wait(lambda s: s['ready']['vis'], 10, 'ready button'):
        gu.click('ready')
    else:
        guest.call('coop.ready 1')

    host_ed = wait(lambda: mode_id(host) == EDITOR, 45, 0.5)
    ck(host_ed, f'host enters the spaceship editor ({mode_id(host)})')

    guest_ed = wait(lambda: mode_id(guest) == EDITOR, 35, 0.5)
    st, res, raw = civship(guest)
    print('  guest civship (open):', raw.strip(), flush=True)
    ck(guest_ed and st.get('opened') == '1',
       f'guest opens its own spaceship editor after the vote (mode={mode_id(guest)} civship={st})')
    keyframe('ship_editor_open', 'Civ -> Space vote passed: host and guest in the spaceship editor', settle=0)
    if not guest_ed:
        ck.faults(host=host, guest=guest)
        return ck.result()

    # 3. Guest builds a spaceship and leaves the editor cleanly back to Civ
    back = drive_ship_editor(guest)
    ck(back, f'guest leaves its spaceship editor cleanly back to Civ ({mode_id(guest)})')
    if back:
        done_ok = wait(lambda: civship(guest)[0].get('phase') == 'done', 20, 0.5)
        st, res, raw = civship(guest)
        print('  guest civship (done):', raw.strip(), flush=True)
        ck(done_ok, f'guest civship reached phase=done ({st})')
        ck(res.get('seen') and not res.get('cancelled') and res.get('key', 0) != 0,
           f'guest spaceship result captured cleanly ({res})')
        unpaused = wait(lambda: 'paused' not in busy_flags(guest), 20, 0.5)
        ck(unpaused, f'guest Civ stage is not stuck paused after returning from spaceship editor (busy={busy_flags(guest)})')
        keyframe('ship_editor_back', 'guest returned cleanly from its spaceship editor to Civilization', settle=0)

    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
