"""Exercise the actual damage filter and native HP getters on the four-seat Creature fixture."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import re
import time

from duo_orders import wait_until
from duo_villages import Console, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('host','guest','port'):
        parser.add_argument('--' + key,type=int,required=True)
    parser.add_argument('--current-defaults', action='store_true',
                        help='Use a 15/5, 250-basis-point four-seat fixture and verify both hunger meters.')
    args = parser.parse_args()
    health_factor, damage_factor = (1.45, 1.15) if args.current_defaults else (1.75, 1.3)
    h,g = Console(args.host),Console(args.guest)
    def settle(peer):
        wait_until(lambda:peer.call('ping')['ok'],'No lab console',60)
        wait_until(lambda:'GalaxyGameEntry' in command(peer,'game.mode'),'No menu',90)
        assert 'duoqa' in command(peer,'coop.savedb').lower()
        command(peer,'wait.frames 600 120000')
    with ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(settle,(h,g)))
    command(h,'coop.load Game0 QuartetHunt')
    wait_until(lambda:'Creature' in command(h,'game.mode'),'No Creature stage',75)
    command(h,f'coop.host {args.port} PackHost')
    assert 'Reserved founders: 4' in command(h,'coop.status')
    command(g,f'coop.join 127.0.0.1:{args.port} PackGuest')
    wait_until(lambda:'Creature' in command(g,'game.mode') and 'QuartetHunt' in command(g,'coop.avatar'),
               'Guest did not join the Creature campaign',90)
    if args.current_defaults:
        wait_until(lambda: 'Host campaign rules: 4 reserved founders' in command(g,'coop.status'),
                   'Guest did not receive the active campaign rules',20)
        def sample(peer):
            raw = command(peer,'coop.hp hunger')
            hit = re.search(r'samples=(\d+) previous=([\d.]+) requested=([-\d.]+) result=([\d.]+)',raw)
            if not hit:
                return None
            number, previous, requested, result = map(float,hit.groups())
            return (number,(previous-result)/(previous-requested)) if previous-requested > .001 and result > 0 else None
        for peer in (h,g):
            first = wait_until(lambda:sample(peer),'No native hunger drain recorded',90)
            observed = wait_until(lambda:(x if (x:=sample(peer)) and x[0]>first[0] else None),
                                  'No subsequent native hunger update',20)
            assert abs(observed[1]-1.075) < .002, (first,observed)
            print('PASS native Creature hunger factor',observed[1],flush=True)
    def stats(peer,idx):
        result = peer.call(f'coop.hp enemy {idx}')
        m = re.search(r'hp=([\d.]+) max=([\d.]+) native=([\d.]+) worth=([\d.]+)',result['out'])
        return tuple(map(float,m.groups())) if m else None
    def candidate():
        for line in command(h,'coop.npc list').splitlines():
            m = re.search(r'#(\d+) [\dA-F]+ species=([\dA-F]+) hp=([\d.]+)',line)
            if m and float(m[3])>2 and 'family' not in line and 'DEAD' not in line:
                idx = int(m[1])
                local,remote = stats(h,idx),stats(g,idx)
                if local and remote and local[3] > 0 and local[1] == local[2] and abs(local[0]-local[1]) < .02:
                    return idx,local
        return None
    idx,prior = wait_until(candidate,'No living, unengaged NPC mirrored on both players',70)
    def avatar_hp(peer):
        m = re.search(r'hp=([\d.]+)/([\d.]+)',command(peer,'coop.avatar'))
        return tuple(map(float,m.groups())) if m else None
    wait_until(lambda:avatar_hp(g) and avatar_hp(g)[0] > 4,'Guest avatar unavailable',30)
    player = avatar_hp(g)
    command(h,f'coop.npc hurt 1 {idx} 2')
    def received():
        current = avatar_hp(g)
        return (player[0]-current[0]) if current and abs(player[0]-current[0]-2*damage_factor)<.12 and current[1] == player[1] else None
    observed = wait_until(received,'Guest did not receive the configured enemy damage',10)
    target = prior[2]*health_factor
    def buffed():
        a,b=stats(h,idx),stats(g,idx)
        return a and b and abs(a[1]-target)<.02 and abs(b[1]-target)<.02 and abs(a[0]-b[0])<.2
    wait_until(buffed,'Native hostile max HP did not agree on both games',15)
    print('PASS enemy health',prior[2],'->',stats(h,idx),stats(g,idx),'guest HP loss at receipt',observed,flush=True)
    command(g,f'coop.npc hit {idx} 1')
    wait_until(lambda:stats(h,idx) and stats(h,idx)[0] < target-.2 and
               abs(stats(h,idx)[0]-stats(g,idx)[0]) < .2,'Hostile HP did not follow guest hit',12)
    def dna(peer):
        return float(re.search(r'dna=([-\d.]+)',command(peer,'coop.dna'))[1])
    cash = dna(h)
    command(g,f'coop.npc hit {idx} 250')
    assert wait_until(lambda:dna(h)>cash and abs(dna(h)-dna(g))<.11,'Native kill reward did not reach both players',15)
    print('Native kill worth',prior[3],'shared DNA gain',dna(h)-cash,flush=True)
    assert abs(dna(h)-cash-prior[3]/4) < .12, 'Hunt credited an incorrect amount'
    cash = dna(h)
    command(h,'call Simulator::cCreatureGameData::AddEvolutionPoints cdecl f:4')
    wait_until(lambda:abs(dna(h)-cash-4)<.12,'Non-activity credit was scaled',5)
    command(h,'call Simulator::cCreatureGameData::AddEvolutionPoints cdecl f:-4')
    wait_until(lambda:abs(dna(h)-cash)<.12,'A debit was scaled',5)
    assert 'total faults: 0' in command(h,'exc') and 'total faults: 0' in command(g,'exc')


if __name__ == '__main__':
    main()
