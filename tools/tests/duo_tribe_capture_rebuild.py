"""Native enemy defeat -> capture -> helper rebuild on disposable Clyth copies.

Lethal enemy damage and initial helper separation are fixture setup. Capture,
checkpoint refresh, rebuilding through F9, and control restoration use the mod.
Both instances must already be connected on the registered Clyth campaign.
"""
import argparse
import json
import math
from pathlib import Path
import struct
import subprocess
import sys

from coop_ability_bar import mem, u32
from coop_ui import Ui
from duo_orders import wait_until
from duo_tribe_rebuild_checkpoint import objects, nearest
from duo_tribe_rescue import citizens, move
from duo_villages import Console, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--victim', choices=('host', 'guest'), required=True)
    parser.add_argument('--record', type=Path, required=True)
    parser.add_argument('--hut', action='store_true', help='Start village defeat with lethal hut damage instead of citizen damage.')
    parser.add_argument('--click-control', action='store_true', help='Require the native missing-click control, then enable BUG-9018 recovery.')
    parser.add_argument('--conqueror', type=lambda value: int(value, 16), default=0x21E0)
    parser.add_argument('--campaign', action='store_true', help='Require preservation of an earned native totem and hut upgrade.')
    args = parser.parse_args()
    host, guest = Console(args.host), Console(args.guest)
    own, other = ('067B3E4A', '0680BF38') if args.victim == 'host' else ('0680BF38', '067B3E4A')
    faction = 0x800021CA if args.victim == 'host' else 0x21CB
    controller = guest if args.victim == 'host' else host
    for client in (host, guest):
        wait_until(lambda: client.call('coop.avatar')['out'].startswith('Tribe:'), 'Tribe not loaded', 60)
        if "world 'Clyth.spo'" not in command(client, 'coop.avatar'):
            raise RuntimeError('Only disposable registered Clyth copies are supported.')
        command(client, 'coop.uistate 0')
    command(host, 'coop.tribe village_orders on')
    rows = citizens(host)
    victims = [r for r in rows.values() if r['species'] == own and not r['dead']]
    if not victims:
        raise RuntimeError('The victim needs a living population at test start.')
    defeated = u32(host, victims[0]['ptr'] + 0x1014)
    campaign = None
    if args.campaign:
        if args.victim != 'host':
            raise RuntimeError('The native shared campaign totem belongs to the host founder')
        totem = u32(host, defeated + 0x19D0)
        if not totem:
            raise RuntimeError('Campaign fixture has no native totem')
        begin, end = u32(host, totem + 0x11C), u32(host, totem + 0x120)
        if not begin or not 0 < end-begin <= 20:
            raise RuntimeError('Campaign fixture requires earned totem segments')
        colors, colors_end = u32(host,totem+0x130), u32(host,totem+0x134)
        if colors_end-colors != (end-begin)*3:
            raise RuntimeError('Campaign fixture has inconsistent native segment colors')
        campaign = dict(segments=list(mem(host, begin, end-begin)),
                        colors=list(mem(host,colors,colors_end-colors)),
                        upgrade=u32(host, defeated+0x38),
                        hut_max_health=u32(host,u32(host,defeated+0x368)+0x14C))
    if u32(host, defeated + 0x30) != faction:
        raise RuntimeError('Unexpected founder faction.')
    record = dict(old_pos=struct.unpack('<3f', mem(host, defeated + 0x124, 12)),
                  old_native_id=u32(host, defeated + 0x24), founder_faction=faction, conqueror_faction=args.conqueror)
    if campaign:
        record['campaign'] = campaign
    # Keep the partner outside revival range, even when reusing a rebuilt fixture.
    for row in rows.values():
        if row['species'] == other and row['hp'] > 0 and not row['dead']:
            partner = u32(host, row['ptr'] + 0x1014)
            move(host, row['ptr'], partner + 0x124)
    if args.hut:
        enemy = next(r['ptr'] for r in rows.values() if not r['dead'] and
                     u32(host, u32(host, r['ptr']+0x1014)+0x30) == args.conqueror)
        hut = u32(host, defeated + 0x368)
        for row in victims:
            move(host, row['ptr'], defeated + 0x124)
        command(host, f'call SporeCoop::cTribeHut_TakeDamage thiscall {hut + 0x120:#x} '
                      f'f:1000 {args.conqueror:#x} 0 {enemy + 0xC4:#x} {enemy + 0x5A8:#x}')
    else:
        for row in victims:
            move(host, row['ptr'], defeated + 0x124)
            pointer = row['ptr']
            command(host, f'call Simulator::cCreatureCitizen::TakeDamage thiscall {pointer + 0x5A8:#x} '
                          f'f:1000 {args.conqueror:#x} 0 {pointer + 0xC4:#x} 0')
    wait_until(lambda: any(r['timer'] > 0 for r in citizens(host).values() if r['species'] == own),
               'No last-villager rescue timer', 3)
    wait_until(lambda: not host.call('coop.avatar')['out'].startswith('Tribe:'),
               'Rescue expiry did not capture and refresh the world', 50)
    for client in (host, guest):
        wait_until(lambda: client.call('coop.avatar')['out'].startswith('Tribe:'), 'Capture reload did not finish', 60)
        hut = nearest(objects(client, 0x1E4DAAE), record['old_pos'])
        mat = nearest(objects(client, 0x629BAFE), record['old_pos'])
        owner = u32(client, hut + 0x230)
        if u32(client, owner + 0x30) != args.conqueror or u32(client, mat + 0x10C) != owner:
            raise RuntimeError('Captured buildings did not retain the conqueror.')
        tribe = next(t for t, _ in objects(client, 0x18C6D19) if u32(client, t + 0x30) == faction)
        if u32(client, tribe + 0x24) != record['old_native_id'] or not u32(client, tribe + 0x18CC):
            raise RuntimeError('The waiting founder lost its native identity or species profile.')
    print('PASS rescue expiry captured the village and preserved the waiting founder on both peers.', flush=True)
    rows = citizens(host)
    helper = next(i for i, r in rows.items() if r['species'] == other and r['hp'] > 0 and not r['dead'])
    for identifier, row in rows.items():
        if row['species'] == other:
            command(controller, f'coop.tribe select {identifier} 0')
    if not command(controller, 'coop.tribe rebuild').startswith('Select a living villager'):
        raise RuntimeError('A rebuild request without a selected helper was accepted.')
    command(controller, f'coop.tribe select {helper} 1')
    command(controller, 'coop.tribe rebuild')
    command(host, 'wait.frames 45 5000')
    still_waiting = [t for t, _ in objects(host, 0x18C6D19) if u32(host, t + 0x30) == faction]
    if len(still_waiting) != 1 or u32(host, still_waiting[0] + 0x24) != record['old_native_id']:
        raise RuntimeError('An occupied rebuilding site replaced the waiting founder.')
    print('PASS missing helper and occupied-site requests made no replacement.', flush=True)
    body = rows[helper]['ptr']
    partner = u32(host, body + 0x1014)
    center = struct.unpack('<3f', mem(host, partner + 0x124, 12))
    tangent = (center[2], 0, -center[0])
    length, radius = math.hypot(*tangent), math.hypot(*center)
    tangent = [value / length for value in tangent]
    normal = [value / radius for value in center]
    across = [normal[1] * tangent[2] - normal[2] * tangent[1],
              normal[2] * tangent[0] - normal[0] * tangent[2],
              normal[0] * tangent[1] - normal[1] * tangent[0]]
    huts = [position for _, position in objects(host, 0x1E4DAAE)]
    site = None
    for step in range(16):
        angle = step * math.pi / 8
        point = [center[i] + 120 * (tangent[i] * math.cos(angle) + across[i] * math.sin(angle)) for i in range(3)]
        scale = radius / math.hypot(*point)
        candidate = [value * scale for value in point]
        if all(math.dist(candidate, hut) >= 75 for hut in huts):
            site = candidate
            break
    if site is None:
        raise RuntimeError('No clear fixture site within 120 metres of the surviving village.')
    if command(controller, f'coop.tribe select {helper} 1').strip() != 'selected=1':
        raise RuntimeError('The surviving player cannot select its helper.')
    command(controller, 'coop.tribe walk ' + ' '.join(f'{value:.3f}' for value in site))
    wait_until(lambda: math.dist(citizens(host)[helper]['pos'], site) < 12, 'Helper did not walk to the new site', 30)
    command(controller, 'key f9')
    ui = Ui(controller, 'surviving player')
    if not ui.wait(lambda state: state['open'] == '1', 10, 'F9 dialog'):
        raise RuntimeError('F9 did not open.')
    if args.click_control:
        command(controller, 'coop.fix core.ui_tribal_clicks off')
        if ui.state().get('page') == 'space':
            raise RuntimeError('The click control needs the lobby page.')
        before = int(ui.state()['locked_clicks'])
        if not ui.click('space'):
            raise RuntimeError('Missing Village navigation button.')
        command(controller, 'wait.frames 45 5000')
        if ui.state().get('page') == 'space':
            raise RuntimeError('The missing native click did not reproduce; this is not negative evidence.')
        print('FAIL control reproduced: native Village click ignored with the fix disabled.', flush=True)
        command(controller, 'coop.fix core.ui_tribal_clicks on')
        if not ui.click('space') or not ui.wait(lambda state: state['page'] == 'space', 10, 'recovered Village click'):
            raise RuntimeError('The recovery did not open Village controls.')
        if int(ui.state()['locked_clicks']) <= before:
            raise RuntimeError('The native game, rather than the recovery path, handled the click.')
        print('PASS enabled recovery delivered the missing click in the same session.', flush=True)
    elif ui.state().get('page') != 'space' and not ui.click('space'):
        raise RuntimeError('Village navigation is unavailable.')
    button = 'sbtn3' if args.victim == 'host' else 'sbtn1'
    if not ui.wait(lambda state: state[button]['vis'] and state[button]['text'] == 'Rebuild', 10, 'Rebuild button'):
        raise RuntimeError('Village page did not open; check BUG-9018.')
    if not ui.click(button):
        raise RuntimeError('Rebuild button did not accept the click.')

    def replacement():
        for tribe, _ in objects(host, 0x18C6D19):
            if u32(host, tribe + 0x30) == faction and u32(host, tribe + 0x24) != record['old_native_id']:
                return tribe
        return False

    new = wait_until(replacement, 'Helper did not rebuild the defeated founder', 10)
    if campaign:
        totem = u32(host, new+0x19D0)
        if not totem:
            raise RuntimeError('Rebuilt founder lost the native campaign totem')
        begin, end = u32(host, totem+0x11C), u32(host, totem+0x120)
        if end-begin != len(campaign['segments']) or list(mem(host,begin,end-begin)) != campaign['segments']:
            raise RuntimeError('Rebuilt founder lost earned conquest segments')
        if u32(host,new+0x38) != campaign['upgrade']:
            raise RuntimeError('Rebuilt founder lost its earned native hut upgrade')
    record.update(new_pos=struct.unpack('<3f', mem(host, new + 0x124, 12)), new_native_id=u32(host, new + 0x24))
    args.record.write_text(json.dumps(record, indent=2), encoding='utf-8')
    wait_until(lambda: not host.call('coop.avatar')['out'].startswith('Tribe:'), 'Rebuild did not refresh the world', 45)
    directory = Path(__file__).parent
    pair = ['--host', str(args.host), '--guest', str(args.guest)]
    common = [*pair, '--planet', 'Clyth.spo', '--species', own]
    subprocess.run([sys.executable, str(directory / 'duo_tribe_rebuild_checkpoint.py'), *common,
                    '--record', str(args.record)], check=True)
    if args.victim == 'host':
        subprocess.run([sys.executable, str(directory / 'duo_tribe_host_orders.py'), *common], check=True)
    else:
        subprocess.run([sys.executable, str(directory / 'duo_orders.py'), *pair], check=True)
    print('PASS capture, partner rebuild through F9, original species, conquered old site and restored controls.', flush=True)


if __name__ == '__main__':
    main()
