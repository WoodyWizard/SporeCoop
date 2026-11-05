#!/usr/bin/env python3
"""Cell: the host's editor opened after the evolution vote gets the stage's DNA budget, and the guest's Cell DNA
counts what the host's editor spent (BUG-0560).

Player report (0.7.5): the host's log said "DNA at the editor request: ... budget=25" while its Cell editor showed
45, and every editor visit gave a lot of DNA again. The Cell request (0xE84600) submits and then posts the budget
(message 0x90A03FDF); the editor's listener exists only once the editor entered. With two players the request waits
for the vote (h_Submit holds it), the game's post goes out at once with no listener, and the editor opens with its
config's start budget (100); on the way out the game books spent = budget - remaining, a negative spent
(docs/re/cell.md#cell-editor-budget).

1. Cell fixture. The host's mating call, the vote, the guest is ready, the host's editor opens.
2. The host moves an eye (free) and saves (the editors' check). PASS: the host's game's spent DNA
   (cCellSerializableData +0x2C) is what it was before (the editor got the stage's budget and nothing was bought),
   and within 10 s the guest's game has the host's spent DNA (wsync row cell_spent).

  SPORE_LANE=4 python3 tools/tests/coop_cell_editor_budget.py
"""
import re
import time

from coop_editor_desync import READY, edit_cell, mating_call, mode, wait
from twoinst import Checks, start


def spent(con):
    m = re.search(r'cell_spent spent=(-?\d+) budget=(-?\d+)', con.call('coop.wsync dump cell_spent')['out'])
    return (int(m.group(1)), int(m.group(2))) if m else None


def main():
    host, guest = start('cell', guest_creations=False)
    ck = Checks()
    s0 = spent(host)
    print(f'  before: host spent/budget {s0}, guest {spent(guest)}', flush=True)
    ck(s0 is not None, 'the host is in Cell with its DNA record')
    host_vote = lambda: 'vote active' in host.call('coop.vote')['out'] or 'Editor' in mode(host)
    alive, reached = mating_call(host, 'host', host_vote)
    ck(reached, 'the host reaches its mate: the evolution vote starts')
    wait(lambda: guest.call('coop.uistate')['out'], 10)
    guest.call('click %d %d' % READY)
    ck(wait(lambda: 'Editor' in mode(host), 30), 'the host editor opens after the vote')
    time.sleep(4)
    edit_cell(host, 'Budgetcell')
    ck(wait(lambda: 'Cell' in mode(host), 30), 'the host is back in the pool')
    time.sleep(3)
    s1 = spent(host)
    print(f'  after the editor: host spent/budget {s1}', flush=True)
    ck(s0 and s1 and s1[0] == s0[0],
       f'the host\'s editor had the stage\'s budget: its spent DNA is unchanged after a free edit ({s0} -> {s1})')
    g = wait(lambda: spent(guest) and s1 and spent(guest)[0] == s1[0] and spent(guest), 20)
    print(f'  guest spent/budget {spent(guest)}', flush=True)
    ck(bool(g), f'the guest\'s game has the host\'s spent DNA ({s1} / {spent(guest)})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
