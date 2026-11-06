"""Guest healing through native AI on disposable two-founder Clyth worlds."""
import argparse
import re
import struct
import time

from coop_ability_bar import eax, mem, u32
from coop_ui import Ui
from duo_native_ui import require_gameplay_ui, view_tribal_point
from duo_orders import wait_until
from duo_tribe_rescue import citizens, move
from duo_villages import Console, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--expect-rejected', action='store_true')
    args = parser.parse_args()
    h, g = Console(args.host), Console(args.guest)
    for c in (h, g):
        if "world 'Clyth.spo'" not in command(c, 'coop.avatar'):
            raise RuntimeError('Use disposable Clyth profiles')
        command(c, 'coop.uistate 0')
        require_gameplay_ui(c)
    command(h, 'coop.tribe village_orders on')
    rows = citizens(h)
    def adult(species, chief_ok=False):
        return next((i, r) for i, r in rows.items() if r['species'] == species and not r['dead']
                    and u32(h, r['ptr'] + 0xB34) == 1
                    and (chief_ok or u32(h, u32(h, r['ptr'] + 0x1014) + 0x11C) != r['ptr']))
    healer, actor = adult('0680BF38')
    patient, victim = adult('067B3E4A', chief_ok=True)
    for row in (actor, victim):
        village = u32(h, row['ptr'] + 0x1014)
        command(h, f'call SporeCoop::cTribe_AddFood thiscall {village:#x} 100')
    # Equipment and injury are fixture setup, not the behavior being asserted.
    body = actor['ptr']
    command(h, f'call SporeCoop::Citizen_SetSpecializedTool thiscall {body:#x} 9')
    item = eax(h, f'Simulator::cCreatureCitizen::GetHandheldItemForTool thiscall {body:#x} 9')
    command(h, f'call SporeCoop::Citizen_SetHandheldItem thiscall {body:#x} {item} -1')
    wait_until(lambda: u32(g, citizens(g)[healer]['ptr'] + 0x102C) == 9, 'Healer tool did not replicate')
    # Bring the partner to the healer so camera setup does not require a long march.
    move(h, victim['ptr'], body + 0xC4)
    for row in (actor, victim):
        p = row['ptr']
        rec = eax(h, f'Simulator::cCreatureCitizen::DoAction thiscall {p:#x} 4 0 0')
        if rec:
            command(h, f'call App::Property::SetValueVector3 thiscall {rec+0x39:#x} {p+0xC4:#x}')
    for i in citizens(g):
        command(g, f'coop.tribe select {i} {int(i == healer)}')
    maximum = struct.unpack('<f', mem(h, victim['ptr'] + 0x5D4, 4))[0]
    initial = maximum * .35
    def injure():
        p = citizens(h)[patient]['ptr']
        # Camera panning can outlast the original WalkTo; interrupt any native
        # Eat/Sleep task before creating the injury so it cannot heal the fixture.
        rec = eax(h, f'Simulator::cCreatureCitizen::DoAction thiscall {p:#x} 4 0 0')
        if rec:
            command(h, f'call App::Property::SetValueVector3 thiscall {rec+0x39:#x} {p+0xC4:#x}')
        command(h, f'call Simulator::cCombatant::SetHealth thiscall {p+0x5A8:#x} f:{initial}')
        wait_until(lambda: abs(citizens(g)[patient]['hp'] - initial) < 1, 'Injury did not replicate')
    rejected = lambda: int(re.search(r'rejected=(\d+)', command(h, 'coop.tribe'))[1])
    if args.expect_rejected:
        h.call('coop.fix tribe.heal_orders off')
        injure()
        before = rejected()
        command(g, f'coop.tribe act 5 {patient}')
        wait_until(lambda: rejected() > before, 'Old command rejection did not reproduce')
        time.sleep(2)
        if citizens(h)[patient]['hp'] > initial + 1:
            raise RuntimeError('Control patient healed without the accepted healing order')
        print('FAIL control reproduced: equipped guest healer order rejected by the host.', flush=True)
    else:
        # Authorization checks precede native execution. A selected adult without
        # the healing tool, and an injured enemy target, must both be refused.
        untrained = next(i for i, r in rows.items() if r['species'] == '0680BF38'
                         and i != healer and not r['dead']
                         and u32(h, r['ptr'] + 0x102C) != 9)
        injure()
        command(g, f'coop.tribe select {healer} 0')
        command(g, f'coop.tribe select {untrained} 1')
        before = rejected()
        command(g, f'coop.tribe act 5 {patient}')
        wait_until(lambda: rejected() > before, 'Unequipped healer request was accepted')
        command(g, f'coop.tribe select {untrained} 0')
        command(g, f'coop.tribe select {healer} 1')
        enemy, enemy_row = next((i, r) for i, r in citizens(h).items()
                               if r['species'] not in ('067B3E4A', '0680BF38') and not r['dead'])
        command(h, f'call Simulator::cCombatant::SetHealth thiscall {enemy_row["ptr"]+0x5A8:#x} f:20')
        before = rejected()
        command(g, f'coop.tribe act 5 {enemy}')
        wait_until(lambda: rejected() > before, 'Enemy healing request was accepted')
        current_enemy = citizens(h).get(enemy)
        if current_enemy and not current_enemy['dead']:
            command(h, f'call Simulator::cCombatant::SetHealth thiscall {current_enemy["ptr"]+0x5A8:#x} f:{enemy_row["hp"]}')
        print('PASS host rejects healing without the tool and healing an enemy.', flush=True)
        x, y = view_tribal_point(g, citizens(g)[patient]['pos'])
        # The chief may return home while the camera pans. Put the healer near
        # the current patient; this test qualifies healing, not a cross-map walk.
        current = citizens(h)
        move(h, current[healer]['ptr'], current[patient]['ptr'] + 0xC4)
        injure()
        ui = Ui(g, 'guest')
        command(g, 'key f9')
        if not ui.wait(lambda s: s['open'] == '1', 5, 'F9'):
            raise RuntimeError('F9 did not open')
        if ui.state().get('page') != 'space': ui.click('space')
        wait_until(lambda: ui.state().get('page') == 'space', 'Village page missing')
        found = False
        for _ in range(8):
            state = ui.state()
            key = next((k for k, v in state.items() if k.startswith('sbtn') and isinstance(v, dict)
                        and v.get('text') == 'Heal' and v.get('vis')), None)
            if key:
                found = ui.click(key)
                break
            ui.click('snext')
            time.sleep(.15)
        if not found or not ui.wait(lambda s: s['open'] == '0', 5, 'Heal'):
            raise RuntimeError('F9 Heal control unavailable')
        command(g, f'click {x} {y}')
        saw_healing = False
        def healed():
            nonlocal saw_healing
            actor_now = citizens(h)[healer]['ptr']
            ai = u32(h, actor_now + 0xB4C)
            saw_healing |= bool(ai and u32(h, ai + 0x1D8) == 0x0562C0FB)
            return saw_healing and citizens(h)[patient]['hp'] > initial + maximum * .15
        wait_until(healed,
                   'Native healer did not restore partner health', 20)
        wait_until(lambda: abs(citizens(h)[patient]['hp'] - citizens(g)[patient]['hp']) < 1,
                   'Partner healing did not reach guest', 10)
        print('PASS F9 guest healer restores partner health through native AI on both games.', flush=True)
    for c in (h, g):
        if 'total faults: 0' not in command(c, 'exc'):
            raise RuntimeError('Native fault occurred')


if __name__ == '__main__':
    main()
