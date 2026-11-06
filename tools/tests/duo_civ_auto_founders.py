"""BUG-9046: native graduation automatically founds the second species' nation."""
import argparse,time,re
from duo_villages import Console,command
from duo_orders import wait_until
from duo_native_ui import dismiss_registration,dismiss_service_error
from duo_tribe_graduation_editor import button


def verify(h,g,species,timeout=60):
    species_keys=tuple(f'{int(key,16):08X}' for key in species)
    assert len(set(species_keys))==2
    def ready():
        results=[]
        for c in(h,g):
            dismiss_registration(c);dismiss_service_error(c)
            button(c,'0445C99D');button(c,'062EF353')
            text=command(c,'coop.civ nations')
            results.append(all('nation_species='+key in text for key in species_keys))
        return all(results)
    wait_until(ready,'The two native species capitals did not appear automatically',timeout)
    for c in(h,g):
        text=command(c,'coop.civ nations');print(text,flush=True)
        rows=re.findall(r'nation_pol=(\w+).*?nation_species=(\w+)',text)
        assert len({pol for pol,species in rows if species in species_keys})==2
        assert 'total faults: 0' in command(c,'exc')
    print('PASS two native species capitals automatically founded; zero faults.',flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host',type=int,required=True);p.add_argument('--guest',type=int,required=True)
    p.add_argument('--timeout',type=int,default=60)
    p.add_argument('--species',nargs=2,default=['067B3E4A','0680BF38'],metavar=('HOST','GUEST'))
    a=p.parse_args();verify(Console(a.host),Console(a.guest),a.species,a.timeout)
