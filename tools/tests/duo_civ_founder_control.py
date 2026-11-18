"""BUG-9054: separate native capital ownership, a shared treasury, and guest orders."""
import argparse,re,time
from duo_villages import Console,command
from duo_orders import wait_until
from duo_civ_species import native
from coop_ability_bar import u32


def cities(c):
    return [(int(i),int(ptr,16),int(pol,16),int(owner)) for i,ptr,pol,owner in re.findall(
        r'^city (-?\d+) ([0-9A-F]+) pol=([0-9A-F]+).*? owner=(-?\d+)',command(c,'coop.civ'),re.M)]


def own(c,spatial,symbol):
    text=command(c,f'call {symbol} thiscall 0x{spatial:X}')
    return int(re.search(r'eax=0x([0-9a-f]+)',text,re.I)[1],16)&255


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',type=int,required=True);p.add_argument('--guest',type=int,required=True)
    a=p.parse_args();h,g=Console(a.host),Console(a.guest)
    for c,me in((h,0),(g,1)):
        rows=cities(c);assert len(rows)==2,rows
        assert sorted(owner for _,_,_,owner in rows)==[0,1],rows
        for _,city,_,owner in rows:
            # The original native vtable dispatch must agree with ownership.
            address=u32(c,u32(c,city+0x120)+0x58)
            assert own(c,city+0x120,f'0x{address:X}')==(owner==me), 'Native city ownership differs'
    before=command(h,'coop.civ')
    time.sleep(12)
    assert command(h,'coop.civ').count('  veh #')==before.count('  veh #'), 'Partner AI bought vehicles'
    idx,city,pol,_=next(row for row in cities(h) if row[3]==1)
    prev=set(re.findall(r'  veh #(\d+)',command(h,'coop.civ')))
    money_before=float(re.search(r'player=1 .*?wealth=([\d.]+)',command(h,'coop.civ nations'))[1])
    print(command(g,f'coop.civ buy {idx} 0'),flush=True)
    def spawned():
        return next(((int(i),ptr) for i,ptr,p,owner in re.findall(
            r'  veh #(\d+) ([0-9A-F]+) pol=([0-9A-F]+) owner=(\d+)',command(h,'coop.civ'))
            if i not in prev and int(p,16)==pol and owner=='1'),None)
    vehicle,_=wait_until(spawned,'Guest purchase did not produce its own native vehicle',12)
    wait_until(lambda:f'veh #{vehicle} ' in command(g,'coop.civ'),'Guest mirror absent',12)
    def shared_budget():
        values=[]
        for c in(h,g):
            values.extend(map(float,re.findall(r'available=([\d.]+)',command(c,'coop.civ nations'))))
        return len(values)==4 and len(set(values))==1 and values[0]<money_before
    wait_until(shared_budget,'Both native nations did not charge the same shared treasury',12)
    command(g,f'coop.civ move {vehicle} 20 490 90')
    wait_until(lambda:re.search(rf'veh #{vehicle} .*?opos=\(20\.0 490\.0 90\.0\)',command(h,'coop.civ')),
               'Native host order did not receive the guest destination',10)
    for c in(h,g):assert 'total faults: 0' in command(c,'exc')
    print('PASS native city ownership, no idle AI purchases, own-country vehicle and forwarded native order.',flush=True)


if __name__=='__main__':main()
