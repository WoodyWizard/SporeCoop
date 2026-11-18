"""Guest instrument input must drive Spore's own performance and relationship result.

Use disposable Clyth profiles with AudioMasterVolume 0, without -nosound.
The audio engine emits native round/request events even when output is muted.
"""
import argparse
import math
import random
import re
import struct
import time

from coop_ability_bar import eax, mem, u32
from coop_ui import Ui
from duo_native_ui import require_gameplay_ui, view_tribal_point
from duo_orders import inspect, wait_until
from duo_tribe_rescue import citizens, move
from duo_food import food
from duo_tribe_graduation_editor import button
from duo_villages import Console, command


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', type=int, required=True)
    parser.add_argument('--guest', type=int, required=True)
    parser.add_argument('--planet', default='Clyth.spo', help='Exact disposable campaign world.')
    parser.add_argument('--species', nargs=2, default=['067B3E4A', '0680BF38'],
                        metavar=('HOST', 'GUEST'), help='Current native founder models.')
    parser.add_argument('--control', action='store_true')
    parser.add_argument('--ui', action='store_true', help='Start through F9 Perform and an actual NPC click')
    parser.add_argument('--repeat', action='store_true', help='Require a second native performance win')
    parser.add_argument('--trace-dispatch', action='store_true', help='Require a new host performance order and inspect its native dispatch.')
    parser.add_argument('--feed-first', action='store_true', help='Run the native meal control for a hungry band before the performance.')
    parser.add_argument('--alliance', action='store_true', help='Also require the second win and shared campaign totem')
    parser.add_argument('--six-rival-trial', action='store_true', help='Verify score and totem milestones in the enrolled three-victory fixture.')
    parser.add_argument('--faction', type=lambda s:int(s,16), help='Explicit native NPC faction for continued campaign qualification.')
    parser.add_argument('--band-size', type=int, choices=range(3,13), default=6)
    parser.add_argument('--instruments', type=int, choices=(4,5,6), nargs='+', default=[4,5])
    args = parser.parse_args()
    h, g = Console(args.host), Console(args.guest)
    for c in (h, g):
        if f"world '{args.planet}'" not in command(c, 'coop.avatar'):
            raise RuntimeError('Wrong disposable campaign world')
        command(c, 'coop.uistate 0')
        require_gameplay_ui(c)
    if not eax(h, 'Audio::AudioSystem::Get cdecl'):
        raise RuntimeError('Native audio engine unavailable: remove -nosound and mute the disposable profile volume.')
    command(h, 'coop.tribe village_orders on')
    home_model, own = [f'{int(key,16):08X}' for key in args.species]
    recipient = '067C684A'
    rows = citizens(h)
    performers = [i for i, r in rows.items() if r['species'] == own and not r['dead']]
    target = next(u32(h, r['ptr'] + 0x1014) for r in rows.values()
                  if (u32(h, u32(h,r['ptr']+0x1014)+0x30)==args.faction if args.faction is not None else r['species']==recipient))
    faction = u32(h, target + 0x30)
    leader = u32(h, target + 0x11C)
    leader_id = next(i for i, row in rows.items() if row['ptr'] == leader)
    if args.ui:
        view_tribal_point(g, citizens(g)[leader_id]['pos'])
    owner = u32(h, rows[performers[0]]['ptr'] + 0x1014)
    own_faction = u32(h, owner + 0x30)
    # A full native-capped band is fixture setup, not a forced score/reward.
    # SpawnMember(age=1) also performs normal adult registration and ownership.
    spawn = u32(h, u32(h, owner) + 0x98)
    for _ in range(max(0,args.band_size-len(performers))):
        if not eax(h, f'{spawn:#x} thiscall {owner:#x} 1'):
            raise RuntimeError('Could not prepare the requested native band')
    wait_until(lambda: sum(r['species'] == own and not r['dead'] for r in citizens(g).values()) == max(args.band_size,len(performers)),
               'Band population did not synchronize')
    rows = citizens(h)
    performers = [i for i, r in rows.items() if r['species'] == own and not r['dead']]
    for tribe in {u32(h, r['ptr'] + 0x1014) for r in rows.values()
                  if r['species'] in (own, home_model)}:
        command(h, f'call SporeCoop::cTribe_AddFood thiscall {tribe:#x} 100')
    if args.feed_first:
        assert all('duoqa' in command(peer, 'coop.savedb').lower() for peer in (h,g))
        mat = u32(h, owner + 0x260)
        assert mat and u32(h, mat + 0x10C) == owner
        for identity in performers:
            move(h, rows[identity]['ptr'], mat + 0x38)
        before = food(h)[own]
        command(g, 'coop.tribe select_village')
        command(g, 'coop.tribe eat')
        wait_until(lambda: food(h)[own] < before, 'Band consumed no village food', 40)
        def finished_meal():
            current = citizens(h)
            return all(i in current and not current[i]['dead'] and
                       u32(h, u32(h,current[i]['ptr']+0xB4C)+0x1D8) != 0x461300F for i in performers)
        wait_until(finished_meal, 'Band did not finish its meal', 60)
        rows = citizens(h)
        print('Native meal consumed village food:', before, 'to', food(h)[own], flush=True)
    musician = 0
    game=eax(h,'SporeCoop::TribalGame_Get cdecl')
    for tool in args.instruments:
        assert eax(h,f'SporeCoop::TribalGame_IsToolUnlocked thiscall {game:#x} {tool}') & 255, 'Fixture cannot equip an unearned instrument'
    for identity in performers:
        actor = rows[identity]['ptr']
        if u32(h, actor + 0x102C) != 11:
            tool = args.instruments[musician % len(args.instruments)]
            musician += 1
            command(h, f'call SporeCoop::Citizen_SetSpecializedTool thiscall {actor:#x} {tool}')
            item = eax(h, f'Simulator::cCreatureCitizen::GetHandheldItemForTool thiscall {actor:#x} {tool}')
            command(h, f'call SporeCoop::Citizen_SetHandheldItem thiscall {actor:#x} {item} -1')
        # Place near the village before ordering; Spore still owns the entire
        # approach and formation. Do not stack the group on the audience chief.
        move(h, actor, target + 0x124)
    command(g, 'coop.tribe select_village')

    def guest_factions():
        # The UI selects a mirrored citizen, not a host-local political ID.
        # Re-read both owners because an earned checkpoint can replace them.
        current = citizens(g)
        leader = current.get(leader_id)
        performer = next((current[i] for i in performers if i in current and current[i]['species'] == own), None)
        if not leader or not performer:
            return None
        owners = [u32(g, row['ptr'] + 0x1014) for row in (leader, performer)]
        return tuple(u32(g, t + 0x30) for t in owners) if all(owners) else None

    def relation(c):
        pair = (faction, own_faction) if c is h else guest_factions()
        if not pair:
            return None
        text = command(c, f'coop.tribe relation {pair[0]:X} {pair[1]:X}')
        try:
            return float(text.split('=')[-1])
        except ValueError:
            return None  # the earned campaign checkpoint may be loading

    def rejected():
        return int(re.search(r'rejected=(\d+)', command(h, 'coop.tribe'))[1])

    def start():
        # A broadcast checkpoint replaces the guest's native actors and their
        # selection. Select the current band as a player would before giving
        # an order. Only retry selection, never a submitted performance.
        wait_until(lambda: command(g, 'coop.tribe select_village') ==
                   f'Guest villagers selected: {len(performers)}',
                   'Guest cannot select the current band after synchronization', 30)
        if not args.ui:
            local, _ = wait_until(guest_factions, 'Guest performer or audience missing')
            response = command(g, f'coop.tribe perform_to {local:X}')
            if response != 'Performance requested.':
                raise RuntimeError(f'Guest could not request performance: {response}')
            return
        ui = Ui(g, 'guest')
        command(g, 'key f9')
        if not ui.wait(lambda s: s['open'] == '1', 5, 'F9'):
            raise RuntimeError('F9 unavailable')
        if ui.state().get('page') != 'space':
            ui.click('space')
        wait_until(lambda: ui.state().get('page') == 'space', 'Village page unavailable')
        # The arrows clamp; Previous does not wrap from page one to the end.
        for _ in range(3):
            ui.click('sprev')
        for _ in range(4):
            key = next((k for k, v in ui.state().items() if k.startswith('sbtn') and isinstance(v, dict)
                        and v.get('vis') and v.get('text') == 'Perform'), None)
            if key:
                if not ui.click(key) or not ui.wait(lambda s: s['open'] == '0', 5, 'Perform'):
                    raise RuntimeError('Perform did not close F9')
                import play_civ
                driver = play_civ.Player('Tribal target', g, 1, random.Random(0), [])
                point = driver.screen_of(citizens(g)[leader_id]['pos'])
                if not point:
                    raise RuntimeError('NPC performer target is outside the gameplay view')
                command(g, f'click {round(point[0])} {round(point[1])}')
                print('F9 Perform target click:', tuple(map(round, point)), flush=True)
                return
            ui.click('snext')
            time.sleep(.15)
        raise RuntimeError('F9 Perform button not found')

    if args.control:
        command(h, 'coop.fix tribe.performance off')
        before = rejected()
        start()
        wait_until(lambda: rejected() > before, 'Old performance rejection did not reproduce')
        command(h, 'coop.fix tribe.performance on')
        print('FAIL control reproduced: separate-village performance rejected with fix off.', flush=True)

    def run_round():
        initial = relation(h)
        print('Initial native relation:', initial, flush=True)
        done = inspect(h)[1]
        def dispatch():
            reply = command(h,'coop.tribe performance_orders')
            result = dict((key,int(value)) for key,value in re.findall(r'(\w+)=(\d+)',reply))
            assert 'sequence' in result, 'Host performance dispatch diagnostics unavailable'
            return result
        before = dispatch()['sequence'] if args.trace_dispatch else None
        start()
        if args.trace_dispatch:
            def delivered():
                now = dispatch()
                return now if now['sequence'] != before and now['target'] == leader_id and now['dispatched'] == len(performers) else None
            try:
                result = wait_until(delivered, 'Native dispatch did not cover the band')
            finally:
                print('Latest native performance dispatch:', dispatch(), flush=True)
            assert result['records'] == result['dispatched'], 'Native DoAction returned no record for some performers'
        else:
            wait_until(lambda: inspect(h)[1] >= done + len(performers), 'Host rejected native performers')
        def session_started():
            if 'member=' in command(g, 'coop.tribe performance'):
                return True
            # A newly formed NPC is selectable before its introduction ends.
            # Spore accepts the order but pauses every citizen until Continue.
            caption = h.call('coop.ui find has formed on your planet')
            flags = re.findall(r'fl=0x([0-9A-F]+)', caption.get('out', ''), re.I)
            if caption['ok'] and flags and all(int(flag, 16) & 1 for flag in flags):
                button(h, '0445C99D')
            return False
        try:
            wait_until(session_started, 'No guest performance snapshot', 45)
        except RuntimeError:
            print(command(h, 'coop.tribe'), flush=True)
            print(command(g, 'coop.tribe'), flush=True)
            raise
        deadline = time.monotonic() + 120
        highest = 0.0
        native_rounds = False
        sent = 0
        last = ''
        while time.monotonic() < deadline:
            state = command(g, 'coop.tribe performance')
            progress = [float(x) for x in re.findall(r'progress=([\d.]+)', state)]
            highest = max([highest] + progress)
            if state.splitlines()[0] != last:
                last = state.splitlines()[0]
                print(last, flush=True)
            if 'ready=1' in state:
                # Exercise the game's actual key callback, not the debug input helper.
                requested = re.search(r'request=([456]) ready=1', state)
                if not requested:
                    raise RuntimeError('Ready performance has no valid instrument request')
                command(g, f'key {int(requested[1]) - 3}')
                sent += 1
            # Keep the reaction loop responsive: certify native rounds once,
            # then validate all members after the session. Reading every field
            # of every musician here delays the next real keypress.
            if not native_rounds and 'phase=5' in state:
                identity = int(re.search(r'member=(\d+)', state)[1])
                actor = citizens(h)[identity]['ptr']
                ai = u32(h, actor + 0xB4C)
                if u32(h, ai + 0x1D8) == 0xB46E8803:
                    block = u32(h, ai + 0x600)
                    session = eax(h, f'SporeCoop::SocialSession_Get cdecl {u32(h, block + 0x1A8)}')
                    native_rounds = bool(session and u32(h, session + 0x48) > 0)
            if relation(h) > initial + 1:
                break
            if native_rounds and state == 'No active guest performance.':
                if highest < .5 or not sent:
                    raise RuntimeError('Instrument input did not produce substantial native progress')
                print('Native performance lost:', highest, 'inputs', sent, flush=True)
                return False
            time.sleep(.05)
        else:
            print('Failure:', 'score', highest, 'inputs', sent, 'rounds', native_rounds,
                  'relations', relation(h), relation(g), flush=True)
            print(command(h, 'coop.tribe'), flush=True)
            raise RuntimeError('Guest keys did not produce a native performance win')
        if not native_rounds or highest < .5 or not sent:
            raise RuntimeError('Relationship changed without observed native rounds, score and guest inputs')
        wait_until(lambda: (remote := relation(g)) is not None and abs(relation(h) - remote) < .001,
                   'Native relationship differs on guest', 45)
        wait_until(lambda: command(g, 'coop.tribe performance') == 'No active guest performance.',
                   'Finished performance controls remained active', 15)
        for c in (h, g):
            wait_until(lambda: all((row := citizens(c).get(i)) and row['species'] == own for i in performers),
                       'Performance changed the founder species or lost its members', 45)
            if 'total faults: 0' not in command(c, 'exc'):
                raise RuntimeError('Native fault occurred')
        print('PASS guest keys drove native performance, relationship reward and both-peer reconciliation.',
              'relation', relation(h), 'inputs', sent, flush=True)
        return True

    def win_performance():
        # A real performance may lose with this small band. Retry ordinary
        # gameplay at most twice; missing input/session evidence still fails
        # immediately. Never change a score, relationship or reward to win.
        for attempt in range(3):
            if attempt:
                wait_until(lambda: u32(h, u32(h, leader + 0xB4C) + 0x1D8) != 0x546E8804,
                           'Audience remained in the lost performance', 20)
                command(g, 'coop.tribe select_village')
            if run_round():
                return
        raise RuntimeError('Three native performances failed to earn the required relationship')

    def segments(c):
        home_actor = next((r['ptr'] for r in citizens(c).values() if r['species'] == home_model), None)
        if home_actor is None:
            return None
        home = u32(c, home_actor + 0x1014)
        totem = u32(c, home + 0x19D0)
        if not totem:
            raise RuntimeError('Native campaign totem is missing')
        return (u32(c, totem + 0x120) - u32(c, totem + 0x11C)) // 4

    initial_segments = segments(h)
    def points(c):
        noun = eax(c,'Simulator::cGameNounManager::Get cdecl')
        player = u32(c,noun+0x74) if noun else 0
        if not player: return None
        raw = mem(c,player+0x10F0,8)
        if len(raw) != 8: return None
        score, maximum = struct.unpack('<2f',raw)
        return score if maximum == 5 else None
    if args.six_rival_trial:
        assert args.alliance and args.planet == 'Vantiegan.spo'
        initial_points = points(h)
        assert initial_points is not None and 3 <= initial_points < 5
        expected_points = initial_points + 2/3
        expected_segments = math.floor(expected_points + .001)
    else:
        expected_segments = initial_segments + 1
    assert relation(h)<7, 'Target is already allied'
    win_performance()
    if args.repeat or (args.alliance and relation(h)<7):
        wait_until(lambda: u32(h, u32(h, leader + 0xB4C) + 0x1D8) != 0x546E8804,
                   'Native audience did not leave the completed performance', 20)
        command(g, 'coop.tribe select_village')
        win_performance()
        print('Alliance native relation/totem:', relation(h), segments(h), segments(g), flush=True)
    if args.alliance:
        wait_until(lambda: segments(h) == expected_segments,
                   'Guest alliance did not award the shared native campaign totem', 20)
        # As in duo_tribe_raid, native victory/upgrade cinematics wait for the
        # host's Continue button. Production saves correctly wait until then.
        def acknowledge_result():
            if not re.search(r'^cinematic [0-9A-F]+ playing', command(h, 'coop.status'), re.M):
                return True
            captions = [h.call('coop.ui find ' + text) for text in
                        ('allied', 'ally', 'Your tribe grows in prestige')]
            if not any(caption['ok'] for caption in captions):
                return False
            buttons = command(h, 'coop.ui where 062EF353 0445C99D')
            button = re.search(r'^(?:062EF353|0445C99D) ([\d.]+) ([\d.]+) ([\d.]+) ([\d.]+) enabled=1',
                               buttons, re.M)
            if button:
                x1, y1, x2, y2 = map(float, button.groups())
                command(h, f'click {round((x1+x2)/2)} {round((y1+y2)/2)}')
                time.sleep(.5)
            return False
        def guest_current():
            acknowledge_result()
            if args.six_rival_trial:
                for peer in (h,g):
                    score = points(peer)
                    if score is None or abs(score-expected_points) > .001: return False
            return segments(g) == segments(h) == expected_segments
        wait_until(guest_current, 'Alliance totem did not reach the guest after native Continue', 75)
        if not args.six_rival_trial:
            print('PASS native alliance awards the shared campaign totem on both peers.', flush=True)
        if args.six_rival_trial:
            print('PASS shared six-rival points:', points(h), points(g), 'native milestones:', segments(h), segments(g), flush=True)



if __name__ == '__main__':
    main()
