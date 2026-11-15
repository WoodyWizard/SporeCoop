#!/usr/bin/env python3
"""BUG-0638: a guest still in its own Cell editor when the host enters Creature played, after the load, a creature it
had chosen in another world (Discord report 2026-11-06, 0.7.8: "after I left the editor I became a species from
another world").

The player's way, in the `cell` fixture:
  1. the guest has an explicit creature choice of another world: `coop.creations choose creature <newest own>` and
     `worlds forget` (no record of this world: the choice was made elsewhere, as the player's two days before);
  2. the guest's own mating call opens its own Cell editor (real clicks, coop_editor_desync.py's way);
  3. meanwhile the host fills its food bar, History, the next stage: the host enters Creature; the guest presses
     "I'm ready" from its editor (F9, the co-op window), the host leaves its creature editor with "done";
  4. the host's world reaches the guest while its editor is open; the guest leaves its editor (the red cross) and
     loads the host's Creature world.
PASS: in the guest's game its avatar plays the world's species (the host's avatar species), not the other world's
chosen creature, and the host's game shows the guest's puppet with that species.

  coop_choice_world.py     (SPORE_FIX_OFF=creations.choice_world: the guest plays its old chosen creature: FAILS)"""
import re
import time

import creations_state
import lane
from coop_editor_desync import CANCEL, mating_call
from coop_legless_guest import CAPTION_OK, DONE, HISTORY_OK, STAGE_BTN
from coop_ui import Ui
from twoinst import Checks, shot, start

VK_F9 = '0x78'


def mode(con):
    return con.call('game.mode')['out']


def avatar(con):
    m = re.search(r'key=0x([0-9A-F]{8})', con.call('coop.avatar')['out'])
    return m.group(1) if m else None


def puppet(con, rid):
    m = re.search(r"remote %d '[^']*'.*\n.*species=([0-9A-F]{8})" % rid, con.call('coop.status')['out'])
    return m.group(1) if m else None


def wait(cond, secs, step=1.0):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return cond()


def log_size(inst):
    try:
        with open(lane.game_log(inst), 'rb') as f:
            return f.seek(0, 2)
    except OSError:
        return 0


def log_has(inst, text, since):
    try:
        with open(lane.game_log(inst), 'rb') as f:
            f.seek(since)
            return text in f.read().decode('utf-8', 'replace')
    except OSError:
        return False


def ready(con):
    """F9, then "I'm ready" (a real click on the widget), F9 again"""
    ui = Ui(con, 'guest')
    if ui.state().get('open') != '1':
        con.call(f'key {VK_F9} down')
        time.sleep(0.3)
        con.call(f'key {VK_F9} up')
        ui.wait(lambda s: s.get('open') == '1', 6, 'the co-op window')
    st = ui.state()
    if st.get('ready', {}).get('text', '') in ('Not ready', 'Не готов'):
        r = 'already ready'
    elif st.get('ready', {}).get('vis'):
        ui.click('ready')
        r = 'ready clicked'
    else:
        r = 'no ready button'
    time.sleep(1)
    if ui.state().get('open') == '1':
        con.call(f'key {VK_F9} down')
        time.sleep(0.3)
        con.call(f'key {VK_F9} up')
    return r


def main():
    host, guest = start('cell')
    ck = Checks()
    st0 = creations_state.save(guest)
    try:
        return run(host, guest, ck)
    finally:
        creations_state.restore(guest, st0)  # the profile as it was (BUG-0345)
        print('  left:', guest.call('coop.creations worlds')['out'].strip(), flush=True)


