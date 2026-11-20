"""Which DNA the session plays (WP-D of 0.7.16, BUG-1291): own DNA per player (the default) or the old shared pool
(the session setting all.shared_dna on). Tests that spend DNA check what their session plays; `--shared` on their
command line forces the shared pool (`coop.settings force`: a lane knob, the next test starts cold)."""
import sys

import settings_lib


def shared(con):
    """the game plays the shared pool (all.shared_dna on; a game without the setting: an older mod, shared)"""
    v = settings_lib.get(con, 'all.shared_dna')
    return v is None or v == 1


def setup(host, guest, ck):
    """--shared: the shared pool for this test; returns True when the session plays it"""
    if '--shared' in sys.argv:
        import twoinst
        twoinst.forget_settings()
        ck(settings_lib.force(host, [guest], 'all.shared_dna', 'on'), 'the guest plays with all.shared_dna on (one DNA pool)')
    s = shared(host)
    print(f'  DNA: {"one shared pool" if s else "own DNA per player"} (all.shared_dna)', flush=True)
    return s


def teardown(host, guest):
    if '--shared' in sys.argv:
        import twoinst
        settings_lib.reset([host, guest], ('all.shared_dna',))
        twoinst.forget_settings()


def party_gain(ck, shared_pool, read_guest, h0, h1, g0, what, secs=4.0, tol=0.6):
    """BUG-1340: a party gain the host's game paid (its DNA h0 -> h1) as the guest's game shows it (read_guest(): its DNA
    now, None when unknown; g0: before the gain). The shared pool: the guest has the host's total. Own DNA (the
    default): every gain counts for everybody, so the guest's own DNA grew by the same amount (it may differ from the
    host's: its own spends, or a wallet kept from an earlier game of the lane). Waits up to `secs` for the guest's
    total (sent per guest, a frame or more after the host's gain); True when it is right"""
    import time
    want = None if h0 is None or h1 is None else (h1 if shared_pool else (None if g0 is None else g0 + (h1 - h0)))
    g = read_guest()
    t0 = time.time()
    while want is not None and (g is None or abs(g - want) >= tol) and time.time() - t0 < secs:
        time.sleep(0.25)
        g = read_guest()
    ok = want is not None and g is not None and abs(g - want) < tol
    if shared_pool:
        ck(ok, f'{what}: the guest has the same total as the host (one pool: {g}, host {h1})')
    else:
        ck(ok, f'{what}: the guest\'s own DNA grew by the same {"?" if h0 is None or h1 is None else round(h1 - h0, 1)} '
               f'(own DNA: {g0} -> {g}, want {want})')
    return ok
