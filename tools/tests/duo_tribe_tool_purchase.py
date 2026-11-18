"""Guest F9 rack purchases: native unlocks, food, placement, duplicate refusal."""
import argparse
import math
import re
import struct

from coop_ability_bar import eax, mem, u32
from coop_ui import Ui
from duo_food import food
from duo_native_ui import dismiss_registration, require_gameplay_ui, view_tribal_point
from duo_orders import wait_until
from duo_tribe_rescue import citizens, move
from duo_tribe_tools import rack
from duo_villages import Console, command


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',type=int,required=True)
    p.add_argument('--guest',type=int,required=True)
    args=p.parse_args()
    h,g=Console(args.host),Console(args.guest)
    own,partner='0680BF38','067B3E4A'
    for c in (h,g):
        if "world 'Clyth.spo'" not in command(c,'coop.avatar'):raise RuntimeError('Use disposable Clyth copies')
        command(c,'coop.uistate 0')
        dismiss_registration(c);require_gameplay_ui(c)
    rows=citizens(h)
    identity=next(i for i,r in rows.items() if r['species']==own and not r['dead'])
    ht=u32(h,rows[identity]['ptr']+0x1014)
    gt=u32(g,citizens(g)[identity]['ptr']+0x1014)
    if rack(h,ht,1):raise RuntimeError('Fresh fixture required')
    anchor=struct.unpack('<3f',mem(h,ht+0x124,12))
    radius=math.sqrt(sum(v*v for v in anchor));up=[v/radius for v in anchor]
    tangent=[-up[2],0,up[0]]
    length=math.sqrt(sum(v*v for v in tangent));tangent=[v/length for v in tangent]
    side=[up[1]*tangent[2]-up[2]*tangent[1],up[2]*tangent[0]-up[0]*tangent[2],up[0]*tangent[1]-up[1]*tangent[0]]
    # Geometry fixture only. The actual purchase still uses the F9 button and
    # a ground click; no CreateTool/SetPurchasedTool call is made by this test.
    point=[anchor[i]+24*tangent[i] for i in range(3)]
    length=math.sqrt(sum(v*v for v in point));point=[v*radius/length for v in point]
    ui=Ui(g,'guest')
    rejected=lambda:int(re.search(r'rejected=(\d+)',command(h,'coop.tribe'))[1])
    def balance(value):
        command(h,f'call SporeCoop::cTribe_AddFood thiscall {ht:#x} {round(value-food(h)[own])}')
        wait_until(lambda:abs(food(h)[own]-value)<.01 and abs(food(g)[own]-value)<.01,'Food fixture failed')
    def build(kind,at):
        x,y=view_tribal_point(g,at)
        command(g,'key f9')
        if not ui.wait(lambda s:s['open']=='1',5,'F9'):raise RuntimeError('F9 unavailable')
        if ui.state().get('page')!='space':ui.click('space')
        if not ui.wait(lambda s:s.get('page')=='space',5,'Village'):raise RuntimeError('Village unavailable')
        if ui.state().get('sbtn1',{}).get('text')!='Build':ui.click('snext')
        if not ui.wait(lambda s:s.get('sbtn1',{}).get('text')=='Build',5,'Build page'):raise RuntimeError('Build page unavailable')
        if not ui.click(f'sbtn{kind}') or not ui.wait(lambda s:s['open']=='0',5,'Build'):raise RuntimeError('Build failed')
        command(g,f'click {x} {y}')
    balance(0)
    before=rejected();build(1,point)
    wait_until(lambda:rejected()>before,'Unaffordable rack was not refused',5)
    if rack(h,ht,1) or food(h)[own]!=0:raise RuntimeError('Unaffordable purchase mutated village')
    print('PASS unaffordable F9 purchase refused.',flush=True)
    balance(75)
    # Isolate host balance from native eating while asserting the guest charge.
    for row in rows.values():
        if row['species']==partner and not row['dead']:
            body=row['ptr'];rec=eax(h,f'Simulator::cCreatureCitizen::DoAction thiscall {body:#x} 4 0 0')
            if rec:command(h,f'call App::Property::SetValueVector3 thiscall {rec+0x39:#x} {ht+0x124:#x}')
            move(h,body,ht+0x124)
    if 'unlock' not in command(g,'coop.tribe build 2'):raise RuntimeError('Locked axes allowed')
    before=rejected();build(1,anchor)
    wait_until(lambda:rejected()>before,'Hut-overlapping placement not refused',5)
    if rack(h,ht,1) or food(h)[own]!=75:raise RuntimeError('Invalid placement spent food')
    host_food=food(h)[partner]
    build(1,point)
    wait_until(lambda:rack(h,ht,1) and rack(g,gt,1),'Purchased rack missing on a peer',10)
    cost=u32(h,eax(h,'Simulator::GetTribeToolData cdecl 1')+0x38)
    wait_until(lambda:abs(food(h)[own]-(75-cost))<.01 and abs(food(g)[own]-(75-cost))<.01,'Rack charge incorrect',5)
    if food(h)[partner]!=host_food:raise RuntimeError('Partner food changed during purchase')
    if not u32(h,ht+0x19d8)&2:raise RuntimeError('Native purchase history missing')
    before=rejected();build(1,point)
    wait_until(lambda:rejected()>before,'Duplicate rack was not refused',5)
    if food(h)[own]!=75-cost:raise RuntimeError('Duplicate purchase charged again')
    for c in (h,g):
        if 'total faults: 0' not in command(c,'exc'):raise RuntimeError('Native faults occurred')
    print('PASS F9 construction mirrors native rack, charges guest once, preserves partner food, refuses locked/overlapping/duplicate purchases.',flush=True)


if __name__=='__main__':main()