def run(host, guest, ck):
    own = creations_state.own_keys(guest)
    ck(own, f'the guest profile has own creatures ({len(own)})')
    if not own:
        return ck.result()
    other = own[0]
    print(' ', guest.call(f'coop.creations choose creature {other}')['out'].strip(), '|',
          guest.call('coop.creations worlds forget')['out'].strip(), flush=True)
    print('  guest worlds:', guest.call('coop.creations worlds')['out'].strip(), flush=True)

    # 2. the guest's own Cell editor
    alive, reached = mating_call(guest, 'guest', lambda: 'Editor' in mode(guest))
    ck(wait(lambda: 'Editor' in mode(guest), 60), f"the guest's own Cell editor opened ({mode(guest).strip()})")
    gpos = log_size(lane.GUEST)

    # 3. the host enters Creature meanwhile (the food bar, History, the next stage)
    host.call('coop.pool food 1000')
    t0 = time.time()
    while time.time() - t0 < 180 and 'Creature' not in mode(host) and 'Editor' not in mode(host):
        if 'vote active' in host.call('coop.vote')['out']:
            break
        if 'cine=1' in host.call('coop.pool')['out']:
            host.call('click %d %d' % CAPTION_OK)
            time.sleep(3)
            continue
        if time.time() - t0 > 25:
            host.call('click %d %d' % STAGE_BTN)
            time.sleep(3)
            host.call('click %d %d' % HISTORY_OK)
        time.sleep(4)
    ck(wait(lambda: 'vote active' in host.call('coop.vote')['out'] or 'Editor' in mode(host), 60),
       f"the host entered Creature, its first editor waits for the vote ({mode(host).strip()})")
    ck('Editor' in mode(guest), f'the guest is still in its own editor ({mode(guest).strip()})')
    for _ in range(4):
        if 'Editor' in mode(host) and 'vote active' not in host.call('coop.vote')['out']:
            break
        print('  guest:', ready(guest), flush=True)
        time.sleep(4)
    ck(wait(lambda: 'Editor' in mode(host), 40), 'the host is in its creature editor')
    time.sleep(6)
    host.call('click %d %d' % DONE)
    ck(wait(lambda: 'Creature' in mode(host) and avatar(host), 90), 'the host plays Creature after its editor')
    h_av = avatar(host)
    got = wait(lambda: log_has(lane.GUEST, 'is loaded when you leave the editor', gpos), 120, 2)
    ck(got, "the host's Creature world reached the guest while its own editor is open")
    shot(guest, 'choice_world_editor.png')

    # 4. the guest leaves its editor and loads the host's world
    guest.call('click %d %d' % CANCEL)
    time.sleep(4)
    if 'Editor' in mode(guest):
        guest.call('click %d %d' % DONE)
    ok = wait(lambda: 'Creature' in mode(guest) and avatar(guest), 180, 2)
    ck(ok, f'the guest plays Creature in the host\'s world ({mode(guest).strip()})')
    # the creation swap came 3 s after the load in the report, 25 s in the lab (20261106-230447): 45 s of samples
    t0, bad, seen = time.time(), [], []
    while time.time() - t0 < 45:
        g_av, h_pup = avatar(guest), puppet(host, 1)
        seen.append((round(time.time() - t0), g_av, h_pup))
        # (None: no avatar in that sample, a world load of the guest's game; not another creature)
        if (g_av and g_av != h_av) or (time.time() - t0 > 10 and h_pup and h_pup != h_av):
            bad.append(seen[-1])
        time.sleep(3)
    ck(seen[-1][1] == h_av and seen[-1][2] == h_av and sum(1 for s in seen if s[1]) >= 10,
       f"the guest plays a creature for most of the 45 s and at the end ({sum(1 for s in seen if s[1])} samples with one)")
    print(f'  host avatar {h_av}, other world\'s choice {other}; samples (s, guest avatar, host shows the guest): {seen}',
          flush=True)
    print('  guest:', guest.call('coop.creations worlds')['out'].strip(), flush=True)
    ck(not [b for b in bad if b[1] != h_av],
       f"for 45 s after the load the guest plays the world's species {h_av}, not the other world's chosen {other} "
       f"(other: {[b for b in bad if b[1] != h_av][:3]})")
    ck(not [b for b in bad if b[2] != h_av],
       f"the host's game shows the guest with the world's species (other: {[b for b in bad if b[2] != h_av][:3]})")
    shot(guest, 'choice_world_guest.png')
    shot(host, 'choice_world_host.png')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
