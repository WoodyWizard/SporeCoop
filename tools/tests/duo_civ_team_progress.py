"""BUG-9056: native capture credit and technology across two founder nations.

Requires disposable joined Clyth with exactly two founder capitals. Explicit
native spawn/capture calls construct the regression fixture, not evidence of
player-earned combat. Reads real nation city vectors, technology and counters.
"""
import argparse
import re
import time

from coop_ability_bar import mem, u32
from duo_civ_founder_control import cities
from duo_civ_species import native
from duo_orders import wait_until
from duo_villages import Console, command
from duo_tribe_graduation_editor import button


def country(c, faction):
    manager = native(c, 'Simulator::cGameNounManager::Get cdecl')
    return native(c, f'Simulator::cGameNounManager::GetCivilizationByPoliticalID thiscall {manager:#x} {faction:#x}')


def state(c, faction):
    nation = country(c, faction)
    begin, end = u32(c, nation + 0x9C), u32(c, nation + 0xA0)
    assert end >= begin and (end - begin) % 4 == 0
    count = (end - begin) // 4
    assert count <= 12, f'Invalid native city vector: {count}'
    actual = [u32(c, begin + 4 * i) for i in range(count)]
    assert len(set(actual)) == count, 'Duplicate native city pointers'
    assert all(u32(c, city + 0x590) == nation for city in actual), 'Foreign city in ownership vector'
    return dict(cities=count, tech=u32(c, nation + 0x298),
                highest=u32(c, nation + 0x4D0),
                captures=tuple(u32(c, nation + 0x4C0 + 4 * i) for i in range(3)),
                unlocks=mem(c, nation + 0x484, 12).hex(), species=u32(c, nation + 0x268))


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', type=int, required=True)
    p.add_argument('--guest', type=int, required=True)
    p.add_argument('--planet', choices=('Clyth', 'Vantiegan'), default='Clyth')
    p.add_argument('--space-ready', action='store_true', help='Test the native readiness threshold at each capture through ten owned cities.')
    a = p.parse_args()
    h, g = Console(a.host), Console(a.guest)
    for c in (h, g):
        assert 'duoqa' in command(c, 'coop.savedb').lower(), 'Disposable profile required'
        assert f"world '{a.planet}.spo'" in command(c, 'coop.avatar'), 'Wrong disposable campaign'
    initial = cities(h)
    assert len(initial) == 2 and sorted(r[3] for r in initial) == [0, 1], initial
    home, guest = (next(r[2] for r in initial if r[3] == owner) for owner in (0, 1))
    before = {pol: state(h, pol) for pol in (home, guest)}
    print('BEFORE', before, flush=True)
    expansion = native(h, 'Simulator::cCivExpansion::Get cdecl')
    captures = 8 if a.space_ready else 2
    for step in range(1, captures + 1):
        faction = guest if step % 2 else home
        previous = {r[1] for r in cities(h)}
        target = next((r for r in cities(h) if r[2] not in (home, guest)), None) if a.space_ready else None
        if not target:
            command(h, f'call Simulator::cCivExpansion::SpawnNextCity thiscall {expansion:#x} 0 0')
            target = wait_until(lambda: next((r for r in cities(h) if r[1] not in previous), None),
                                'NPC fixture city did not spawn', 20)
        assert target[2] not in (home, guest), target
        print('FIXTURE CAPTURE', step, hex(target[2]), 'by', hex(faction), flush=True)
        command(h, f'call Simulator::cCity::Capture thiscall {target[1]:#x} {faction:#x} 0 0 0 0')
        wait_until(lambda: next((r for r in cities(h) if r[1] == target[1] and r[2] == faction), None),
                   'Native capture did not finish', 40)
        observed = state(h, home)
        print('CAPTURE RESULT', observed, state(h, guest), flush=True)
        assert observed['captures'][0] == before[home]['captures'][0] + step, 'Guest capture did not advance shared native campaign counter'
        wait_until(lambda: all(sum(r[2] in (home,guest) for r in cities(c)) == 2 + step for c in (h, g)),
                   'Founder city count differs on guest', 45)
        def checkpoint():
            # A player acknowledges the native capture/technology captions.
            # Save confirmation is the mod's responsibility, not this driver.
            for c in (h, g):
                button(c, '0445C99D')
                button(c, '062EF353')
                button(c, '05107B1A')  # choose military for the captured religious city
            if 'Civ' not in command(g, 'game.mode'):
                return False
            try:
                return state(g, home)['captures'] == observed['captures']
            except RuntimeError as error:
                if str(error).startswith('Native lookup failed:'):
                    return False  # checkpoint load changed mode between commands
                raise
        wait_until(checkpoint, 'Native campaign counter did not survive its automatic checkpoint', 90)
        if a.space_ready:
            for peer in (h,g):
                result = command(peer, 'call SporeCoop::CivMode_SpaceReady cdecl')
                ready = int(re.search(r'eax=0x([0-9a-f]+)', result, re.I)[1], 16) & 255
                assert ready == int(2 + step == 10), (step, result)
            print('PASS native Space readiness at', 2+step, 'team cities:', ready, flush=True)
    def complete():
        rows = [[state(c, pol) for pol in (home, guest)] for c in (h, g)]
        return rows if all(s['cities'] == 1 + captures//2 and s['tech'] >= (4 if a.space_ready else 3) and s['highest'] >= 4
                           for pair in rows for s in pair) and rows[0] == rows[1] else None
    rows = wait_until(complete, 'Shared four-city tech/credit did not converge on both native games', 90)
    assert len({s['unlocks'] for pair in rows for s in pair}) == 1, rows
    for pair in rows:
        assert [s['species'] for s in pair] == [before[p]['species'] for p in (home, guest)]
    print('FINAL', rows, flush=True)
    time.sleep(5)
    for c in (h, g):
        assert 'total faults: 0' in command(c, 'exc')
        assert 'Civ' in command(c, 'game.mode')
    print('PASS shared native capture totals, technology, unlocks and separate ownership on both games.', flush=True)


if __name__ == '__main__':
    main()
