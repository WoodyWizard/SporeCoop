"""A returning founder can launch from a saved Civ victory while its partner is already in Space."""
import argparse
import time

import uifind
from coop_ability_bar import u32
from duo_civ_launch_editor import acknowledge, create_first_ship
from duo_civ_species import native
from duo_native_ui import dismiss_registration, dismiss_service_error
from duo_orders import wait_until
from duo_tribe_graduation_editor import button, click
from duo_space_captain_outfit import identity
from duo_space_tutorial_choices import context
from duo_villages import Console, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=int,required=True)
    parser.add_argument('--guest',type=int,required=True)
    parser.add_argument('--recover-victory', action='store_true',
                        help='Wait for the host to supply its saved Civilization victory first.')
    parser.add_argument('--rejoin-at-tutorial', type=int, metavar='PORT',
                        help='Reconnect at Mission Control and verify both models and the tutorial choice.')
    args = parser.parse_args()
    h, g = Console(args.host), Console(args.guest)
    for peer in (h,g):
        assert 'duoqa' in command(peer,'coop.savedb').lower()
    assert 'Space' in command(h,'game.mode')
    if args.recover_victory:
        def restored():
            assert 'Space' in command(h,'game.mode'), 'Host left Space while delivering Civilization'
            mode = command(g,'game.mode')
            assert 'Space' not in mode, 'Guest skipped its personal Civilization launch'
            if 'Civ' not in mode: return False
            nations = command(g,'coop.civ nations')
            return all(nations.count('nation_pol=' + political) == 5 for political in ('800021CA','000021E9'))
        wait_until(restored,'Guest did not receive the saved ten-city victory',90)
        dismiss_registration(g)
        dismiss_service_error(g)
    def received():
        assert 'Civ' in command(g,'game.mode'), 'Orbital share replaced the first-time guest launch'
        return 'shared orbital save waiting for your first flight' in command(g,'coop.status')
    wait_until(received,'Guest did not stage the orbital checkpoint',90)
    command(g,'coop.uistate 0')
    acknowledge(g)
    click(g,1136,648)
    time.sleep(2)
    button(g,'FFFFFFF1')
    click(g,1152,641)
    def editor():
        acknowledge(g)
        return 'Editor' in command(g,'game.mode')
    wait_until(editor,'Returning guest could not open its own ship editor',45)
    assert 'editor 96B24187' in command(g,'coop.editor')
    create_first_ship(g,'LateFounderVoyager',170)
    def captain():
        for widget in ('0445C99D','062EF353'): button(g,widget)
        return 'enabled=1' in command(g,'coop.ui where 01510D07')
    wait_until(captain,'Late guest captain prompt missing',60)
    click(g,576,347)
    command(g,'text LateCaptain')
    click(g,805,423)
    wait_until(lambda:'none' in command(g,'coop.ui where 01510D07'),'Captain naming stayed open after confirmation',15)
    def comm():
        acknowledge(g)
        return 'enabled=1' in command(g,'coop.ui where 05244C68')
    wait_until(comm,'Late guest did not reach Mission Control',60)
    click(g,289,502)
    wait_until(lambda:uifind.shown(g,'Been there, done that'),'Late guest tutorial choice missing',25)
    cid = u32(g,native(g,'Simulator::GetPlayerEmpire cdecl')+0xC4)
    def own_body():
        result = command(g,f'coop.creations body_parts {cid:X}').partition(' parts:')[2]
        return result and result == command(g,'coop.creations body_parts 29148A01').partition(' parts:')[2]
    wait_until(own_body,'Late captain does not match its founder',25)
    assert 'Space' in command(h,'game.mode'), 'Host left Space during guest launch'
    for peer in (h,g):
        assert native(peer,'Simulator::GetPlayerEmpireID cdecl') == 0x800021CA
        assert 'total faults: 0' in command(peer,'exc')
    print('PASS returning Civ founder created its own spacecraft and captain and received flight training while host remained in Space.')

    if args.rejoin_at_tutorial:
        before = identity(g)
        assert before and context(g) == 0 and context(h) in (1,2)
        command(g,'coop.leave')
        wait_until(lambda:'players=1/4' in command(h,'coop.status'),'Host did not observe departure',25)
        command(g,f'coop.join 127.0.0.1:{args.rejoin_at_tutorial} LateFounder')
        wait_until(lambda:'players=2/4' in command(h,'coop.status'),'Guest failed to reconnect',30)
        wait_until(lambda:'shared orbital save waiting for your first flight' in command(g,'coop.status'),
                   'Reconnected guest failed to reserve its live tutorial',90)
        assert context(g) == 0 and context(h) in (1,2)
        assert uifind.shown(g,'Been there, done that') and identity(g) == before
        for peer in (h,g):
            assert 'total faults: 0' in command(peer,'exc')
        print('PASS disconnected founder kept its two native tutorial choices, spacecraft and captain on reconnect.')


if __name__ == '__main__':
    main()
