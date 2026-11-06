"""Check native diplomacy on two different NPCs whose political IDs collide across peers."""
import argparse
from coop_ability_bar import eax, u32
from duo_orders import wait_until
from duo_tribe_rescue import citizens
from duo_villages import Console, command


def factions(client):
    by_species = {}
    for row in citizens(client).values():
        if row['species'] not in by_species:
            tribe = u32(client, row['ptr'] + 0x1014)
            by_species[row['species']] = u32(client, tribe + 0x30)
    return by_species


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', required=True, type=int)
    p.add_argument('--guest', required=True, type=int)
    p.add_argument('--prepare-npcs', action='store_true', help='Use the native village factory for this diplomacy-only fixture.')
    args = p.parse_args()
    h, g = Console(args.host), Console(args.guest)
    for c in (h, g):
        assert "world 'Vantiegan.spo'" in command(c, 'coop.avatar')
        assert 'host 291489FF, guest 7CF26CCB' in command(c, 'coop.founders')
    founder = '7CF26CCB'
    def collision():
        host, guest = factions(h), factions(g)
        npcs = sorted((host.keys() & guest.keys()) - {founder, '291489FF'})
        return next(((host, guest, a, b) for a in npcs for b in npcs
                     if a != b and host[a] == guest[b] and host[a] != guest[a]), None)
    if args.prepare_npcs:
        # Isolate packet addressing. This fixture is never evidence of natural
        # campaign progression and never assigns either peer a political ID.
        for _ in range(3):
            if collision(): break
            before = len(factions(h))
            controller = eax(h, 'Simulator::cNpcTribeController::Get cdecl')
            assert controller and before < 7
            command(h, f'call SporeCoop::NpcTribeController_FormVillage thiscall {controller:#x} 0')
            wait_until(lambda: len(factions(g)) > before and len(factions(h)) == len(factions(g)),
                       'Native fixture village did not appear on both peers', 30)
    host, guest, a, b = wait_until(collision, 'Fresh fixture did not produce overlapping NPC political IDs', 120)
    print('Collision:', a, hex(host[a]), hex(guest[a]), b, hex(host[b]), hex(guest[b]), flush=True)
    manager = eax(h, 'Simulator::cRelationshipManager::Get cdecl')
    assert manager

    def read(c, ids):
        return tuple(float(command(c, f'coop.tribe relation {ids[s]:X} {ids[founder]:X}').split('=')[-1])
                     for s in (a, b))

    # Disposable relationship fixture only, never a qualification for earning
    # diplomatic rewards. Two distinct neutral-range opinions reveal cross-talk.
    for species, value in ((a, 1.25), (b, -1.25)):
        command(h, f'call Simulator::cRelationshipManager::ApplyRelationship thiscall {manager:#x} '
                   f'{host[species]:#x} {host[founder]:#x} 0x0530CF07 f:{value}')
    wanted = read(h, host)
    assert abs(wanted[0] - wanted[1]) > 1, wanted
    try:
        wait_until(lambda: read(g, guest) == read(h, host) == wanted, 'Diplomacy applied to wrong NPCs', 10)
    finally:
        print('Native opinions (target A, B), host:', read(h, host), 'guest:', read(g, guest), flush=True)
    for c in (h, g):
        assert 'total faults: 0' in command(c, 'exc')
    print('PASS same two native NPC opinions despite colliding faction IDs.', flush=True)


if __name__ == '__main__':
    main()
