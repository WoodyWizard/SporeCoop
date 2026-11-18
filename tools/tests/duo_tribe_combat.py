"""Guest village attacks through F9; the host rejects friendly and mistyped targets.

Use disposable registered Clyth copies. Native relocation arranges the fixture;
damage must come from the game's AI executing the guest's networked order.
"""
import argparse
import math
import re

from coop_ability_bar import u32
from coop_ui import Ui
from duo_orders import inspect, wait_until
from duo_native_ui import require_gameplay_ui
from duo_tribe_rescue import citizens, move
from duo_villages import Console, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    args = parser.parse_args()
    host, guest = Console(args.host), Console(args.guest)
    for client in (host, guest):
        wait_until(lambda: client.call('coop.avatar')['out'].startswith('Tribe:'), 'Tribe not loaded', 60)
        if "world 'Clyth.spo'" not in command(client, 'coop.avatar'):
            raise RuntimeError('Use disposable registered Clyth copies.')
        command(client, 'coop.uistate 0')
        require_gameplay_ui(client)
    command(host, 'coop.tribe village_orders on')
    rows = citizens(host)
    own = [i for i, row in rows.items() if row['species'] == '0680BF38' and not row['dead']]
    friends = [i for i, row in rows.items() if row['species'] == '067B3E4A' and not row['dead']]
    enemy = next(i for i, row in rows.items() if row['species'] == '06785BC2' and not row['dead'])
    if len(own) != 3 or not friends:
        raise RuntimeError('The fixture requires three guest villagers and a living host.')
    home = u32(host, rows[friends[0]]['ptr'] + 0x1014)
    other = u32(host, rows[own[0]]['ptr'] + 0x1014)
    for i in friends:
        move(host, rows[i]['ptr'], other + 0x124)
    for i in own:
        move(host, rows[i]['ptr'], home + 0x124)
    move(host, rows[enemy]['ptr'], home + 0x124)
    wait_until(lambda: enemy in citizens(guest) and
               math.dist(citizens(guest)[enemy]['pos'], citizens(host)[enemy]['pos']) < 4,
               'Enemy mirror did not reach the fixture site')
    ui = Ui(guest, 'guest')

    def page():
        command(guest, 'key f9')
        if not ui.wait(lambda state: state['open'] == '1', 10, 'F9'):
            raise RuntimeError('F9 did not open')
        if ui.state().get('page') != 'space':
            ui.click('space')
        if not ui.wait(lambda state: state.get('page') == 'space', 10, 'Village page'):
            raise RuntimeError('Village page did not open')

    page()
    if not ui.click('sbtn5') or not ui.wait(lambda state: state['open'] == '0', 10, 'Select'):
        raise RuntimeError('Select button failed')
    wait_until(lambda: len(re.findall(r'owner=1 age=\d+ sel=1', command(guest, 'coop.tribe'))) == 3,
               'Three villagers were not selected', 3)
    before = inspect(host)[1]
    rejected = int(re.search(r'rejected=(\d+)', command(host, 'coop.tribe'))[1])
    command(guest, f'coop.tribe act 7 {friends[0]}')
    wait_until(lambda: int(re.search(r'rejected=(\d+)', command(host, 'coop.tribe'))[1]) > rejected,
               'Host accepted a friendly target')
    command(guest, f'coop.tribe act 7 {own[0]}')
    command(guest, f'coop.tribe act 15 {enemy}')
    command(guest, 'wait.frames 20 5000')
    if inspect(host)[1] != before:
        raise RuntimeError('Invalid targets produced native orders')
    page()
    if not ui.click('sbtn4') or not ui.wait(lambda state: state['open'] == '0', 10, 'Attack'):
        raise RuntimeError('Attack button failed')
    targets = command(guest, 'coop.targets objs')
    point = re.search(rf'^obj t{enemy} [\d.-]+ (\d+) (\d+)$', targets, re.M)
    if not point:
        raise RuntimeError(f'Fixture enemy is not visible: {targets}')
    initial = citizens(host)[enemy]['hp']
    command(guest, f'click {point[1]} {point[2]}')
    wait_until(lambda: inspect(host)[1] == before + 3, 'Attack click did not execute three native orders')

    def hurt(client):
        actual = citizens(host)
        if enemy in actual and any(math.dist(actual[i]['pos'], actual[enemy]['pos']) < 40
                                   for i in friends if i in actual and not actual[i]['dead']):
            raise RuntimeError('Host villagers could confound guest combat damage')
        row = citizens(client).get(enemy)
        return row is None or row['dead'] or row['hp'] < initial - 0.5

    for client in (host, guest):
        wait_until(lambda: hurt(client), 'Native fighting did not reduce enemy health on both peers', 30)
        if 'total faults: 0' not in command(client, 'exc'):
            raise RuntimeError('Native faults occurred')
    command(guest, f'click {point[1]} {point[2]}')
    command(guest, 'wait.frames 20 5000')
    if inspect(host)[1] != before + 3:
        raise RuntimeError('A second click repeated the one-shot attack')
    print('PASS F9 selection and attack caused native enemy damage on both peers; friendly/mistyped orders were refused.', flush=True)


if __name__ == '__main__':
    main()
