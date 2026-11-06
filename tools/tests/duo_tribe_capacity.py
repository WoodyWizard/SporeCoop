"""An earned campaign upgrade expands both founders' actual native birth limits."""
import argparse
import re

from coop_ability_bar import eax, u32
from coop_ui import Ui
from duo_food import food
from duo_native_ui import require_gameplay_ui
from duo_orders import wait_until
from duo_tribe_rescue import citizens
from duo_villages import Console, command


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', type=int, required=True)
    p.add_argument('--guest', type=int, required=True)
    p.add_argument('--control', action='store_true')
    args = p.parse_args()
    h, g = Console(args.host), Console(args.guest)
    own, partner = '0680BF38', '067B3E4A'
    for c in (h, g):
        if "world 'Clyth.spo'" not in command(c, 'coop.avatar'):
            raise RuntimeError('Use disposable earned-upgrade Clyth profiles')
        require_gameplay_ui(c)
    command(h, 'coop.tribe village_orders on')

    def tribes(c):
        return {r['species']: u32(c, r['ptr'] + 0x1014) for r in citizens(c).values()}

    def capacities(c):
        return {s: eax(c, f'SporeCoop::Tribe_MaxPopulation thiscall {t:#x}') for s, t in tribes(c).items()}

    def count(c):
        return sum(r['species'] == own and not r['dead'] for r in citizens(c).values())

    if args.control:
        for c in (h, g): command(c, 'coop.fix tribe.shared_capacity off')
        before_h, before_g = capacities(h), capacities(g)
        if before_h[partner] != 9 or before_h[own] != 6 or before_g[own] != 12:
            raise RuntimeError(f'Old native mismatch did not reproduce: {before_h}, {before_g}')
        print('FAIL control reproduced: host campaign9, host guest6, client guest12.', flush=True)
        for c in (h, g): command(c, 'coop.fix tribe.shared_capacity on')
    else:
        before_h, before_g = capacities(h), capacities(g)
    for c, old in ((h, before_h), (g, before_g)):
        current = capacities(c)
        if current[own] != current[partner] or current[own] != 9:
            raise RuntimeError(f'Founders disagree on shared earned capacity: {current}')
        if any(current[s] != cap for s, cap in old.items() if s not in (own, partner)):
            raise RuntimeError('An unrelated NPC population limit changed')
    village = tribes(h)[own]
    command(h, f'call SporeCoop::cTribe_AddFood thiscall {village:#x} 100')
    ui = Ui(g, 'guest')

    def buy():
        command(g, 'key f9')
        if not ui.wait(lambda s: s['open'] == '1', 5, 'F9'):
            raise RuntimeError('F9 unavailable')
        if ui.state().get('page') != 'space': ui.click('space')
        wait_until(lambda: ui.state().get('page') == 'space', 'Village page unavailable')
        for _ in range(3): ui.click('sprev')
        key = next((k for k, v in ui.state().items() if k.startswith('sbtn') and isinstance(v, dict)
                    and v.get('vis') and v.get('text') == 'Add baby'), None)
        if not key or not ui.click(key) or not ui.wait(lambda s: s['open'] == '0', 5, 'Birth'):
            raise RuntimeError('F9 Add baby unavailable')

    initial = count(h)
    if initial != 6:
        raise RuntimeError('Use the earned alliance checkpoint with six guest citizens')
    for total in range(initial + 1, 10):
        buy()
        wait_until(lambda: count(h) == count(g) == total, 'Shared upgrade did not allow the native birth')
    rejected = lambda: int(re.search(r'rejected=(\d+)', command(h, 'coop.tribe'))[1])
    before, balance = rejected(), food(h)[own]
    buy()
    wait_until(lambda: rejected() > before, 'Birth beyond the shared native limit was not rejected')
    if count(h) != count(g) or count(h) != 9 or food(h)[own] != balance:
        raise RuntimeError('Capped birth changed population or charged food')
    for c in (h, g):
        if 'total faults: 0' not in command(c, 'exc'): raise RuntimeError('Native fault occurred')
    print('PASS shared native capacity9 on both peers; F9 births7/8/9 work, birth10 refused without charge.', flush=True)


if __name__ == '__main__': main()
