"""F9 guest food delivery and native hostile-to-neutral diplomacy on both games."""
import argparse
import math
import re
import struct
import time

from coop_ability_bar import eax, mem, u32
from coop_ui import Ui
from duo_food import food
from duo_native_ui import require_gameplay_ui, view_tribal_point
from duo_orders import inspect, wait_until
from duo_tribe_rebuild_checkpoint import objects
from duo_tribe_rescue import citizens, move
from duo_villages import Console, command


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', type=int, required=True)
    p.add_argument('--guest', type=int, required=True)
    p.add_argument('--control', action='store_true')
    p.add_argument('--no-ui', action='store_true', help='Isolate the network/native path before the F9 check.')
    args = p.parse_args()
    h, g = Console(args.host), Console(args.guest)
    for c in (h, g):
        if "world 'Clyth.spo'" not in command(c, 'coop.avatar'):
            raise RuntimeError('Use disposable Clyth profiles')
        command(c, 'coop.uistate 0')
        require_gameplay_ui(c)
    command(h, 'coop.tribe village_orders on')
    own, recipient = '0680BF38', '06785BC2'
    rows = citizens(h)
    identity = next(i for i, r in rows.items() if r['species'] == own and not r['dead']
                    and u32(h, r['ptr'] + 0x102C) != 11)
    for tribe in {u32(h, r['ptr'] + 0x1014) for r in rows.values()
                  if r['species'] in (own, '067B3E4A')}:
        command(h, f'call SporeCoop::cTribe_AddFood thiscall {tribe:#x} 100')
    target = next(u32(h, r['ptr'] + 0x1014) for r in rows.values() if r['species'] == recipient)
    faction = u32(h, target + 0x30)
    local = next(t for t, _ in objects(g, 0x18C6D19) if u32(g, t + 0x30) == faction)
    hut = u32(g, local + 0x368)
    point = dict(objects(g, 0x1E4DAAE))[hut]
    x, y = (0, 0) if args.no_ui else view_tribal_point(g, point)
    body = citizens(h)[identity]['ptr']
    owner = u32(h, body + 0x1014)
    own_faction = u32(h, owner + 0x30)
    for i in citizens(g): command(g, f'coop.tribe select {i} {int(i == identity)}')
    manager = eax(h, 'Simulator::cRelationshipManager::Get cdecl')
    thresholds = struct.unpack('<4f', mem(h, manager + 0x10, 16))
    if list(thresholds) != sorted(thresholds) or thresholds[1] >= 0:
        raise RuntimeError('Unexpected native relationship thresholds')
    hostile = thresholds[1] - 1
    # Hostility is fixture setup. Native gift AI must produce the peace event.
    command(h, f'call Simulator::cRelationshipManager::ApplyRelationship thiscall {manager:#x} '
               f'{faction:#x} {own_faction:#x} 0x0530CF07 f:{hostile}')
    def relation(c):
        return float(command(c, f'coop.tribe relation {faction:X} {own_faction:X}').split('=')[-1])
    print('Hostility fixture:', relation(h), relation(g), 'thresholds', thresholds, flush=True)
    wait_until(lambda: relation(h) < -.1 and abs(relation(h)-relation(g)) < .001,
               'Initial hostile relationship did not replicate')
    ui = Ui(g, 'guest')
    def gift():
        if args.no_ui:
            command(g, f'coop.tribe gift_to {faction:X}')
            return
        command(g, 'key f9')
        if not ui.wait(lambda s: s['open'] == '1', 5, 'F9'): raise RuntimeError('F9 unavailable')
        if ui.state().get('page') != 'space': ui.click('space')
        wait_until(lambda: ui.state().get('page') == 'space', 'Village page unavailable')
        for _ in range(4):
            state = ui.state()
            key = next((k for k, v in state.items() if k.startswith('sbtn') and isinstance(v, dict)
                        and v.get('vis') and v.get('text') == 'Gift food'), None)
            if key:
                if not ui.click(key): raise RuntimeError('Gift click failed')
                if not ui.wait(lambda s: s['open'] == '0', 5, 'Gift food'): raise RuntimeError('Gift did not close F9')
                command(g, f'click {x} {y}')
                return
            ui.click('sprev')
            time.sleep(.15)
        raise RuntimeError('F9 Gift food not found')
    rejected = lambda: int(re.search(r'rejected=(\d+)', command(h, 'coop.tribe'))[1])
    if args.control:
        command(h, 'coop.fix tribe.gift_orders off')
        before = rejected()
        gift()
        wait_until(lambda: rejected() > before, 'Old gift rejection did not reproduce')
        print('FAIL control reproduced: native guest food gift rejected by old allowlist.', flush=True)
        command(h, 'coop.fix tribe.gift_orders on')
    # An empty payer is refused before native execution.
    command(h, f'call SporeCoop::cTribe_AddFood thiscall {owner:#x} {-math.ceil(food(h)[own])}')
    wait_until(lambda: food(h)[own] == food(g)[own] == 0, 'Empty payer differs between peers')
    before = rejected()
    gift()
    wait_until(lambda: rejected() > before, 'Unfunded gift was accepted')
    command(h, f'call SporeCoop::cTribe_AddFood thiscall {owner:#x} 100')
    move(h, body, u32(h, target + 0x260) + 0x38)
    initial = food(h)
    if args.control:
        command(g, 'coop.fix tribe.relationships off')
        command(g, 'coop.fix tribe.npc_food off')
    done = inspect(h)[1]
    gift()
    print('Gift request:', command(g, 'coop.tribe').split('tribe last order:')[-1].splitlines()[0], flush=True)
    try:
        wait_until(lambda: inspect(h)[1] > done, 'Funded gift was not accepted', 5)
    except Exception:
        print(command(h, 'coop.tribe'), flush=True)
        raise
    saw_native = False
    def delivered():
        nonlocal saw_native
        current = citizens(h).get(identity)
        if not current or current['dead']:
            raise RuntimeError('Gift actor died or disappeared before delivery')
        ai = u32(h, current['ptr'] + 0xB4C)
        saw_native |= bool(ai and u32(h, ai + 0x1D8) == 0x0462855E)
        totals = food(h)
        return saw_native and totals[own] < initial[own] and totals[recipient] > initial[recipient] and abs(relation(h)) < .001
    try:
        wait_until(delivered, 'Native gift failed to deliver food and neutralize hostility', 30)
    except Exception:
        print('Gift failure:', food(h), food(g), relation(h), relation(g), 'native', saw_native, flush=True)
        print(command(h, 'coop.tribe'), flush=True)
        raise
    if args.control:
        time.sleep(2)
        if relation(g) >= -.1: raise RuntimeError('Old relationship divergence did not reproduce')
        print('FAIL control reproduced: host peace remained hostile on guest without snapshots.', flush=True)
        if food(g)[recipient] != initial[recipient]: raise RuntimeError('Old NPC food divergence did not reproduce')
        print('FAIL control reproduced: delivered NPC food remained stale on guest without food snapshots.', flush=True)
        command(g, 'coop.fix tribe.relationships on')
        command(g, 'coop.fix tribe.npc_food on')
    wait_until(lambda: abs(relation(h)-relation(g)) < .001 and
               all(food(h)[k] == food(g)[k] for k in (own, recipient)), 'Gift state differs on guest', 10)
    for c in (h, g):
        if citizens(c)[identity]['species'] != own: raise RuntimeError('Gift changed the founding species')
        if 'total faults: 0' not in command(c, 'exc'): raise RuntimeError('Native fault occurred')
    print('PASS '+('Relayed' if args.no_ui else 'F9')+' native gift pays food, delivers it, makes peace, and reconciles both peers.', flush=True)


if __name__ == '__main__': main()
