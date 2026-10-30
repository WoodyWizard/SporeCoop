#!/usr/bin/env python3
"""The guest's own editor does not change while it is open (BUG-0283).

The first Creature vote from the Cell stage, as in coop_legless_guest.py: the host adds legs in its
editor and leaves it; the vote opened the guest's own editor on the host's legless pre-editor
creature. The guest does nothing in its editor until the host's new body has reached it ("evolved
the species", the host's creation bundle and its world), then clicks the red cross once.

Seen on 0.7.3.0 (lab run 20261029-143204, lane 8): the guest's editor showed the host's new legs and
its DNA dropped by their price while it was open, and the red cross asked "Exiting will cause your
changes to be lost" instead of leaving. Checked on the guest's game (its mode): one click on the red
cross leaves the editor (an editor without changes leaves at once, docs/re/creations.md
#creations-own-editor-save).

  coop_guest_editor_hold.py   (SPORE_FIX_OFF=creations.editor_hold must FAIL)"""
import time

import lane
from coop_legless_guest import (CANCEL, CAPTION_OK, DONE, HISTORY_OK, QUIT, READY, STAGE_BTN, add_legs, editor_ui,
                                mode, wait)
from twoinst import Checks, shot, start


def guest_log_has(text, since):
    try:
        with open(lane.game_log(lane.GUEST), 'rb') as f:
            f.seek(since)
            return text in f.read().decode('utf-8', 'replace')
    except OSError:
        return False


def log_size():
    try:
        with open(lane.game_log(lane.GUEST), 'rb') as f:
            f.seek(0, 2)
            return f.tell()
    except OSError:
        return 0


def main():
    host, guest = start('cell', guest_creations=False)
    ck = Checks()
    host.call('coop.pool food 1000')
    t0 = time.time()
    while time.time() - t0 < 150 and 'Creature' not in mode(host):
        if 'cine=1' in host.call('coop.pool')['out']:
            host.call('click %d %d' % CAPTION_OK)
            time.sleep(3)
            continue
        if time.time() - t0 > 25:
            host.call('click %d %d' % STAGE_BTN)
            time.sleep(3)
            host.call('click %d %d' % HISTORY_OK)
        time.sleep(4)
    ck('Creature' in mode(host), f'the host entered the Creature stage ({mode(host).strip()})')
    ck(wait(lambda: 'vote active' in host.call('coop.vote')['out'], 30), 'its first editor waits for the vote')
    ck(wait(lambda: 'Creature' in mode(guest) and guest.call('coop.avatar')['ok'], 120), 'the guest follows')
    time.sleep(3)
    pos = log_size()
    guest.call('click %d %d' % READY)
    ck(wait(lambda: 'Editor' in mode(host), 40), 'the host is in the creature editor')
    time.sleep(6)
    add_legs(host)
    host.call('click %d %d' % DONE)
    own = wait(lambda: 'Editor' in mode(guest), 120)
    ck(own, "the guest's own editor opens after the vote")
    if not own:
        return ck.result()
    ck(wait(lambda: editor_ui(guest), 40), "the guest's own editor shows its interface")
    shot(guest, 'editor_hold_open.png')
    got = wait(lambda: guest_log_has('creations: player 0 creature =', pos) and
               guest_log_has('receiving world from', pos), 90)
    ck(got, "the host's new body reached the guest while its editor is open")
    time.sleep(5)
    shot(guest, 'editor_hold_after_host.png')
    print('  guest creations: ' + guest.call('coop.creations')['out'].strip().replace('\n', ' | ')[:300], flush=True)
    guest.call('click %d %d' % CANCEL)
    left = wait(lambda: 'Creature' in mode(guest) or 'LoadGame' in mode(guest), 15)
    print('  guest mode after one click on the red cross: ' + mode(guest).strip(), flush=True)
    if not left:
        shot(guest, 'editor_hold_dialog.png')
    ck(left, 'one click on the red cross leaves the unchanged editor (no "changes will be lost")')
    if not left:
        guest.call('click %d %d' % QUIT)
        wait(lambda: 'Editor' not in mode(guest), 30)
    ck(wait(lambda: 'Creature' in mode(host), 60), 'the host is back in the Creature stage')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
