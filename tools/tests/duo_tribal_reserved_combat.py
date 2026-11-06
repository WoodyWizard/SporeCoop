"""Check both native Tribal health bars and a real enemy citizen's hit in a four-seat fixture."""
import argparse
import re
import time

from duo_orders import wait_until
from duo_tribe_rescue import citizens
from duo_tribe_graduation_editor import button
from duo_villages import Console, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=int,required=True)
    parser.add_argument('--guest',type=int,required=True)
    args = parser.parse_args()
    h,g = Console(args.host),Console(args.guest)
    for c in (h,g):
        assert 'duoqa' in command(c,'coop.savedb').lower() and 'Tribe' in command(c,'game.mode')
    assert 'Reserved founders: 4' in command(h,'coop.status')
    def stats(peer,idx):
        out = command(peer,f'coop.tribe combat_hp {idx}')
        m = re.search(r'hp=([\d.]+) max=([\d.]+) faction=([A-F0-9]+) founder=(\d) native=([\d.]+)',out)
        return (float(m[1]),float(m[2]),int(m[3],16),int(m[4]),float(m[5])) if m else None
    if command(h,'coop.tribe villages').count('village ') < 3:
        assert 'npc tribe' in command(h,'coop.tribe npc_tribe 3 clear 067C684A')
    time.sleep(2)
    def playable():
        for peer in (h,g):
            for widget in ('0445C99D','062EF353'):
                button(peer,widget)
        return 'cinematic' not in re.search(r'^busy: (.*)',command(h,'coop.status'),re.M)[1]
    wait_until(playable,'Native rival-formation cinematic is still protecting the founders',45)
    host = citizens(h)
    adults = {int(i) for i in re.findall(r'#(\d+) [\dA-F]+ owner=\d+ age=1 ',command(h,'coop.tribe'))}
    available = [(idx,row,stats(h,idx)) for idx,row in host.items() if idx in adults and not row['dead'] and row['hp']>10 and not row['timer']]
    defenders = [r for r in available if r[2] and r[2][3]]
    outsiders = [r for r in available if r[2] and not r[2][3]]
    assert defenders and outsiders, 'Fixture needs an adult founder and a rival warrior'
    victim_id,victim,_ = max(defenders,key=lambda r:r[2][0])
    matched = None
    attempted = set()
    for idx,enemy,base in outsiders:
        if base[2] in attempted: continue
        attempted.add(base[2])
        old = stats(h,victim_id)
        outcome = command(h,f'coop.tribe pincer {victim_id} {idx} 4')
        native_hit = re.search(r'hp ([\d.]+) -> ([\d.]+)',outcome)
        hit = stats(h,victim_id)
        actual = stats(h,idx)
        if abs(actual[1]-base[4]*1.75) < .02:
            loss = float(native_hit[1])-float(native_hit[2])
            assert abs(loss-5.2)<.05, ('Unexpected native damage',old,hit,outcome)
            assert abs(old[1]-hit[1])<.01, 'Founder HP was buffed'
            wait_until(lambda:stats(g,idx) and abs(stats(g,idx)[1]-actual[1])<.02,
                       'Guest did not receive the warrior max HP',15)
            matched = (idx,loss,base[4],actual[1]);break
    assert matched, 'No non-allied rival entered combat'
    assert all('total faults: 0' in command(c,'exc') for c in (h,g))
    print('PASS warrior id, native damage, original max and both games’ new max:',matched,flush=True)


if __name__ == '__main__':
    main()
