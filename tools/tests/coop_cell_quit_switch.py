#!/usr/bin/env python3
"""The game's own Cell -> Creature switch while the mod leaves Cell for the galaxy menu (BUG-1436).

A player report (coop 0.7.15.5, Steam): a guest's game left Cell for the galaxy menu to load the host's Creature world
(the mod's quit, message 0x4715068) and crashed 67 ms after the menu came up: the game entered the Creature stage from
the galaxy menu without a loaded game (OnModeEnter GalaxyGameEntry -> CreatureGame, posted by the App state's `mode`
command) and its home planet setup read the null cPlayer (SporeCoop::Player_SetProperty+0x14D, read 0x1188). The only
path that queues that command in Cell is the game's own switch (SetMode 0x1654C01, the state CreatureGameFromCellGame;
docs/re/space.md#space-menu-creature-switch); a guest's own switch passes the mod when the host is no longer in its
world, as it was then.

1. Cell fixture; the guest leaves, the host plays alone (the switch is the game's own, no vote).
2. `coop.cellswitch quit`: the mod's quit to the galaxy menu, and the game's switch (CellStage_ToCreature through the
   mod's hook, as History's check or the end sequence calls it) right after the game handled the quit.
3. PASS: the game reaches the galaxy menu (game.mode GalaxyGameEntry), never the Creature stage, and is still up 20 s
   later without a crash report; the mod says the switch was not taken (key frame: the galaxy menu).
   SPORE_FIX_OFF=world.quit_no_switch (cell_quit_switch_off): the switch goes through: the game ends in Creature from
   the menu without a game (or crashes as in the report): FAIL. If the twin passes, the report's trigger was another one
   (BUG-1436 stays open).

  SPORE_LANE=4 python3 tools/tests/coop_cell_quit_switch.py
"""
import time

from harness import mode, waiter
from twoinst import Checks, keyframe, start

wait = waiter(step=0.5)


def main():
    host, guest = start('cell')
    ck = Checks()
    guest.call('coop.leave')
    time.sleep(3)
    print('  host:', host.call('coop.cellswitch quit')['out'].strip(), flush=True)
    seen = set()
    t0 = time.time()
    while time.time() - t0 < 60:
        m = mode(host).strip()
        seen.add(m.split()[0] if m else '')
        if 'GalaxyGameEntry' in m or 'Creature' in m:
            break
        time.sleep(0.25)
    t1 = time.time()
    while time.time() - t1 < 20:  # the report's crash came 67 ms after the menu; a switch would show within seconds
        m = mode(host).strip()
        seen.add(m.split()[0] if m else '')
        time.sleep(0.25)
    print('  host modes seen:', sorted(seen), flush=True)
    final = mode(host).strip()
    ck('GalaxyGameEntry' in final, f"the host's game is in the galaxy menu after the quit ({final or 'no answer'})")
    ck(not any(s.startswith('Creature') for s in seen), "the host's game never entered the Creature stage from the menu")
    st = [ln for ln in host.call('coop.cellswitch').get('out', '').splitlines()]
    print('  host:', st, flush=True)
    ck(any('dropped 1' in ln or 'dropped so far 1' in ln for ln in st),  # counter: the path ran; the game's mode is checked above
       "the mod did not take the game's switch")
    if 'GalaxyGameEntry' in final:
        keyframe('cell_quit_switch', "the host's galaxy menu after its quit from Cell: no Creature stage, no crash",
                 players={'host': host}, settle=0)
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
