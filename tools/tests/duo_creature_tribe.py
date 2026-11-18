"""Disposable Vantiegan 1176x664: normal transition, then automatic founding.

Run only on fresh copies of the DuoQAM1FinalLand profiles. The only progression
shortcut fills the evolution bar; History, ready, both editors, the fire movie
and Tribal introduction use native controls. Both games are closed on exit.
"""
import argparse
import sys, time, re, runpy
from pathlib import Path
sys.stdout.reconfigure(encoding='utf-8')
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/tests'))
from duo_villages import Console, command
from duo_orders import wait_until
from duo_native_ui import dismiss_registration, dismiss_service_error
from coop_ui import Ui
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--host',type=int,required=True)
p.add_argument('--guest',type=int,required=True)
args=p.parse_args()
h,g=Console(args.host),Console(args.guest)

def button(c, wid):
    m=re.search(rf'^{wid} ([\d.]+) ([\d.]+) ([\d.]+) ([\d.]+) enabled=1',command(c,'coop.ui where '+wid),re.M)
    if not m: return False
    x1,y1,x2,y2=map(float,m.groups())
    command(c,f'click {round((x1+x2)/2)} {round((y1+y2)/2)}')
    return True

def dialogs(c):
    command(c,'coop.uistate 0')
    dismiss_service_error(c)
    for wid in ('0445C99D','062EF353'):
        if button(c,wid): return
    if re.search(r'^05822D40 [\d.]+ [\d.]+ [\d.]+ [\d.]+ enabled=',
                 command(c,'coop.ui where 05822D40'),re.M):
        button(c,'FFFFFFF1')

def click(c,x,y):
    command(c,f'move {x} {y}')
    time.sleep(.2)
    command(c,f'click {x} {y}')

try:
    for c in (h,g): wait_until(lambda:c.call('ping')['ok'],'Console unavailable',45)
    command(h,'coop.load Game0 Vantiegan')
    wait_until(lambda:'Creature' in command(h,'game.mode'),'Host Creature unavailable',60)
    command(h,'coop.host 47810 DuoTribeHost')
    command(g,'coop.join 127.0.0.1:47810 DuoTribeGuest')
    wait_until(lambda:'Creature' in command(g,'game.mode'),'Guest Creature unavailable',60)
    for c in (h,g):
        dismiss_registration(c)
        assert 'screen 1176 664' in command(c,'coop.uistate')
    command(h,'call Simulator::cCreatureGameData::AddEvolutionPoints cdecl f:3000')
    for step in range(75):
        for c in (h,g): dialogs(c)
        if step>12 and all('busy: -' in command(c,'coop.status') for c in (h,g)): break
        time.sleep(1)
    else: raise RuntimeError('Evolution reward captions did not settle')
    click(h,1136,648) # observed native History button, fixture resolution
    time.sleep(2)
    dialogs(h)
    click(h,1152,641) # observed History check
    wait_until(lambda:command(h,'coop.vote').startswith('vote active'),'Evolution vote absent',10)
    command(g,'coop.uistate 1')
    assert Ui(g,'guest').click('ready')
    command(g,'coop.uistate 0')
    for c,key in ((h,'291489FF'),(g,'29148A01')):
        wait_until(lambda:'Editor' in command(c,'game.mode'),'Own editor absent',45)
        detail=command(c,'coop.editor')
        assert 'key '+key in detail, detail
        print('Own editor preserved',key,detail,flush=True)
        time.sleep(2)
        dialogs(c)
    for c in (g,h):
        click(c,1155,645) # observed native editor Done
        wait_until(lambda:'Editor' not in command(c,'game.mode'),'Editor did not close',30)
    good_since=None
    last_modes=None
    for step in range(240):
        for c in (h,g): dialogs(c)
        modes=[command(c,'game.mode').strip() for c in (h,g)]
        if modes!=last_modes: print('Modes',modes,flush=True); last_modes=modes
        ready=all('Tribe' in m for m in modes) and all('busy: -' in command(c,'coop.status') for c in (h,g))
        # Mode alone is not playable Tribal entry: wait past native intro and
        # automatic save/reload before invoking gameplay assertions.
        good_since=(good_since or time.monotonic()) if ready else None
        if good_since and time.monotonic()-good_since>8: break
        time.sleep(1)
    else: raise RuntimeError('Playable Tribal entry did not settle')
    sys.argv=['duo_tribe_founding.py','--host',str(args.host),'--guest',str(args.guest)]
    runpy.run_path(str(ROOT/'tools/tests/duo_tribe_founding.py'),run_name='__main__')
    print('PASS live Creature -> own editors -> native Tribe -> automatic distinct founders.',flush=True)
finally:
    cleanup_errors=[]
    for i,c in ((args.host,h),(args.guest,g)):
        try:
            print(i,c.call('coop.founders')['out'],c.call('exc')['out'],flush=True)
            c.call('shot "'+(ROOT/f'build/local/live-complete-{i}.png').as_posix()+'"')
        except Exception as error:
            cleanup_errors.append(f'{i} diagnostics: {error}')
        finally:
            try: c.call('quit')
            except Exception as error: cleanup_errors.append(f'{i} quit: {error}')
    if cleanup_errors: raise RuntimeError('; '.join(cleanup_errors))
