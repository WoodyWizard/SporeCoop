#!/usr/bin/env python3
"""Civilization: meeting a civilization the guest's game does not have shows no card and does not
crash (BUG-0492).

0.7.3.6: a guest crashed at UTFWin::IImageDrawable::SetImageForWindow+0x21F (read 0xC) under the city
click handler: a first click on a city calls the game's "meet that civilization" (0xC746F0), whose card
data (0xBA5B60) is null when the city's political id names no civilization in that game, and the card
(0xE30CF0) reads data+0xC.

1. Two games on civ_npc.
2. Guest: its game meets the civilization of an NPC city it has (`coop.civ discover <pol>`, the
   game's own function a first click calls): the card data exists, no crash.
3. Guest: its game meets a political id no civilization of it has (the state of a city whose owner
   the guest's game does not know): no card, no crash, the game goes on (a console answer, frames).
PASS: 2 and 3, no fault or crash on either game.
SPORE_FIX_OFF=civ.discovery_card: step 3 crashes the guest (the 0.7.3.6 signature).

  SPORE_LANE=2 python3 tools/tests/coop_civ_discovery.py
"""
import re
import time

from play_civ import civ_state
from twoinst import Checks, host_frame, start


def main():
    host, guest = start('civ_npc')
    ck = Checks()
    time.sleep(3)
    gc = civ_state(guest)[0]
    npc = sorted({c['pol'] for c in gc.values() if not c['player']})
    ck(npc, f'the guest has NPC civilizations ({npc})')
    def meet(pol):
        try:
            out = guest.call(f'coop.civ discover {pol}')['out'].strip()
            time.sleep(3)
            st = [ln for ln in guest.call('coop.civ')['out'].splitlines() if ln.startswith('planner')]
            alive = host_frame(guest) > 0
        except Exception as e:  # the game died
            return f'no answer ({e})', '', False
        return out, st[0] if st else '', alive

    if npc:
        out, st, alive = meet(npc[0])
        print('  guest meets an NPC civilization:', out, '|', st, flush=True)
        ck(alive and 'civ=0' not in out and 'civ=(nil)' not in out and re.search(r'met=[01]\b', st),
           'the guest\'s game met a civilization it has (the game\'s own path, no crash)')
    out, st, alive = meet('7EADBEEF')
    print('  guest meets a political id it has no civilization for:', out, '|', st, flush=True)
    ck(alive and re.search(r'met=[01]\b', st), 'the guest\'s game goes on after meeting a civilization it does not have')
    ck(alive and 'cards_skipped=0 ' not in st, 'the card without data was not made')
    try:
        ck.faults(host=host, guest=guest)
    except ConnectionError as e:
        ck(False, f'a game answers after the test ({e})')
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
