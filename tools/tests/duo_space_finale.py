"""Exercise the native galactic-core ending on an already running disposable Windows pair.

Fixture shortcuts: grants an interstellar drive and moves the host near the core.
These do NOT qualify earning the drive or travelling through the Grox. Entry, the
guest's Fly to button, cinematics, rewards and reconnect use the normal game paths.
"""
import argparse
import re
import time

from coop_space_finale import fin, fly_button, CORE_CINEMATIC
from coop_ui import Ui
from duo_orders import wait_until
from duo_space_captain_outfit import identity
from duo_tribe_graduation_editor import button
from duo_villages import Console, command


def settle(peer, timeout=90):
    quiet=0
    until=time.monotonic()+timeout
    last=time.monotonic()
    while time.monotonic()<until:
        if fin(peer).get('busy')=='0':
            quiet+=1
            if quiet>=3:return
        else:
            quiet=0
            if time.monotonic()-last>=8:
                for wid in ('0445C99D','062EF353','FFFFFFF1'):button(peer,wid)
                command(peer,'key enter')
                last=time.monotonic()
        time.sleep(1)
    raise AssertionError('Native caption/cinematic did not settle: '+fin(peer)['line'])


def zoom(peer,ctx,notches):
    for _ in range(40):
        if fin(peer).get('ctx')==str(ctx):return
        command(peer,f'wheel 588 332 {notches}')
        time.sleep(1.5)
    raise AssertionError('Native zoom did not reach context '+str(ctx))


def follow(peer):
    ui=Ui(peer,'guest')
    if ui.state().get('open')!='1':command(peer,'key f9')
    assert ui.wait(lambda s:s.get('open')=='1',15)
    if ui.state().get('page')!='space':assert ui.click('space')
    assert ui.wait(lambda s:s.get('page')=='space',15)
    key=fly_button(ui)
    assert key and ui.click(key),'No native Fly to partner button'
    if ui.state().get('open')=='1':command(peer,'key f9')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('host','guest','port'):parser.add_argument('--'+name,type=int,required=True)
    args=parser.parse_args()
    h,g=Console(args.host),Console(args.guest)
    original=[]
    for peer in (h,g):
        assert 'duoqa' in command(peer,'coop.savedb').lower()
        assert 'Space' in command(peer,'game.mode') and fin(peer).get('ctx') in ('1','2')
        assert fin(peer).get('entered')=='0' and fin(peer).get('staff')=='0'
        assert re.search(r'screen 1176 664',command(peer,'coop.uistate'))
        original.append(identity(peer))
        assert original[-1],'No personal spacecraft/captain'
        print('FIXTURE drive:',command(peer,'coop.space tool add interstellardrive'),flush=True)
        settle(peer)
    zoom(h,2,-5);settle(h)
    print('FIXTURE near:',command(h,'coop.space finale goto near'),flush=True)
    wait_until(lambda:fin(h).get('zone')=='2','Host did not reach near-core fixture',30)
    print('FIXTURE core:',command(h,'coop.space finale goto core'),flush=True)
    wait_until(lambda:fin(h).get('zone')=='3','Host did not reach core fixture',30)
    settle(h);zoom(h,1,3);settle(h)
    for _ in range(4):
        command(h,'click 588 272')
        time.sleep(5)
        for _ in range(4):
            command(h,'wheel 588 332 3');time.sleep(1.5)
        if fin(h).get('waiting')=='1':break
    assert fin(h).get('waiting')=='1' and fin(h).get('entered')=='0',fin(h)
    print('PASS native core entry waits for partner',flush=True)
    follow(g)
    wait_until(lambda:fin(g).get('zone')=='2' and fin(g).get('ctx')=='1',
               'Guest did not follow to the near-core system',45)
    settle(g);zoom(g,2,-5);settle(g);follow(g)
    wait_until(lambda:all(fin(c).get('entered')=='1' for c in (h,g)),
               'Both players did not enter the core',90)
    assert all(fin(c).get('cin')==CORE_CINEMATIC for c in (h,g))
    print('PASS both native core cinematics playing',flush=True)
    deadline=time.monotonic()+300
    last=time.monotonic()
    while time.monotonic()<deadline:
        if all(fin(c).get('busy')=='0' and fin(c).get('ctx')=='2' and fin(c).get('staff')=='1' for c in (h,g)):break
        if time.monotonic()-last>=8:
            for c in (h,g):command(c,'key enter')
            last=time.monotonic()
        time.sleep(2)
    for peer in (h,g):
        state=fin(peer)
        assert state.get('busy')=='0' and state.get('ctx')=='2'
        assert state.get('staff')=='1' and int(state['charges'])>0,state
        print('PASS native Staff of Life:',state['line'],flush=True)
    command(g,'coop.leave')
    wait_until(lambda:'players=1/4' in command(h,'coop.status'),'Guest did not leave',20)
    command(g,f'coop.join 127.0.0.1:{args.port} LateFounder')
    wait_until(lambda:'Space' in command(g,'game.mode') and fin(g).get('staff')=='1',
               'Guest lost the ending reward on reconnect',150)
    for peer,expected in zip((h,g),original):
        wait_until(lambda:identity(peer)==expected,'Personal creations changed after ending',30)
        assert 'total faults: 0' in command(peer,'exc')
    print('PASS core ending, both native rewards, personal creations and reconnect. '
          'Drive unlock and long-distance campaign travel were fixture shortcuts.',flush=True)


if __name__=='__main__':main()
