#!/usr/bin/env python3
"""The end of Tribe with another cinematic playing when the graduation starts (BUG-1325).

Lab 20261119-120449 (line_stages): back from the City Hall editor the host's game formed its first NPC tribe and played
TRG_PRE_FirstTribeAppears; TribeStage_Graduate then played TRG2CVG_ShowCityHall without the queue flag, the cinematic
manager dropped it, so its end never submitted the vehicle editor: the host stood in Tribe with the advance button up.
The next advance (the campaign driver's click, as a stuck player's) played TRG2CVG_PreEditor, the City Hall editor and
the graduation again, which faulted in cGameNounManager::DestroyInstance on the food mat the first one had destroyed
(TribeStage_Graduate+0x34D, a fault storm). docs/re/tribe.md#tribe-graduation-cinematic.

tribe fixture. The race on demand: `coop.tribe gradcine TRG_TotemPole_Conquer` on the host starts that cinematic right
before the next graduation (the game's own TRG_PRE_FirstTribeAppears needs a planet with no NPC tribe yet). Then the
end of Tribe the players' way (tribe_civ's steps): the advance button, the card, the captions, the votes, the editors.
A host that stands in Tribe with the advance button up 75 s after the City Hall editor clicks it again, as a player
would. Checked:
  * the game asks for a vehicle editor (the vote's editor F670AA43 / 9AD7D4AA / 7D433FAD, the game's own request)
    after the City Hall editor, without a second advance;
  * the host's game reaches the Civ stage, the guest's follows;
  * no fault on either game (a second graduation faulted in DestroyInstance).

  SPORE_FIX_OFF=tribe.city_hall_cinematic: the vehicle editor never comes (FAIL); the second advance runs a second
    graduation, which game.destroy_null survives (no fault).
  SPORE_FIX_OFF=tribe.city_hall_cinematic,game.destroy_null: also the fault of lab 20261119-120449 (FAIL).
"""
import re
import sys
import time

from harness import mode, waiter
from coop_ui import Ui
from twoinst import Checks, shot, start
from coop_tribe_civ import ADVANCE, CARD_OK, CAPTION_OK, busy, finish_editor, vote_active, where

PLANT = 'TRG_TotemPole_Conquer'
VEHICLE_EDITORS = ('F670AA43', '9AD7D4AA', '7D433FAD')
CITY_HALL = '99E92F05'
STALL = 75  # s back in Tribe after the City Hall without a vote: the game will ask for nothing (ShowCityHall is ~60 s)

wait = waiter(step=1.0)


def vote_editor(con):
    """the editor of the game's last request (the vote's), from the end-of-Tribe guard's line"""
    m = re.search(r'last editor ([0-9A-F]{8})', con.call('coop.vote tribeend')['out'])
    return m.group(1) if m else None


def click(con, xy):
    con.call('move %d %d' % xy)
    time.sleep(0.4)
    con.call('click %d %d' % xy)


def advance(host):
    """the advance button, then the card and the PreEditor caption until the game asks for its City Hall editor"""
    r = wait(lambda: where(host, ADVANCE), 15)
    if not r:
        return False
    click(host, ((r[0] + r[2]) // 2, (r[1] + r[3]) // 2))
    time.sleep(5)
    t0 = time.time()
    while time.time() - t0 < 120 and not vote_active(host) and 'Editor' not in mode(host):
        b = busy(host)
        if 'cinematic' not in b:
            host.call('click %d %d' % CARD_OK)
        elif 'paused' in b:
            time.sleep(2)
            host.call('click %d %d' % CAPTION_OK)
        time.sleep(3)
    return vote_active(host) or 'Editor' in mode(host)


def main():
    ck = Checks()
    host, guest = start(fixture='tribe')
    ck('Tribe' in mode(host) and 'Tribe' in mode(guest), 'both games in Tribe')
    print('  host: ' + host.call(f'coop.tribe gradcine {PLANT}')['out'].strip(), flush=True)
    print(' ', host.call('coop.tribe advance')['out'].strip(), flush=True)
    if not ck(advance(host), "the advance gives the game's City Hall editor request (the vote)"):
        return ck.result()
    gu = Ui(guest, 'guest')
    editors = []      # the game's requests, in order
    advances = 1
    back_at = None    # back in Tribe from the last editor
    vehicle_after_city_hall = None
    t0 = time.time()
    while time.time() - t0 < 900 and 'Civ' not in mode(host):
        if vote_active(host):
            ed = vote_editor(host)
            editors.append(ed)
            print(f'  vote for editor {ed} ({time.time() - t0:.0f} s)', flush=True)
            if len(editors) >= 2 and editors[-2] == CITY_HALL and vehicle_after_city_hall is None:
                vehicle_after_city_hall = ed in VEHICLE_EDITORS
            gu.wait(lambda s: s['ready']['vis'], 15, 'ready button')
            gu.click('ready')
            wait(lambda: 'Editor' in mode(host), 90, 0.5)
            continue
        if 'Editor' in mode(host):
            finish_editor(host)
            wait(lambda: 'Editor' not in mode(host), 20)
            if 'Editor' not in mode(host):
                back_at = time.time()
                time.sleep(4)
                print('  host back from the editor: ' + host.call('coop.tribe graduation')['out'].strip(), flush=True)
            continue
        b = busy(host)
        if 'paused' in b and 'cinematic' in b:
            host.call('click %d %d' % CAPTION_OK)
        if back_at and time.time() - back_at > STALL and 'Tribe' in mode(host) and 'cinematic' not in b \
                and where(host, ADVANCE):
            if editors and editors[-1] == CITY_HALL and vehicle_after_city_hall is None:
                vehicle_after_city_hall = False
            shot(host, f'tribe_city_hall_race_stalled{advances}')
            print(f'  host stands in Tribe {time.time() - back_at:.0f} s after the City Hall editor, the advance button '
                  'up: clicked again, as a player would', flush=True)
            back_at = None
            advances += 1
            if advances > 3 or not advance(host):
                break
            continue
        time.sleep(3)
    shot(host, 'tribe_city_hall_race_host')
    print('  host: ' + host.call('coop.tribe graduation')['out'].strip(), flush=True)
    print(f'  editors asked for: {editors}, advances {advances}', flush=True)
    ck(vehicle_after_city_hall is True,
       'the game asks for its vehicle editor after the City Hall editor, with the planted cinematic playing at the '
       'graduation (BUG-1325)')
    ck('Civ' in mode(host), "the host's game reaches the Civ stage (%s)" % mode(host).strip())
    g = wait(lambda: 'Civ' in mode(guest), 300, 3)
    shot(guest, 'tribe_city_hall_race_guest')
    ck(g, "the guest's game follows into the Civ stage (%s)" % mode(guest).strip())
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (TimeoutError, OSError) as e:  # a game stopped answering (the fault storm of BUG-1325)
        print(f'  FAIL a game stopped answering its console ({e!r})', flush=True)
        print('RESULT: FAIL', flush=True)
        sys.exit(1)
