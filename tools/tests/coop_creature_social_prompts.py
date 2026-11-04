#!/usr/bin/env python3
"""A guest's social game has the NPC's prompts and counts only an answer to a prompt (BUG-0525).

The native session (docs/re/creature.md#creature-social-prompts) has the NPC perform the abilities of a prompt
list, one per round, and counts the player's answer to each prompt by its kind (+0x10) and points; a guest's game
with a mirror had none of it: the NPC showed nothing and any social ability, at any time, was a round.
Real engagement on the guest (the social stance, a click on a Kook); the answers are the guest's abilities sent as
its key / bar path sends them (`coop.social act`, `coop.social answer` for an ability of another kind). Checked:
1. an answer before the Kook's first prompt does not impress it (the host's Kook, the guest's mirror);
2. the guest's game shows the prompt: the Kook's mirror plays the prompt ability's animation;
3. an answer of another kind (a combat ability) does not impress it, the game fails (the host's count) and the
   mirror stays not impressed;
4. engaged again, the guest's social ability as the answer to the prompt impresses the Kook on both games.
SPORE_FIX_OFF=social.prompts must FAIL (checks 1-3)."""
import re
import time

from coop_creature_social import KOOK, num, social
from coop_creature_social_ally import engage, guest_list, host_list, pos
from twoinst import Checks, start


def abilities(con):
    """[(index, type, key)] of the own avatar"""
    return [(int(m.group(1)), int(m.group(2), 16), int(m.group(3)))
            for m in re.finditer(r'^ab (\d+) type=0x([0-9A-F]+) key=(\d+)', con.call('coop.social abil -1')['out'], re.M)]


def cur_anim(guest, nid):
    m = re.search(r'^cur %d anim=([0-9A-F]{8})' % nid, guest.call('coop.npc cur')['out'], re.M)
    return m.group(1) if m else None


def impressed_both(host, guest, nid):
    return host_list(host).get(nid, (0, 0, 0, False))[3], guest_list(guest).get(nid, (False, False))[1]


def wait_prompt(guest, nid, n0, secs=8, host=None):
    """the prompt for mirror #nid arrived (prompts_in > n0): (its animation, the mirror's animations seen since,
    the host NPC's animations seen since)"""
    seen, hseen = [], []
    t0 = time.time()
    anim = None
    while time.time() - t0 < secs:
        s = social(guest)
        if num(r'prompts_in=(\d+)', s, 0) > n0 and num(r'prompt_npc=(-?\d+)', s, -1) == nid:
            anim = re.search(r'prompt_anim=([0-9A-F]{8})', s).group(1)
        if anim:
            a = cur_anim(guest, nid)
            if a:
                seen.append(a)
            if host:
                h = cur_anim(host, nid)
                if h:
                    hseen.append(h)
            if time.time() - t0 > 4 or not host and anim in seen:
                break
        time.sleep(0.15)
    return anim, seen, hseen


def main():
    host, guest = start('creature')
    ck = Checks()
    for c in (host, guest):
        c.call('coop.team hp 1000')
    time.sleep(2)
    ab = abilities(guest)
    sing = [a for a in ab if a[2] == 1]
    other = [a for a in ab if a[2] == 0]
    print(f'  guest abilities: {ab}', flush=True)
    gl = guest_list(guest)
    kooks = [i for i, v in host_list(host).items() if v[0] == KOOK and not v[2] and v[1] > 0 and not v[3] and gl.get(i, (False,))[0]]
    ck(kooks and sing and other, f'the guest mirrors Kooks ({kooks}) and has a social and another ability')
    if not (kooks and sing and other):
        ck.faults(host=host, guest=guest)
        return ck.result()
    host.call(f'coop.npc gonpc {kooks[0]} 10')
    time.sleep(1)
    hp = pos(host)
    guest.call('coop.npc gopos %.2f %.2f %.2f' % (hp[0] + 2, hp[1], hp[2]))
    time.sleep(3)

    nid = None
    for k in kooks[:3]:
        if engage(guest, k, 40):
            nid = k
            break
    ck(nid is not None, f'the guest engaged a Kook (#{nid})')
    if nid is None:
        ck.faults(host=host, guest=guest)
        return ck.result()
    # 1. an answer before the first prompt
    n0 = num(r'prompts_in=(\d+)', social(guest), 0)
    guest.call(f'coop.social act {nid} 0')
    early_before_prompt = num(r'prompts_in=(\d+)', social(guest), 0) == n0
    time.sleep(1.0)
    imp = impressed_both(host, guest, nid)
    print(f'  #{nid} after the early answer (before any prompt: {early_before_prompt}): impressed host/guest {imp}; '
          f'host {social(host).strip()}', flush=True)
    if early_before_prompt:
        ck(not imp[0] and not imp[1], f'an answer before the Kook\'s prompt does not impress it (host, guest: {imp})')
    # 2. the prompt on the guest's screen
    anim, seen, hseen = wait_prompt(guest, nid, n0, host=host)
    # the game may play the ability's animation as a variant of its family (042A1CAF played as 042A1CB3, lab
    # 20261104-111837; the social family 0x42A1CA0 + n, docs/re/creature.md#creature-bt-anims)
    fam = [a for a in seen if anim and abs(int(a, 16) - int(anim, 16)) < 0x20]
    print(f'  #{nid} prompt {anim}, the mirror played {sorted(set(seen))}, the host\'s Kook {sorted(set(hseen))}', flush=True)
    ck(anim is not None and fam, f"the guest's game shows the Kook's prompt: its mirror performs it ({anim}: {sorted(set(fam))})")
    # 3. an answer of another kind
    if not any(impressed_both(host, guest, nid)):
        f0 = num(r'prompt_fails=(\d+)', social(host), 0)
        guest.call(f'coop.social answer {nid} {other[0][1]:X}')
        time.sleep(4)
        imp = impressed_both(host, guest, nid)
        hs = social(host)
        print(f'  #{nid} after an answer of another kind: impressed {imp}; host {hs.strip()}', flush=True)
        ck(not imp[0] and not imp[1], f'an answer of another kind does not impress the Kook (host, guest: {imp})')
        ck(num(r'prompt_fails=(\d+)', hs, 0) > f0, 'the host counts that social game as failed')
    # 4. engaged again (`coop.social act` engages it as the click does: the click's camera sweep is slow), the
    # right answer to the prompt
    done = any(impressed_both(host, guest, nid))
    for attempt in range(3):
        if done:
            break
        time.sleep(1)
        guest.call(f'coop.social goto {nid}')
        n1 = num(r'prompts_in=(\d+)', social(guest), 0)
        guest.call(f'coop.social act {nid} 0')  # engages it; an answer before the prompt (not counted)
        a2, _, _ = wait_prompt(guest, nid, n1)
        for _ in range(12):  # (the ability may be cooling down: the act is not sent then)
            if 'social action sent' in guest.call(f'coop.social act {nid} 0')['out']:
                time.sleep(2)
            else:
                time.sleep(1)
            if all(impressed_both(host, guest, nid)):
                done = True
                break
        print(f'  #{nid} attempt {attempt + 1}: prompt {a2}, impressed {impressed_both(host, guest, nid)}; host {social(host).strip()}', flush=True)
    imp = impressed_both(host, guest, nid)
    ck(imp[0] and imp[1], f'the answer to the prompt impresses the Kook on both games ({imp})')
    ck.faults(host=host, guest=guest)
    return ck.result()


if __name__ == '__main__':
    raise SystemExit(main())
