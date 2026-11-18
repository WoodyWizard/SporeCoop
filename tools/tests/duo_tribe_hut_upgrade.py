"""Both native founder huts receive the shared campaign's earned level."""
import argparse
import struct
from coop_ability_bar import mem, u32
from duo_orders import wait_until
from duo_tribe_rescue import citizens
from duo_villages import Console, command


def state(c):
    tribes = {r['species']: u32(c,r['ptr']+0x1014) for r in citizens(c).values() if not r['dead']}
    result = {}
    for species in ('067B3E4A','0680BF38'):
        if species not in tribes: return None
        tribe = tribes[species]
        hut = u32(c,tribe+0x368)
        if not hut: return None
        result[species] = dict(level=u32(c,tribe+0x38),
                               maximum=struct.unpack('<f',mem(c,hut+0x14C,4))[0],
                               health=struct.unpack('<f',mem(c,hut+0x158,4))[0],
                               faction=u32(c,tribe+0x30))
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host',type=int,required=True)
    parser.add_argument('--guest',type=int,required=True)
    args = parser.parse_args()
    h,g = Console(args.host),Console(args.guest)
    for c in (h,g):
        if "world 'Clyth.spo'" not in command(c,'coop.avatar'):
            raise RuntimeError('Use the earned-alliance disposable Clyth checkpoint')
    command(h,'coop.tribe village_orders on')
    before = [state(c) for c in (h,g)]
    print('Native founder huts:',before,flush=True)
    def matched():
        for c in (h,g):
            rows = state(c)
            if not rows: return False
            home,guest = rows.values()
            if home['level'] != 1 or guest['level'] != 1 or home['maximum'] != guest['maximum']:
                return False
            if home['faction'] == guest['faction']: raise RuntimeError('Founder factions merged')
        return True
    wait_until(matched,'Guest native hut did not receive the earned shared upgrade',15)
    for c in (h,g):
        if 'total faults: 0' not in command(c,'exc'): raise RuntimeError('Native hut upgrade fault')
    print('PASS both founder huts share native level and maximum health on both peers.',flush=True)


if __name__=='__main__': main()
