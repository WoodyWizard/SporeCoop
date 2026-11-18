"""Save a cosmetic change on the guest founder while the other captain is in orbit."""
import argparse
import json
import re
import time
from pathlib import Path

import uifind
from coop_ability_bar import u32
from coop_campaign import drag_slow
from duo_civ_species import native
from duo_orders import wait_until
from duo_tribe_graduation_editor import click
from duo_villages import Console, command


def identity(peer):
    emp = native(peer,'Simulator::GetPlayerEmpire cdecl')
    model = u32(peer,emp+0xC4)
    digest = command(peer,f'coop.creations body_hash {model:X}').strip()
    if not re.fullmatch(r'[a-fA-F0-9]{64}',digest): return None
    return dict(captain=model, digest=digest, ship=command(peer,'coop.creations ship_parts').strip())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=int,required=True)
    parser.add_argument('--guest',type=int,required=True)
    parser.add_argument('--record',type=Path,required=True)
    parser.add_argument('--in-orbit',action='store_true',help='Use the cold orbital checkpoint after both players have left the homeworld.')
    args = parser.parse_args()
    h, g = Console(args.host), Console(args.guest)
    assert not args.record.exists()
    for peer in (h,g):
        assert 'duoqa' in command(peer,'coop.savedb').lower()
        assert "world 'Vantiegan.spo'" in command(peer,'coop.avatar')
    host, guest = identity(h), identity(g)
    assert host and guest and host['digest'] != guest['digest']
    if not args.in_orbit:
        assert uifind.shown(g,"I'll be back soon")
        click(g,588,307)
        time.sleep(1.5)
    click(g,1126,475)
    # Several inactive layouts also contain "Enter Outfitter"; caption lookup
    # can return a hidden duplicate. Target the actual visible rank panel.
    def enter_outfitter():
        if 'Editor' in command(g,'game.mode'): return True
        under_pointer = command(g,'coop.ui at 202 440')
        if 'Enter Outfitter' in under_pointer: click(g,202,440)
        return False
    wait_until(enter_outfitter,'Guest captain outfitter missing',25)
    assert f"editor A56567F7 key {guest['captain']:08X}" in command(g, 'coop.editor')
    time.sleep(3)
    drag_slow(g,70,111,733,408)
    click(g,1155,645)
    wait_until(lambda:'Space' in command(g,'game.mode'),'Native outfit save refused',30)
    def changed():
        result = identity(g)
        return result if result and result['digest'] != guest['digest'] else None
    saved = wait_until(changed,'Guest outfit model did not change on disk',30)
    assert identity(h) == host and saved['ship'] == guest['ship']
    assert all('total faults: 0' in command(p,'exc') for p in (h,g))
    args.record.write_text(json.dumps([host,saved]),encoding='utf-8')
    print('PASS guest saved a different captain outfit; host captain and both ships retained.',flush=True)


if __name__ == '__main__':
    main()
