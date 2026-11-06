"""Verify the two native Civ species keys on the isolated Windows Anisa fixture."""
import argparse
import random
import re
import struct

from duo_orders import wait_until
from duo_villages import Console, command


def nations(c):
    rows = command(c, 'coop.civ nations')
    return {pol: (flag, nat, city) for pol, flag, nat, city in re.findall(
        r'nation_pol=(\w+) player=(\d) nation_species=(\w+) city_species=(\w+)', rows)}


def native(c, args):
    answer = command(c, 'call ' + args)
    ptr = int(re.search(r'eax=(0x[0-9A-Fa-f]+)', answer)[1], 16)
    if not ptr:
        raise RuntimeError(f'Native lookup failed: {args}')
    return ptr


def view_citizens(client, pol, species):
    import play_civ  # upstream native camera and keyboard test driver
    ui = command(client, 'coop.uistate')
    play_civ.W, play_civ.H = map(int, re.search(r'screen (\d+) (\d+)', ui).groups())
    play_civ.HUD_BOTTOM = play_civ.H - 95
    target = re.search(r'^city .*?pol=' + pol + r' .*?pos=\(([-\d. ]+)\)', command(client, 'coop.civ'), re.M)
    if not target:
        raise RuntimeError('The second native city is missing.')
    driver = play_civ.Player('Census', client, 0, random.Random(0), [])
    w, h = play_civ.W, play_civ.H
    screen = driver.look_at(tuple(map(float, target[1].split())), box=(w*.35, h*.25, w*.65, h*.62), tries=18)
    if not screen:
        raise RuntimeError('Could not bring the second city into view.')
    x, y = map(round, screen)
    command(client, f'wheel {x} {y} 5')
    def actual():
        text = command(client, 'coop.civ citizens')
        rows = re.findall(r'city_pol=' + pol + r' citizen_pol=\w+ species=(\w+) count=(\d+)', text)
        return sum(int(n) for s, n in rows) if rows and all(s == species for s, n in rows) else 0
    count = wait_until(actual, 'No matching live citizens attached to the second city.', 30)
    print(f'Native second-city citizens: {count} of species {species}.')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', required=True, type=int)
    p.add_argument('--guest', required=True, type=int)
    p.add_argument('--restored', action='store_true', help='read both games after cold load, without writing anything')
    p.add_argument('--citizens', action='store_true', help='after a cold load, bring the second city into view on both games')
    args = p.parse_args()
    host, guest = Console(args.host), Console(args.guest)
    for c in (host, guest):
        if "world 'Anisa.spo'" not in command(c, 'coop.avatar'):
            raise RuntimeError('Requires the disposable Civilization fixture Anisa.')
    if 'duoqacivhost' not in command(host, 'coop.savedb').lower():
        raise RuntimeError('Expected the dedicated disposable host profile.')
    home, other, key = '800021CA', '000021CB', '0680BF38'
    edited = ('0', key, key)
    restored = ('0', key, '00000000')
    home_row = ('1', '067B4002', '00000000')
    if args.restored:
        for c in (host, guest):
            values = nations(c)
            if values.get(home) != home_row or values.get(other) != restored:
                raise RuntimeError(f'Cold native fields differ: {values}')
            if args.citizens:
                view_citizens(c, other, key)
        print('PASS: two different native nation species survived the host restart and guest rejoin.')
        return
    command(guest, f'coop.creations choose creature {key}')
    wait_until(lambda: f'player 1 creature: {key}/2B978C46/40626200' in command(host, 'coop.creations'),
               'Host never received the fixture species.')
    values = nations(host)
    if values.get(home) != home_row or values.get(other) not in (('0', '00000000', '00000000'), edited, restored):
        raise RuntimeError(f'Unexpected existing nation keys, leaving fixture untouched: {values}')
    nm = native(host, 'Simulator::cGameNounManager::Get cdecl')
    nation = native(host, f'Simulator::cGameNounManager::GetCivilizationByPoliticalID thiscall 0x{nm:X} 0x{other}')
    cities = re.findall(r'^city -?\d+ ([0-9A-Fa-f]+) pol=' + other + r'\b', command(host, 'coop.civ'), re.M)
    if len(cities) != 1:
        raise RuntimeError(f'Expected one city in the test nation: {cities}')
    fields = [nation + 0x268, int(cities[0], 16) + 0x658]
    seed = struct.pack('<III', int(key, 16), 0x2B978C46, 0x40626200)
    for addr in fields:
        row = command(host, f'mem.read 0x{addr:X} 12').split()
        if bytes.fromhex(''.join(row[1:])) not in (bytes(12), seed):
            raise RuntimeError('An existing model occupies a target field. No changes made.')
    for addr in fields:
        command(host, f'mem.write 0x{addr:X} {seed.hex()}')
    if nations(host).get(other) != edited:
        raise RuntimeError('The temporary native fixture assignment did not take.')
    old_slot = command(guest, 'coop.savedb').split('location=')[-1].splitlines()[0]
    command(host, 'coop.share due')
    def loaded():
        state = guest.call('coop.avatar')
        if not state['ok'] or "world 'Anisa.spo'" not in state['out']:
            return False
        values = nations(guest)
        slot = command(guest, 'coop.savedb').split('location=')[-1].splitlines()[0]
        return slot != old_slot and values.get(home) == home_row and values.get(other) == restored
    wait_until(loaded, 'Guest failed to load the distinct native country key into a new slot.', 100)
    print('PASS: the guest nation species transferred, the original host nation retained its species, and the city field cleared on load.')


if __name__ == '__main__':
    main()
