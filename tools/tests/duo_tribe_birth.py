"""Guest F9 birth purchase: own food, native cap, and original-species baby on both peers."""
import argparse
import math
import re
import struct

from coop_ability_bar import eax, mem, sym, u32
from coop_ui import Ui
from duo_food import food
from duo_native_ui import require_gameplay_ui
from duo_orders import inspect, wait_until
from duo_tribe_rescue import citizens, move
from duo_villages import Console, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    args = parser.parse_args()
    h, g = Console(args.host), Console(args.guest)
    own, partner = '0680BF38', '067B3E4A'
    for c in (h, g):
        if "world 'Clyth.spo'" not in command(c, 'coop.avatar'):
            raise RuntimeError('Use disposable Clyth copies')
        require_gameplay_ui(c)
    command(h, 'coop.tribe village_orders on')
    row = next(r for r in citizens(h).values() if r['species'] == own and not r['dead'])
    village = u32(h, row['ptr'] + 0x1014)
    cost = struct.unpack('<f', mem(h, sym(h, 'SporeCoop::Tribe_BabyFoodCost'), 4))[0]
    if cost != 10:
        raise RuntimeError('This stock fixture test expects the native ten-food cost')
    ui = Ui(g, 'guest')

    def count(c):
        return {i: r for i, r in citizens(c).items() if r['species'] == own and not r['dead']}

    def balance(value):
        current = food(h)[own]
        command(h, f'call SporeCoop::cTribe_AddFood thiscall {village:#x} {round(value-current)}')
        wait_until(lambda: abs(food(h)[own]-value) < .01 and abs(food(g)[own]-value) < .01,
                   'Fixture food did not synchronize')

    def buy():
        command(g, 'key f9')
        if not ui.wait(lambda state: state['open'] == '1', 5, 'F9'):
            raise RuntimeError('F9 unavailable')
        if ui.state().get('page') != 'space':
            ui.click('space')
        if not ui.wait(lambda state: state.get('page') == 'space', 5, 'Village') or not ui.click('sbtn7'):
            raise RuntimeError('Add baby button unavailable')
        if not ui.wait(lambda state: state['open'] == '0', 5, 'Birth'):
            raise RuntimeError('Birth button did not close')

    initial = count(h)
    if len(initial) != 3:
        raise RuntimeError('Fixture needs three guest citizens')
    balance(0)
    rejected = int(re.search(r'rejected=(\d+)', command(h, 'coop.tribe'))[1])
    buy()
    wait_until(lambda: int(re.search(r'rejected=(\d+)', command(h, 'coop.tribe'))[1]) > rejected,
               'Unaffordable birth was not refused')
    if len(count(h)) != 3 or food(h)[own] != 0:
        raise RuntimeError('Unaffordable birth mutated the village')
    print('PASS unaffordable guest birth refused.', flush=True)
    balance(31)
    # Keep the host away from its food mat so normal eating cannot look like a
    # purchase charge. This arranges the fixture, not the purchase outcome.
    for r in citizens(h).values():
        if r['species'] != partner or r['dead']:
            continue
        p = r['ptr']
        rec = eax(h, f'Simulator::cCreatureCitizen::DoAction thiscall {p:#x} 4 0 0')
        if rec:
            command(h, f'call App::Property::SetValueVector3 thiscall {rec+0x39:#x} {village+0x124:#x}')
        move(h, p, village + 0x124)
    host_food = food(h)[partner]
    buy()
    wait_until(lambda: len(count(h)) == 4 and len(count(g)) == 4, 'Baby missing on a peer')
    new = next(iter(set(count(h)) - set(initial)))
    for c in (h, g):
        if u32(c, count(c)[new]['ptr'] + 0xB34) != 0:
            raise RuntimeError('Purchase did not create a native baby')
    wait_until(lambda: abs(food(h)[own]-21) < .01 and abs(food(g)[own]-21) < .01,
               'Guest birth did not charge its own food exactly once')
    if food(h)[partner] != host_food:
        raise RuntimeError(f'Partner food changed during guest purchase: {host_food} -> {food(h)[partner]}')
    for total in (5, 6):
        buy()
        wait_until(lambda: len(count(h)) == total and len(count(g)) == total, 'Native population did not grow')
    balance(20)
    rejected = int(re.search(r'rejected=(\d+)', command(h, 'coop.tribe'))[1])
    buy()
    wait_until(lambda: int(re.search(r'rejected=(\d+)', command(h, 'coop.tribe'))[1]) > rejected,
               'Native population cap not enforced')
    if len(count(h)) != 6 or food(h)[own] != 20:
        raise RuntimeError('Capped birth spent food or created an extra citizen')
    for c in (h, g):
        if 'total faults: 0' not in command(c, 'exc'):
            raise RuntimeError('Native faults occurred')
    print('PASS F9 births preserve species, charge guest food once, mirror native babies and refuse the native cap without charging.', flush=True)


if __name__ == '__main__':
    main()
