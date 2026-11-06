"""Complete five native objectives from an automatically founded lab campaign.

Food, travel and nonlethal hut damage shorten this disposable fixture. Native
ordered villagers must land every final blow. Never inject victory segments,
unlocks, NPC villages, or advancement. Run after the initial outfitter objective.
"""
import argparse
import re
import struct
import time
from coop_ability_bar import eax, mem, u32
from duo_villages import Console, command
from duo_orders import wait_until
from duo_tribe_rescue import citizens, move
from duo_tribe_rebuild_checkpoint import objects
from duo_native_ui import view_tribal_point, dismiss_registration, dismiss_service_error
from duo_tribe_graduation_editor import button


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--host', type=int, required=True)
    p.add_argument('--guest', type=int, required=True)
    p.add_argument('--planet', default='Vantiegan.spo')
    p.add_argument('--resume', action='store_true')
    p.add_argument('--founders', nargs=2, metavar=('HOST', 'GUEST'), help='Expected immutable species keys, in hexadecimal.')
    earners = p.add_mutually_exclusive_group()
    earners.add_argument('--guest-only', action='store_true', help='Isolate guest-earned NPC objective budget from host input coverage.')
    earners.add_argument('--host-only', action='store_true', help='Qualify native host-earned shared campaign credit.')
    a = p.parse_args()
    h, g = Console(a.host), Console(a.guest)
    original = command(h, 'coop.founders')
    if a.founders:
        expected = tuple(int(key, 16) for key in a.founders)
        actual = re.search(r'host ([0-9A-F]+), guest ([0-9A-F]+)', original)
        assert actual and tuple(int(key, 16) for key in actual.groups()) == expected, 'Wrong founding roster in fixture'
    for c in (h, g):
        assert f"world '{a.planet}'" in command(c, 'coop.avatar')
        assert command(c, 'coop.founders') == original
    command(h, 'coop.tribe village_orders on')
    # Resolve factions from native ownership; outfit changes must not break
    # this test by changing a founder's current model key.
    owners = {}
    outfit = re.search(r'readable=1 home=([0-9A-F]+) guest=([0-9A-F]+)', command(h, 'coop.tribe outfits'))
    assert outfit, 'Saved founder appearances unavailable'
    founder_models = outfit.groups()
    for row in command(h, 'coop.tribe villages').splitlines():
        m = re.match(r'village ([0-9A-F]+) species=([0-9A-F]+).*native_player=(\d)', row)
        if m and m[2] in founder_models: owners[int(m[3])] = (int(m[1], 16), m[2])
    assert len(owners) == 2, 'Both native founders must be present'
    home, host_model = owners[1]
    guest, guest_model = owners[0]
    founders = (home, guest)

    def tribes(c):
        return {u32(c, t+0x30): t for t, _ in objects(c, 0x18C6D19)}

    def health(c, hut):
        return struct.unpack('<f', mem(c, hut+0x158, 4))[0]

    def segments(c):
        t = tribes(c).get(home)
        if not t: return None
        totem = u32(c, t+0x19D0)
        return (u32(c, totem+0x120)-u32(c, totem+0x11C))//4 if totem else 0

    def unlocks(c):
        game = eax(c, 'SporeCoop::TribalGame_Get cdecl')
        return tuple(k for k in range(1, 10) if eax(c,
            f'SporeCoop::TribalGame_IsToolUnlocked thiscall {game:#x} {k}') & 255)

    def dialogs(c):
        dismiss_registration(c)
        dismiss_service_error(c)
        button(c, '0445C99D')
        button(c, '062EF353')

    def check_pair(expected):
        native_history = []
        for c in (h, g):
            assert segments(c)==expected
            assert command(c,'coop.founders')==original
            ts = tribes(c)
            for faction in founders:
                village = ts[faction]
                hut = u32(c,village+0x368)
                assert health(c,hut)>0 and u32(c,hut+0x230)==village, 'Founder hut was captured during the fixture'
                assert u32(c,village+0x344)>u32(c,village+0x340), 'Founder population was lost during the fixture'
            totem = u32(c,ts[home]+0x19D0)
            parts = []
            for offset,width in ((0x11C,4),(0x130,12)):
                begin,end = u32(c,totem+offset),u32(c,totem+offset+4)
                assert end-begin==expected*width
                parts.append(mem(c,begin,end-begin).hex())
            native_history.append(parts)
            assert 'total faults: 0' in command(c,'exc')
        assert native_history[0]==native_history[1], 'Native totem types/colors differ'
        assert unlocks(h)==unlocks(g)

    initial = segments(h)
    assert initial == 0 or a.resume, 'Fresh qualification must start at zero victories'
    print('Initial native victories:', initial, 'founders:', founders, flush=True)
    completed = set()
    for round_no in range(initial, 5):
        def target():
            for c in (h, g): dialogs(c)
            if any('busy: -' not in command(c, 'coop.status') for c in (h, g)): return None
            for faction, t in tribes(h).items():
                if faction in founders: continue
                hut = u32(h, t+0x368)
                relation = float(command(h, f'coop.tribe relation {faction:X} {home:X}').split('=')[-1])
                if hut and health(h, hut)>0 and relation<7: return faction
            return None
        faction = wait_until(target, 'Native campaign did not supply its next NPC objective', 90)
        assert faction not in completed
        print(f'Preparing objective {round_no+1}: native target {faction:X}', flush=True)
        # Alternate which founder earns credit. Native orders are still issued
        # through the owning peer; no host-side direct victory operation.
        actor_client = h if a.host_only else g if a.guest_only or round_no % 2 == 0 else h
        actor_faction, model = (guest, guest_model) if actor_client is g else (home, host_model)
        for f, t in tribes(h).items():
            if f in founders: command(h, f'call SporeCoop::cTribe_AddFood thiscall {t:#x} 300')
        village = tribes(h)[faction]
        hut = u32(h, village+0x368)
        # Newly mirrored NPCs have local native IDs until the next checkpoint;
        # the replicated world position is the mouse target on either peer.
        point = dict(objects(h, 0x1E4DAAE))[hut]
        if actor_client is h:
            command(h, 'wheel 588 332 -4')
            time.sleep(1)
        x, y = view_tribal_point(actor_client, point)
        village = tribes(h)[faction]
        hut = u32(h, village+0x368)
        own = [r for r in citizens(h).values() if r['species']==model and not r['dead'] and r['hp']>0 and not r['timer']]
        assert own, 'Founder has no surviving raiders'
        for row in own: move(h, row['ptr'], village+0x124)
        attacker = own[0]['ptr']
        hp = health(h, hut)
        command(h, f'call SporeCoop::cTribeHut_TakeDamage thiscall {hut+0x120:#x} f:{hp-1} {actor_faction:#x} 0 {attacker+0xC4:#x} {attacker+0x5A8:#x}')
        assert health(h, hut)>0, 'Fixture must not award a victory'
        print('Prepared hut HP:', health(h,hut), 'native actor count:', len(own), flush=True)
        if actor_client is g:
            assert 'selected:' in command(g, 'coop.tribe select_village')
            assert 'Left-click' in command(g, 'coop.tribe raid')
            command(g, f'click {x} {y}')
        else:
            # The host uses Spore's native selection and right-click attack.
            # The guest-only command UI is deliberately not available here.
            # Object SetSelected alone does not initialize the native input
            # strategy's selected set. Use a real drag, as a player would.
            own_rows = {i:r for i,r in citizens(h).items() if r['species']==model and not r['dead'] and r['hp']>0 and not r['timer']}
            time.sleep(.5)
            points = [(own_rows[int(m[1])]['hp'], *map(int,m.groups()[1:])) for m in re.finditer(
                r'^member (\d+) 0 (\d+) (\d+)', command(h,'coop.targets objs'), re.M)
                if int(m[1]) in own_rows and 35<int(m[2])<1140 and 105<int(m[3])<535]
            assert points, 'Host raiders are not visible for native selection'
            _, px, py = max(points)
            command(h, f'drag {px-28} {py-42} {px+28} {py+28}')
            command(h, 'wait.frames 40 10000')
            selected = [s for s in command(h,'coop.tribe').splitlines() if 'owner=0 ' in s and 'sel=1 ' in s]
            print('Native selected host actors:', selected, flush=True)
            assert selected, 'Native drag did not select a living host raider'
            # Native picking uses the model, whereas the projection is its
            # ground origin. Retry higher on the hut as the existing UI driver
            # does, letting native hover update before each right-click.
            for dy in (0, -10, -20, -35):
                command(h, f'move {x} {y+dy}')
                time.sleep(.4)
                command(h, f'click {x} {y+dy} right')
                time.sleep(2)
                if health(h, hut)<=0: break
                order = re.search(r'tribe last local order:.*', command(h,'coop.tribe'))
                print('Host native order:', order[0] if order else 'none', 'hut HP:', health(h,hut), flush=True)
                if order and f'action=15 ' in order[0] and f'obj={hut:08X}' in order[0]: break
        wait_until(lambda: health(h, hut)<=0, 'Native ordered raid failed', 45)
        for row in citizens(h).values():
            if row['species']!=model or row['dead']: continue
            actor = row['ptr']; owner = u32(h, actor+0x1014)
            rec = eax(h, f'Simulator::cCreatureCitizen::DoAction thiscall {actor:#x} 4 0 0')
            if rec: command(h, f'call App::Property::SetValueVector3 thiscall {rec+0x39:#x} {owner+0x124:#x}')
            move(h, actor, owner+0x124)
        def credited():
            for c in (h, g):
                dialogs(c)
                if 'Tribe' not in command(c, 'game.mode') or 'busy: -' not in command(c, 'coop.status'): return False
                if segments(c)!=round_no+1: return False
            return unlocks(h)==unlocks(g)
        wait_until(credited, 'Shared native victory/unlocks did not converge', 100)
        completed.add(faction)
        check_pair(round_no+1)
        print(f'PASS native victory {round_no+1}/5 by faction {actor_faction:X}, target {faction:X}; shared unlocks {unlocks(h)}', flush=True)
    time.sleep(5)
    check_pair(5)
    print('PASS five native objectives, both founders surviving, shared progression.', flush=True)


if __name__ == '__main__': main()
