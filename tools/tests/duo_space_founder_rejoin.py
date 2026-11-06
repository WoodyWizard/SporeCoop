"""Check the orbital save from the two-founder Civilization launch, including rejoin."""
import argparse
import json
import re
from pathlib import Path

from coop_ability_bar import u32
from duo_civ_species import native
from duo_orders import wait_until
from duo_villages import Console, command
from duo_space_captain_outfit import identity


def empire(peer):
    return native(peer, 'Simulator::GetPlayerEmpire cdecl')


def balance(peer):
    return u32(peer, empire(peer) + 0xD0)


def verify(peer):
    assert 'duoqa' in command(peer, 'coop.savedb').lower()
    avatar = command(peer, 'coop.avatar')
    assert avatar.startswith('Space:') and "world 'Vantiegan.spo'" in avatar
    assert 'host 291489FF, guest 7CF26CCB' in command(peer, 'coop.founders')
    assert native(peer,'Simulator::GetPlayerEmpireID cdecl') == 0x800021CA
    assert 'total faults: 0' in command(peer,'exc')


def add_money(peer, delta):
    command(peer, f'call Simulator::cEmpire::AddMoney thiscall {empire(peer):#x} {delta}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True, type=int)
    parser.add_argument('--guest', required=True, type=int)
    parser.add_argument('--port', required=True, type=int)
    parser.add_argument('--identities', type=Path, help='Saved host and guest captain and ship readbacks from the personal launch.')
    args = parser.parse_args()
    h, g = Console(args.host), Console(args.guest)
    expected = json.loads(args.identities.read_text()) if args.identities else None
    def check_models():
        if expected:
            for peer, wanted in zip((h,g),expected,strict=True):
                try:
                    wait_until(lambda:identity(peer) == wanted, 'Personal captain or spacecraft differs', 25)
                except RuntimeError:
                    print('Expected',wanted,'actual',identity(peer),flush=True)
                    raise
            for peer, other, wanted in ((h,1,expected[1]),(g,0,expected[0])):
                def actual_partner():
                    line = re.search(rf"^remote {other} .*? puppet=([0-9a-fA-F]+) ", command(peer,'coop.status'), re.M)
                    ship = int(line[1],16) if line else 0
                    if not ship: return None
                    model = u32(peer,ship+0x780)
                    text = command(peer,f'coop.creations ship_parts {model:X}')
                    return text if ' parts:' in text and text.split(' parts:')[1] == wanted['ship'].split(' parts:')[1] else None
                observed = wait_until(actual_partner,'Other player rendered with the wrong ship',35)
                print('PASS remote spacecraft model', observed, flush=True)
    for peer in (h,g):
        verify(peer)
    check_models()
    before = balance(h)
    wait_until(lambda: balance(g) == before, 'Initial orbital treasury differs')
    assert 100 < before < 99_999_000
    add_money(g, 317)
    wait_until(lambda: balance(h) == balance(g) == before+317, 'Guest treasury gain failed')
    def guest_slot():
        match = re.search(r' location=(.*?) access=',command(g,'coop.savedb'))
        return match[1].casefold() if match else ''
    old_slot = guest_slot()
    assert old_slot, 'No active guest slot before disconnect'
    command(g, 'coop.leave')
    wait_until(lambda: 'players=1/4' in command(h, 'coop.status'), 'Guest did not disconnect')
    add_money(h, -73)
    wait_until(lambda: balance(h) == before+244, 'Host debit failed while partner was offline')
    command(g, f'coop.join 127.0.0.1:{args.port} DuoTribalGuest')
    def joined():
        if 'players=2/4' not in command(h, 'coop.status') or 'Space' not in command(g,'game.mode'):
            return False
        avatar = g.call('coop.avatar')
        return (avatar['ok'] and avatar['out'].startswith('Space:') and balance(g) == before+244
                and bool(guest_slot()) and guest_slot() != old_slot)
    wait_until(joined, 'Rejoined spacecraft did not receive the host treasury', 100)
    check_models()
    for peer in (h,g):
        verify(peer)
        assert balance(peer) == before+244
    print('PASS cold orbital save, both original species, same native empire, treasury and reconnect.',
          'Final balance', before+244, flush=True)


if __name__ == '__main__':
    main()
