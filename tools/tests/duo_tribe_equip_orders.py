"""F9 guest villagers collect tools through host native AI; no forced specialization."""
import argparse
import re

from coop_ability_bar import eax, u32
from coop_ui import Ui
from duo_native_ui import require_gameplay_ui, view_tribal_point
from duo_orders import wait_until
from duo_tribe_rebuild_checkpoint import objects
from duo_tribe_rescue import citizens
from duo_tribe_tools import rack
from duo_villages import Console, command


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=int,required=True)
    parser.add_argument('--guest',type=int,required=True)
    args=parser.parse_args()
    h,g=Console(args.host),Console(args.guest)
    for c in (h,g):
        if "world 'Clyth.spo'" not in command(c,'coop.avatar'):raise RuntimeError('Use disposable Clyth copies')
        command(c,'coop.uistate 0')
        require_gameplay_ui(c)
    rows=citizens(h)
    own=[i for i,r in rows.items() if r['species']=='0680BF38' and not r['dead']]
    home=[i for i,r in rows.items() if r['species']=='067B3E4A' and not r['dead']]
    ht=u32(h,rows[own[0]]['ptr']+0x1014)
    gt=u32(g,citizens(g)[own[0]]['ptr']+0x1014)
    chief=u32(h,ht+0x11c)
    adults=[i for i in own if rows[i]['ptr']!=chief and u32(h,rows[i]['ptr']+0xb34)==1]
    if len(adults)!=2:raise RuntimeError('Expected two non-chief guest adults')
    for kind,member in ((2,own[0]),(3,home[0])):
        if rack(h,u32(h,rows[member]['ptr']+0x1014),kind):continue
        if 'fixture created' not in command(h,f'coop.tribe tool_fixture {kind} {member}'):
            raise RuntimeError('Native fixture rack failed')
    wait_until(lambda:rack(g,gt,2),'Guest rack did not mirror',10)
    ui=Ui(g,'guest')
    def button(name):
        command(g,'key f9')
        if not ui.wait(lambda s:s['open']=='1',5,'F9'):raise RuntimeError('F9 did not open')
        if ui.state().get('page')!='space':ui.click('space')
        if not ui.wait(lambda s:s.get('page')=='space' and s.get(name,{}).get('vis'),5,'Village action'):
            raise RuntimeError('Village action did not appear')
        if not ui.click(name) or not ui.wait(lambda s:s['open']=='0',5,name):raise RuntimeError(name+' failed')
    button('sbtn5')
    home_guest=u32(g,citizens(g)[home[0]]['ptr']+0x1014)
    wait_until(lambda:rack(g,home_guest,3),'Partner rack did not mirror',10)
    rejected=lambda:int(re.search(r'rejected=(\d+)',command(h,'coop.tribe'))[1])
    before=rejected()
    point=dict(objects(g,0x18C8F0C))[rack(g,home_guest,3)]
    x,y=view_tribal_point(g,point)
    button('sbtn8');command(g,f'click {x} {y}')
    wait_until(lambda:rejected()>before,'Foreign rack was not rejected',5)
    point=dict(objects(g,0x18C8F0C))[rack(g,gt,2)]
    x,y=view_tribal_point(g,point)
    button('sbtn8');command(g,f'click {x} {y}')
    def equipped(c):
        current=citizens(c)
        return all(i in current and u32(c,current[i]['ptr']+0x102c)==2 for i in adults)
    for c in (h,g):
        wait_until(lambda:equipped(c),'Selected villagers did not collect native axes',35)
    for c in (h,g):
        current=citizens(c)
        for i in adults:
            p=current[i]['ptr']
            item=eax(c,f'Simulator::cCreatureCitizen::GetHandheldItemForTool thiscall {p:#x} 2')
            wait_until(lambda:u32(c,p+0xffc)==item,'Collected tool model did not appear',5)
            if u32(c,p+0xb28)!=0x0680BF38:raise RuntimeError('Species changed')
        for i in home:
            if i in current and u32(c,current[i]['ptr']+0x102c) not in (0,11):raise RuntimeError('Host villager was equipped by guest')
        if 'total faults: 0' not in command(c,'exc'):raise RuntimeError('Native faults occurred')
    if u32(h,chief+0x102c)!=11:raise RuntimeError('Chief lost its staff')
    print('PASS F9 equips two original-species adults through native AI on both peers; foreign rack refused and chief staff preserved.',flush=True)


if __name__=='__main__':main()
