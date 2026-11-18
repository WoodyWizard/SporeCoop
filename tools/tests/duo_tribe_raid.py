"""Native guest hut raid and live building-health replication on disposable Clyth copies."""
import argparse
import math
import re
import struct
import time

from coop_ability_bar import eax, mem, u32
from coop_ui import Ui
from duo_orders import inspect, wait_until
from duo_native_ui import require_gameplay_ui, view_tribal_point
from duo_tribe_rebuild_checkpoint import objects
from duo_tribe_rescue import citizens, move
from duo_villages import Console, command


def health(client, hut):
    return struct.unpack('<f', mem(client, hut + 0x158, 4))[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--target-control', action='store_true', help='Reproduce the home-sphere miss before enabling the ray correction.')
    parser.add_argument('--ai-control', action='store_true', help='Reproduce ignored native raids before enabling guest ownership.')
    parser.add_argument('--conquest', action='store_true', help='Continue the native raid through destruction and check campaign unlocks on both peers.')
    parser.add_argument('--target-species', help='Override the disposable fixture enemy species (hex).')
    args = parser.parse_args()
    host, guest = Console(args.host), Console(args.guest)
    for client in (host, guest):
        wait_until(lambda: client.call('coop.avatar')['out'].startswith('Tribe:'), 'Tribe not loaded', 60)
        if "world 'Clyth.spo'" not in command(client, 'coop.avatar'):
            raise RuntimeError('Use disposable Clyth copies')
        command(client, 'coop.uistate 0')
        require_gameplay_ui(client)
    command(host, 'coop.tribe village_orders on')
    # Registration must work even if the host has never selected this tribe.
    species = args.target_species.upper() if args.target_species else ('06785BC2' if args.conquest else '067C684A')
    rows = citizens(host)
    enemy = next(row for row in rows.values() if row['species'] == species and not row['dead'])
    village = u32(host, enemy['ptr'] + 0x1014)
    faction = u32(host, village + 0x30)
    hut = u32(host, village + 0x368)
    local_village = next(t for t, _ in objects(guest, 0x18C6D19) if u32(guest, t + 0x30) == faction)
    local_hut = u32(guest, local_village + 0x368)
    own = [i for i, row in rows.items() if row['species'] == '0680BF38' and not row['dead']]
    if len(own) != 3:
        raise RuntimeError('The fixture needs three guest villagers')
    if args.conquest:
        # These old save copies start with an empty guest food mat. Provision
        # the fixture so camera setup cannot starve the attackers beforehand.
        for tribe in {u32(host, row['ptr'] + 0x1014) for row in rows.values()
                      if row['species'] in ('067B3E4A', '0680BF38') and not row['dead']}:
            command(host, f'call SporeCoop::cTribe_AddFood thiscall {tribe:#x} 100')
    command(guest, 'coop.tribe select_village')
    ui = Ui(guest, 'guest')

    def unlocks(client):
        game = eax(client, 'SporeCoop::TribalGame_Get cdecl')
        return tuple(k for k in range(1, 10) if
                     eax(client, f'SporeCoop::TribalGame_IsToolUnlocked thiscall {game:#x} {k}') & 0xFF)

    prior_unlocks = unlocks(host)
    expected_segments = 1
    if args.conquest:
        home = next(t for t,_ in objects(host,0x18C6D19) if u32(host,t+0x30) == 0x800021CA)
        totem = u32(host,home+0x19D0)
        if not totem:
            raise RuntimeError('Native host campaign totem is missing before conquest')
        expected_segments += (u32(host,totem+0x120)-u32(host,totem+0x11C))//4

    def raid_click(x, y):
        command(guest, 'key f9')
        if not ui.wait(lambda state: state['open'] == '1', 10, 'F9'):
            raise RuntimeError('F9 did not open')
        if ui.state().get('page') != 'space':
            ui.click('space')
        if not ui.wait(lambda state: state.get('page') == 'space', 10, 'Village'):
            raise RuntimeError('Village page unavailable')
        if ui.state().get('sbtn1', {}).get('text') == 'Build':
            ui.click('sprev')
        if not ui.wait(lambda state: state.get('sbtn6', {}).get('text') == 'Raid', 10, 'Raid') or not ui.click('sbtn6'):
            raise RuntimeError('Raid button unavailable')
        if not ui.wait(lambda state: state['open'] == '0', 10, 'Raid'):
            raise RuntimeError('Raid button failed')
        command(guest, f'click {x} {y}')

    before = inspect(host)[1]
    local_home = next(t for t, _ in objects(guest, 0x18C6D19) if u32(guest, t + 0x30) == 0x800021CA)
    friendly_hut = u32(guest, local_home + 0x368)
    huts = dict(objects(guest, 0x1E4DAAE))
    friendly = view_tribal_point(guest, huts[friendly_hut])
    rejected = int(re.search(r'rejected=(\d+)', command(host, 'coop.tribe'))[1])
    raid_click(*friendly)
    wait_until(lambda: int(re.search(r'rejected=(\d+)', command(host, 'coop.tribe'))[1]) > rejected,
               'Host did not reject a friendly hut raid')
    if inspect(host)[1] != before:
        raise RuntimeError('Friendly raid produced native commands')
    print('PASS host refused a raid on the partner hut.', flush=True)
    x, y = view_tribal_point(guest, huts[local_hut])
    for i in own:
        move(host, rows[i]['ptr'], village + 0x124)
    command(guest, 'coop.tribe select_village')
    initial = health(host, hut)
    if args.conquest:
        # Shorten the fixture battle without causing defeat. The lethal strike
        # must still come from the ordered citizens' native AI.
        attacker = rows[own[0]]['ptr']
        command(host, f'call SporeCoop::cTribeHut_TakeDamage thiscall {hut+0x120:#x} '
                      f'f:{initial-1} 0x21cb 0 {attacker+0xC4:#x} {attacker+0x5A8:#x}')
        initial = health(host, hut)
        if initial <= 0 or mem(host, village+0x556, 1) != b'\0':
            raise RuntimeError('Conquest fixture caused defeat before the native raid')
    def stop_raiders():
        current = citizens(host)
        for i in own:
            row = current.get(i)
            if not row or row['species'] != '0680BF38' or row['dead'] or row['hp'] <= 0:
                continue
            p = row['ptr']
            owner = u32(host, p + 0x1014)
            if not owner:
                continue
            point = owner + 0x124
            rec = eax(host, f'Simulator::cCreatureCitizen::DoAction thiscall {p:#x} 4 0 0')
            if rec:
                command(host, f'call App::Property::SetValueVector3 thiscall {rec+0x39:#x} {point:#x}')
            move(host, p, point)

    def check_host_distant():
        target = struct.unpack('<3f', mem(host, village + 0x124, 12))
        if any(math.dist(r['pos'], target) < 60 for r in citizens(host).values()
               if r['species'] == '067B3E4A' and not r['dead']):
            raise RuntimeError('Host citizens could confound guest raid damage')

    check_host_distant()
    if args.target_control:
        command(guest, 'coop.fix tribe.target_ray off')
        raid_click(x, y)
        command(guest, 'wait.frames 30 5000')
        if inspect(host)[1] != before:
            raise RuntimeError('The old targeting failure did not reproduce at this camera position')
        print('FAIL control reproduced: the old home-sphere picker ignored the visible enemy hut.', flush=True)
        command(guest, 'coop.fix tribe.target_ray on')
    try:
        if args.ai_control:
            command(host, 'coop.fix tribe.guest_ai off')
            raid_click(x, y)
            wait_until(lambda: inspect(host)[1] == before + 3, 'Control raid orders missing')
            for _ in range(12):
                check_host_distant()
                if health(host, hut) != initial:
                    raise RuntimeError('Native ownership failure did not reproduce')
                time.sleep(1)
            print('FAIL control reproduced: native guest orders did not damage the hut with guest ownership disabled.', flush=True)
            stop_raiders()
            before = inspect(host)[1]
            for i in own:
                move(host, rows[i]['ptr'], village + 0x124)
            command(host, 'coop.fix tribe.guest_ai on')
        raid_click(x, y)
        wait_until(lambda: inspect(host)[1] == before + 3, 'Raid did not execute three orders')
        wait_until(lambda: health(host, hut) < initial - 0.5, 'Native raid caused no hut damage', 30)
        check_host_distant()
        wait_until(lambda: abs(health(host, hut) - health(guest, local_hut)) < 0.1 and health(guest, local_hut) < initial - 0.5,
                   'Guest did not receive the host hut health', 10)
        if args.conquest:
            wait_until(lambda: mem(host, village+0x556, 1) != b'\0', 'Native guest raid did not defeat the village', 45)
            stop_raiders()
            current = wait_until(lambda: unlocks(host) if set(unlocks(host)) > set(prior_unlocks) else None,
                                 'Guest conquest did not advance host native tool unlocks', 20)
            print(f'PASS guest native conquest awarded host unlocks {prior_unlocks} -> {current}.', flush=True)
            # The native victory cinematic waits for the host's Continue click.
            # The production save correctly waits until that cinematic finishes.
            wait_until(lambda: host.call('coop.ui find You have destroyed')['ok'],
                       'Native conquest result caption did not appear', 20)
            def acknowledge_result():
                if not re.search(r'^cinematic [0-9A-F]+ playing', command(host, 'coop.status'), re.M):
                    return True
                if not any(host.call('coop.ui find ' + text)['ok'] for text in
                           ('You have destroyed', 'Your tribe grows in prestige')):
                    return False
                buttons = command(host, 'coop.ui where 062EF353 0445C99D')
                button = re.search(r'^(?:062EF353|0445C99D) ([\d.]+) ([\d.]+) ([\d.]+) ([\d.]+) enabled=1',
                                   buttons, re.M)
                if button:
                    x1, y1, x2, y2 = map(float, button.groups())
                    command(host, f'click {round((x1+x2)/2)} {round((y1+y2)/2)}')
                    time.sleep(.5)
                return False
            wait_until(acknowledge_result, 'Native conquest/upgrade result did not close', 30)
            def guest_current():
                # A second upgrade cinematic may start after the first closes.
                acknowledge_result()
                return guest.call('coop.avatar')['out'].startswith('Tribe:') and unlocks(guest) == current
            wait_until(guest_current, 'Guest native campaign unlocks disagree with host after transfer', 90)
            # Save/load clears the transient defeated byte. Verify the persistent
            # native hut destruction, earned totem segment and tool permission.
            for client in (host, guest):
                tribes = [t for t,_ in objects(client, 0x18C6D19)]
                conquered = next(t for t in tribes if u32(client,t+0x30) == faction)
                home = next(t for t in tribes if u32(client,t+0x30) == 0x800021CA)
                totem = u32(client,home+0x19D0)
                if health(client,u32(client,conquered+0x368)) > 0 or not totem:
                    raise RuntimeError('Conquest transfer lost native hut destruction or the campaign totem')
                segments = (u32(client,totem+0x120)-u32(client,totem+0x11C))//4
                if segments != expected_segments:
                    raise RuntimeError(f'Expected {expected_segments} earned native conquest segments, got {segments}')
            print('PASS both native worlds retain the ruined hut, earned totem segment and matching unlocks.', flush=True)
    finally:
        command(host, 'coop.fix tribe.guest_ai on')
        stop_raiders()
    for client in (host, guest):
        if 'total faults: 0' not in command(client, 'exc'):
            raise RuntimeError('Native faults occurred')
    print(f'PASS native guest raid damaged enemy hut {faction:08X}; both health values agree and friendly raid was rejected.', flush=True)


if __name__ == '__main__':
    main()
