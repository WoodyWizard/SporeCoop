"""Helpers for the tests of the session settings (F3 of 0.7.12, docs/07-modes.md#modes-session-settings): read a
setting on a game, set one on the host in play (`coop.settings force`: a session setting is refused once the host
plays its world) and wait until the guest plays with it, put every setting back."""
import re
import time


def get(con, key):
    m = re.search(re.escape(key) + r'=(-?\d+)', con.call(f'coop.settings get {key}')['out'])
    return int(m.group(1)) if m else None


def force(host, guests, key, value, secs=10):
    """the host sets key=value (on, off, a number, a name); True when every guest plays with it within secs"""
    out = host.call(f'coop.settings force {key} {value}')['out'].strip()
    print(f'  host: {out}', flush=True)
    want = get(host, key)
    t0 = time.time()
    while time.time() - t0 < secs:
        if all(get(g, key) == want for g in guests):
            return True
        time.sleep(0.5)
    print(f'  {key}: a guest did not get {value}: ' + ', '.join(str(get(g, key)) for g in guests), flush=True)
    return False


def reset(cons, keys):
    """every key back to its default on every game (a test that failed half way leaves no setting changed)"""
    for con in cons:
        for k in keys:
            try:
                con.call(f'coop.settings force {k} default')
            except OSError:
                pass


def wait(cond, secs, step=0.5):
    t0 = time.time()
    while time.time() - t0 < secs:
        r = cond()
        if r:
            return r
        time.sleep(step)
    return None
