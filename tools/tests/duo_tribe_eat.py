"""F9 Eat consumes native guest-village food and restores a citizen on both games."""
import argparse
import math
import re
import struct
import time

from coop_ability_bar import eax, mem, u32
from coop_ui import Ui
from duo_food import food
from duo_orders import wait_until
from duo_tribe_rescue import citizens, move
from duo_villages import Console, command


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', type=int, required=True)
    p.add_argument('--guest', type=int, required=True)
    p.add_argument('--control', action='store_true')
    args = p.parse_args()
    h, g = Console(args.host), Console(args.guest)
    for c in (h, g):
        if "world 'Clyth.spo'" not in command(c, 'coop.avatar'):
            raise RuntimeError('Use disposable Clyth profiles')
        command(c, 'coop.uistate 0')
    command(h, 'coop.tribe village_orders on')
    rows = citizens(h)
    identity, actor = next((i, r) for i, r in rows.items() if r['species'] == '0680BF38'
                           and not r['dead'] and u32(h, r['ptr'] + 0x102C) != 11)
    body = actor['ptr']
    owner = u32(h, body + 0x1014)
    mat = u32(h, owner + 0x260)
    if u32(h, mat + 0x10C) != owner:
        raise RuntimeError('Native food-mat owner assumption disagrees with this build')
    command(h, f'call SporeCoop::cTribe_AddFood thiscall {owner:#x} 100')
    move(h, body, mat + 0x38)
    rec = eax(h, f'Simulator::cCreatureCitizen::DoAction thiscall {body:#x} 4 0 0')
    if rec: command(h, f'call App::Property::SetValueVector3 thiscall {rec+0x39:#x} {body+0xC4:#x}')
    for i in citizens(g): command(g, f'coop.tribe select {i} {int(i == identity)}')
    maximum = struct.unpack('<f', mem(h, body + 0x5D4, 4))[0]
    initial = maximum * .35
    command(h, f'call Simulator::cCombatant::SetHealth thiscall {body+0x5A8:#x} f:{initial}')
    wait_until(lambda: abs(citizens(g)[identity]['hp'] - initial) < 1, 'Injury did not replicate')
    ui = Ui(g, 'guest')
    def eat():
        command(g, 'key f9')
        if not ui.wait(lambda s: s['open'] == '1', 5, 'F9'): raise RuntimeError('F9 unavailable')
        if ui.state().get('page') != 'space': ui.click('space')
        wait_until(lambda: ui.state().get('page') == 'space', 'Village page unavailable')
        # Eat is on the first Village page, after Heal.
        for _ in range(4):
            state = ui.state()
            key = next((k for k, v in state.items() if k.startswith('sbtn') and isinstance(v, dict)
                        and v.get('vis') and v.get('text') == 'Eat'), None)
            if key:
                if not ui.click(key): raise RuntimeError('Eat click failed')
                if not ui.wait(lambda s: s['open'] == '0', 5, 'Eat'): raise RuntimeError('Eat did not close F9')
                return
            ui.click('sprev')
            time.sleep(.15)
        raise RuntimeError('F9 Eat not found')
    if args.control:
        command(h, 'coop.fix tribe.eat_orders off')
        before = int(re.search(r'rejected=(\d+)', command(h, 'coop.tribe'))[1])
        eat()
        wait_until(lambda: int(re.search(r'rejected=(\d+)', command(h, 'coop.tribe'))[1]) > before,
                   'Old eating rejection did not reproduce')
        print('FAIL control reproduced: F9 Eat rejected while the native food mat has food.', flush=True)
        command(h, 'coop.fix tribe.eat_orders on')
    before = food(h)['0680BF38']
    eat()
    saw_eating = False
    def recovered():
        nonlocal saw_eating
        current = citizens(h)[identity]
        ai = u32(h, current['ptr'] + 0xB4C)
        saw_eating |= bool(ai and u32(h, ai + 0x1D8) == 0x0461300F)
        return saw_eating and current['hp'] > initial + maximum * .15 and food(h)['0680BF38'] < before
    wait_until(recovered, 'Native eating did not consume food and restore health', 25)
    wait_until(lambda: abs(citizens(h)[identity]['hp'] - citizens(g)[identity]['hp']) < 1
               and food(h)['0680BF38'] == food(g)['0680BF38'], 'Food or recovery differs on guest', 10)
    current = citizens(h)[identity]['ptr']
    village = u32(h, current + 0x1014)
    command(h, f'call SporeCoop::cTribe_AddFood thiscall {village:#x} {-math.ceil(food(h)["0680BF38"])}')
    wait_until(lambda: food(h)['0680BF38'] == food(g)['0680BF38'] == 0, 'Empty food stores differ')
    rejected = int(re.search(r'rejected=(\d+)', command(h, 'coop.tribe'))[1])
    eat()
    wait_until(lambda: int(re.search(r'rejected=(\d+)', command(h, 'coop.tribe'))[1]) > rejected,
               'Empty food-mat eating was not rejected')
    print('PASS empty native food store rejects a further Eat request.', flush=True)
    for c in (h, g):
        if citizens(c)[identity]['species'] != '0680BF38': raise RuntimeError('Eating changed species')
        if 'total faults: 0' not in command(c, 'exc'): raise RuntimeError('Native fault occurred')
    print('PASS F9 Eat runs native eating, pays village food and restores health on both peers.', flush=True)


if __name__ == '__main__': main()
