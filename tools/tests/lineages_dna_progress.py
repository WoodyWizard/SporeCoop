"""Targeted native DNA regression on an already joined disposable Creature pair.

The editor's native setter simulates its result; the negative AddEvolutionPoints
call exercises the same native debit as creature_rescue::charge. This does not
replay the editor UI or the death timer. --nofix must fail at shared spending.
"""
import argparse
import math
import re
import struct
import time
from duo_villages import Console, command
from duo_orders import wait_until
from coop_ability_bar import eax, mem, u32


def values(c):
    manager = eax(c, 'Simulator::cGameNounManager::Get cdecl')
    player = u32(c, manager + 0x74)
    earned = struct.unpack('<f', mem(c, player + 0x10F0, 4))[0]
    dna = float(re.search(r'dna=([-\d.]+)', command(c, 'coop.dna'))[1])
    return dna, earned


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', type=int, required=True)
    p.add_argument('--guest', type=int, required=True)
    p.add_argument('--nofix', action='store_true')
    a = p.parse_args()
    h, g = Console(a.host), Console(a.guest)
    for c in (h, g):
        assert 'duoqa' in command(c, 'coop.savedb').lower()
        assert 'Creature' in command(c, 'game.mode')
        assert 'total faults: 0' in command(c, 'exc')
    assert 'host port=' in command(h, 'coop.status')
    assert 'client id=' in command(g, 'coop.status')
    def matches():
        hv, gv = values(h), values(g)
        return all(abs(x-y) < .1 for x, y in zip(hv, gv))
    def set_budget(n):
        command(h, f'call Simulator::cCreatureGameData::SetEvolutionPoints cdecl f:{n}')
    try:
        command(g, 'coop.fix creature.bar_spend on')
        set_budget(100)
        wait_until(matches, 'Initial budget and progress did not converge', 12)
        if a.nofix:
            command(g, 'coop.fix creature.bar_spend off')
        before = values(h)
        set_budget(60)
        wait_until(lambda: abs(values(g)[0]-60) < .1, 'Shared spending never arrived', 10)
        print('editor debit host/guest:', values(h), values(g), flush=True)
        assert abs(values(h)[1]-before[1]) < .1, 'Native editor setter changed host progress'
        assert matches(), 'Editor spending reduced guest earned progress'
        print('PASS editor spending preserves shared earned progress', flush=True)
        command(h, 'coop.dna local 10')
        wait_until(matches, 'Earned gain failed to converge', 10)
        print('PASS gain host/guest:', values(h), values(g), flush=True)
        before = values(h)
        loss = min(math.floor(before[0]), math.ceil(before[0]*.10))
        command(h, f'coop.dna local {-loss}')
        wait_until(matches, 'Death debit failed to converge', 10)
        after = values(h)
        assert abs(after[0]-(before[0]-loss)) < .1 and abs(after[1]-(before[1]-loss)) < .1
        print('PASS shared penalty host/guest:', after, values(g), flush=True)
        time.sleep(2.5)
        assert matches(), 'Periodic resend changed earned progress'
        for c in (h, g):
            assert 'total faults: 0' in command(c, 'exc')
        print('PASS periodic resend and zero native faults', flush=True)
    finally:
        command(g, 'coop.fix creature.bar_spend on')


if __name__ == '__main__':
    main()
