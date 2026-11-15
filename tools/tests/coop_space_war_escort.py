#!/usr/bin/env python3
"""Space, Rivals at War: the host's escort and the guest's ship damage each other (BUG-1032, owner decision 1).

In Rivals a guest at War with the host is an enemy of the host's empire: its puppet in the host's game has no empire
(space_rivalry.cpp puppets_tick), so the host's escorts and Defenders attack it. The BUG-0420 filter (written for
Allies) dropped those hits as friendly, and dropped the guest's shots at the escort's mirror; the hit was also applied
with the host's own empire id as the attacker's, which the game ignores within one empire (BUG-0705).

Same star system (fixture space). The host switches to Rivals, the guest declares war on the host, the host spawns an
escort of its own empire (coop.fleet spawn 2 own) and a ship without an empire (control):
  1. the escort hits the guest's puppet (coop.fleet hurt, the game's TakeDamage through the hook with the escort as
     the attacker, as its projectile does): the guest's ship loses hp in the guest's game;
  2. the guest shoots the escort's mirror (coop.fleet shoot, the hook path of a real hit): the escort loses hp in
     the host's game;
  3. back to peace (the guest proposes, the host accepts) and Allies: the escort's hit is friendly again (BUG-0420
     stays: no damage, nothing relayed).
SPORE_FIX_OFF=space.war_npc_hits: 1 and 2 do nothing (FAIL).

  SPORE_LANE=2 python3 tools/tests/coop_space_war_escort.py
"""
import re
import time

from twoinst import Checks, keyframe, start


def sp(con, *a):
    return con.call('coop.space ' + ' '.join(str(x) for x in a))['out'].strip()


def ships(con):
    out = con.call('coop.fleet list')['out']
    d = {}
    for m in re.finditer(r'ship #(-?\d+) \S+ type=(\d+) pol=(\w+) hp=([\d.]+)/([\d.]+)( \(me\))?', out):
        d['me' if m.group(6) else int(m.group(1))] = (int(m.group(2)), m.group(3), float(m.group(4)))
    return d


def hp(con):
    m = re.search(r'hp ([\d.]+) max ([\d.]+)', sp(con, 'hp'))
    return float(m.group(1)) if m else -1.0


def riv(con):
    out = sp(con, 'rivalry')
    d = {k: v for k, v in re.findall(r'(\w+)=(-?[\d.]+)\b', out)}
    d['rel'] = {int(i): r for i, r in re.findall(r' (\d+)=(ally|neutral|war)\b', out)}
    return d


def num(con, key):
    m = re.search(r'\b' + key + r'=(\d+)', con.call('coop.status')['out'])
    return int(m.group(1)) if m else -1


def wait(pred, t=20, step=0.5):
    t0 = time.time()
    while time.time() - t0 < t:
        v = pred()
        if v:
            return v
        time.sleep(step)
    return pred()


def spawned_id(host, before):
    for _ in range(10):
        now = ships(host)
        new = [k for k in now if k != 'me' and k >= 0 and k not in before]
        if new:
            return new[0], now
        time.sleep(0.5)
    return None, ships(host)


def main():
    host, guest = start('space')
    ck = Checks()
    time.sleep(3)
    print('  host:', sp(host, 'rivalry mode rivals'), flush=True)
    ck(wait(lambda: riv(host).get('rivals') == '1' and riv(guest).get('rivals') == '1', 15), 'rivals mode on both games')
    print('  guest:', sp(guest, 'rivalry war 0'), flush=True)
    ck(wait(lambda: riv(host)['rel'].get(1) == 'war' and riv(guest)['rel'].get(0) == 'war', 15),
       f"war on both games ({riv(host)['rel']} / {riv(guest)['rel']})")
    print('  host pol:', sp(host, 'rivalry pol'), '| guest pol:', sp(guest, 'rivalry pol'), flush=True)
    b0 = ships(host)
    print(' ', host.call('coop.fleet spawn 2 own')['out'].strip(), flush=True)
    esc, b1 = spawned_id(host, b0)
    print(' ', host.call('coop.fleet spawn 2')['out'].strip(), flush=True)
    pir, b2 = spawned_id(host, b1)
    time.sleep(3)
    gs = ships(guest)
    print(f'  escort #{esc} {b2.get(esc)}, other #{pir} {b2.get(pir)}; host ship {b2.get("me")}; guest sees '
          f'{gs.get(esc)} / {gs.get(pir)}', flush=True)
    ck(esc is not None and pir is not None, 'the host has its escort and a ship without an empire')
    ck(esc in gs and pir in gs, f'the guest mirrors both ({esc in gs}, {pir in gs})')
    if esc is None or pir is None or esc not in gs:
        ck.faults(host=host, guest=guest)
        return ck.result()
    ck(wait(lambda: 'puppet 1 FFFFFFFF' in sp(host, 'rivalry pol'), 15),
       f"the guest's puppet on the host belongs to no empire ({sp(host, 'rivalry pol')})")

    # 1. the escort's hits on the guest's puppet: the guest's ship in the guest's game
    hp0 = hp(guest)
    for _ in range(3):
        print(' ', host.call(f'coop.fleet hurt {esc} 1 20')['out'].strip(), flush=True)
        time.sleep(0.7)
    time.sleep(2)
    hp1 = hp(guest)
    print(f"  escort hit the guest x3: guest ship hp {hp0} -> {hp1}; host war_hits_out={num(host, 'war_hits_out')} "
          f"friendly_dropped={num(host, 'friendly_dropped')}", flush=True)
    ck(hp1 < hp0 - 1, f"the host's escort damages the guest's ship in the guest's game (hp {hp0} -> {hp1})")
    keyframe('war_escort', "both games in the host's star system: the host's escort next to the ships, the guest's "
             'ship damaged (its health bar lower), no black screen')

    # 2. the guest's shots at the escort's mirror: the escort in the host's game
    e0 = ships(host)[esc][2]
    for _ in range(2):
        print(' ', guest.call(f'coop.fleet shoot {esc} 10')['out'].strip(), flush=True)
        time.sleep(0.7)
    time.sleep(2)
    e1 = ships(host).get(esc, (0, '', -1))[2]
    print(f"  guest shot the escort x2: escort hp {e0} -> {e1}; host war_hits_in={num(host, 'war_hits_in')}", flush=True)
    ck(e1 < e0, f"the guest's shots damage the host's escort in the host's game (hp {e0} -> {e1})")
    p0 = ships(host)[pir][2]
    guest.call(f'coop.fleet shoot {pir} 5')
    time.sleep(2)
    p1 = ships(host).get(pir, (0, '', -1))[2]
    ck(p1 < p0, f"control: the guest's shot on the ship without an empire is applied (hp {p0} -> {p1})")

    # 3. peace and Allies: the escort is friendly again (BUG-0420 still holds)
    print('  guest:', sp(guest, 'rivalry peace 0'), '| host:', sp(host, 'rivalry accept 1'), flush=True)
    time.sleep(2)
    print('  host:', sp(host, 'rivalry mode allies'), flush=True)
    ck(wait(lambda: riv(host).get('rivals') == '0' and riv(guest).get('rivals') == '0', 15), 'allies mode again')
    time.sleep(2)
    hp2 = hp(guest)
    print(' ', host.call(f'coop.fleet hurt {esc} 1 20')['out'].strip(), flush=True)
    time.sleep(2)
    hp3 = hp(guest)
    ck(hp3 >= hp2, f"allies: the escort's hit does not hurt the guest (hp {hp2} -> {hp3})")
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
