"""Native graduation must open City Hall promptly after both players ready.

Requires the disposable Clyth five-victory campaign already joined on both
consoles. No progression or editor request is injected. --control disables
only the graduation exception, retaining the general cinematic safety gate.
"""
import argparse,re,time
from duo_villages import Console,command
from duo_orders import wait_until
from duo_native_ui import dismiss_registration,dismiss_service_error
from coop_ui import Ui
from coop_ability_bar import u32
from duo_tribe_rebuild_checkpoint import objects


def button(c,wid):
    row=re.search(rf'^{wid} ([\d.]+) ([\d.]+) ([\d.]+) ([\d.]+) enabled=1',command(c,'coop.ui where '+wid),re.M)
    if not row:return False
    x1,y1,x2,y2=map(float,row.groups())
    click(c,round((x1+x2)/2),round((y1+y2)/2))
    return True


def click(c,x,y):
    command(c,f'move {x} {y}');time.sleep(.2)
    command(c,f'click {x} {y}');time.sleep(.3)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',type=int,required=True)
    p.add_argument('--guest',type=int,required=True)
    p.add_argument('--control',action='store_true')
    p.add_argument('--planet',default='Clyth.spo',help='Disposable five-victory campaign world.')
    p.add_argument('--guest-offline',action='store_true',help='Guest leaves before the host starts native History graduation.')
    a=p.parse_args();h,g=Console(a.host),Console(a.guest)
    for c in(h,g):
        if f"world '{a.planet}'" not in command(c,'coop.avatar'):raise RuntimeError('Wrong disposable campaign')
        tribes={u32(c,t+0x30):t for t,_ in objects(c,0x18C6D19)}
        assert 0x21CB in tribes and 0x800021CA in tribes
        totem=u32(c,tribes[0x800021CA]+0x19D0)
        assert (u32(c,totem+0x120)-u32(c,totem+0x11C))//4==5,'Need five earned victories'
        dismiss_registration(c);dismiss_service_error(c)
    print(command(h,'coop.fix tribe.graduation_editor '+('off' if a.control else 'on')),flush=True)
    assert 'on BUG-0404' in command(h,'coop.fix core.editor_cinematic on')
    if a.guest_offline:
        command(g,'coop.leave')
        wait_until(lambda:'players=1/4' in command(h,'coop.status'),'Guest departure was not acknowledged',10)
        print('Guest offline before native History advancement.',flush=True)
    click(h,1136,648);time.sleep(2)
    # Native first-use History tooltip, then its next-stage check.
    if button(h,'FFFFFFF1'):time.sleep(.5)
    click(h,1152,641)
    if a.guest_offline:
        def offline_editor():
            button(h,'0445C99D');button(h,'062EF353')
            return 'Editor' in command(h,'game.mode')
        wait_until(offline_editor,'Offline host could not open native City Hall',45)
    else:
        for _ in range(30):
            if command(h,'coop.vote').startswith('vote active'):break
            button(h,'0445C99D');button(h,'062EF353')
            time.sleep(.5)
        else:raise RuntimeError('Native graduation did not request a vote')
        command(g,'coop.uistate 1');time.sleep(.3)
        assert Ui(g,'guest').click('ready')
        wait_until(lambda: not command(h,'coop.vote').startswith('vote active'),'Ready vote did not finish',10)
    try:
        wait_until(lambda:'Editor' in command(h,'game.mode'),'City Hall blocked by native graduation pause',12)
        assert 'editor 99E92F05' in command(h,'coop.editor')
        for c in(h,g):assert 'total faults: 0' in command(c,'exc')
        print('PASS native City Hall editor opens with general cinematic guard enabled.',flush=True)
    finally:
        print(command(h,'coop.vote'),command(h,'game.mode'),flush=True)
        for c in(h,g):print(command(c,'exc'),flush=True)


if __name__=='__main__':main()
