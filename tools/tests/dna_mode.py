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
