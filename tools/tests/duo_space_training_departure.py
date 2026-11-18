"""Verify departure after completed native training and the personal shared-Space handoff.

Requires the final Mission Control 'Bye.' response and a JSON list of both
players' identity() readbacks recorded before the training handoff.
"""
import argparse,time,json,re
from pathlib import Path
from duo_villages import Console,command
from duo_space_captain_outfit import identity
from duo_space_tutorial_choices import context
from duo_tribe_graduation_editor import button
from duo_orders import wait_until
import uifind
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host',type=int,required=True)
parser.add_argument('--guest',type=int,required=True)
parser.add_argument('--identities',type=Path,required=True)
args=parser.parse_args()
h,g=Console(args.host),Console(args.guest)
expected=json.loads(args.identities.read_text())
assert len(expected)==2
for peer,want in zip((h,g),expected):
    assert 'duoqa' in command(peer,'coop.savedb').lower()
    assert identity(peer)==want
assert uifind.press(g,'Bye.',timeout=20)
def orbit():
    if 'Space' not in command(g,'game.mode'):return False
    if context(g) in (1,2):return True
    for wid in ('0445C99D','062EF353','FFFFFFF1'):button(g,wid)
    command(g,'wheel 588 332 -25')
    time.sleep(1.5)
    return False
wait_until(orbit,'Guest remained on homeworld after completed native training',65)
print('Guest departed with the native drive',flush=True)
time.sleep(15)
def settled():
    if 'Space' not in command(g,'game.mode') or context(g) not in (1,2):return False
    status=command(g,'coop.status')
    remote=re.search(r'remote 0 .*?puppet=([0-9a-fA-F]+)',status)
    return 'shared orbital save waiting' not in status and bool(remote and int(remote[1],16))
wait_until(settled,'Shared orbital world did not finish loading',120)
for peer,want in zip((h,g),expected):
    wait_until(lambda:identity(peer)==want,'Personal identity changed on orbital handoff',30)
    assert 'host 291489FF, guest 7CF26CCB' in command(peer,'coop.founders')
    assert 'total faults: 0' in command(peer,'exc')
    print(command(peer,'coop.avatar'),flush=True)
print('PASS native training departure and shared Space handoff retain both personal creations.',flush=True)
