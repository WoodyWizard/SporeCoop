// The registrations of the portable fix switches (BUG-0214 rule R1, fixes.h): bools defined in files
// that are also built without fixes.cpp (mods/coop/net/*, updater/*: net_tests, fuzz_decoders,
// win_tests, release_tsan, the setup) with FIX_SWITCH(var), and the members of net::Fixes.
// coop.dll only. Registration stores the bool's address, so the order of static initialisation
// does not matter (no switch is read in a static initialiser).
#include "fixes.h"
#include "net/dbpf.h"
#include "net/fixes.h"
#include "../../updater/pack.h"
#include "../../updater/winupdate.h"

namespace {

// net::Fixes (mods/coop/net/fixes.h)
COOP_FIX("net.big_reliable", "BUG-0034", net::g_fix.big_reliable, "net: reliable messages above one packet are split, not stuck");
COOP_FIX("net.state_roster", "BUG-0109", net::g_fix.state_roster, "net: a guest ignores the state of a player not in its roster");
COOP_FIX("net.offset_window", "BUG-0133", net::g_fix.offset_window, "net: the clock offset forgets an old minimum within seconds");
COOP_FIX("net.sender_time", "BUG-0122", net::g_fix.sender_time, "net: ship / vehicle velocity over the host's time, not arrival");
COOP_FIX("net.first_ack", "BUG-0030", net::g_fix.first_ack, "net: packet seq starts at 1, an ack 0 acks nothing");
COOP_FIX("net.time_wrap", "BUG-0053", net::g_fix.time_wrap, "net: snapshot order survives the sender's millisecond wrap");
COOP_FIX("net.lan_prune", "BUG-0039", net::g_fix.lan_prune, "net: the LAN game list forgets silent hosts and has a cap");
COOP_FIX("net.addr_check", "BUG-0138", net::g_fix.addr_check, "net: host addresses trimmed, the port checked");
COOP_FIX("net.peer_caps", "BUG-0219", net::g_fix.peer_caps, "net: reliable messages are split only for a peer that joins pieces (HELLO caps)");
COOP_FIX("net.rejoin_token", "BUG-0235", net::g_fix.rejoin_token, "net: a crash rejoin is recognised by its token, not by name and silence");
COOP_FIX("net.addr_label", "BUG-0236", net::g_fix.addr_label, "net: a pasted \"Friends connect to: ...\" line joins its first address");
COOP_FIX("net.async_resolve", "BUG-0204", net::g_fix.async_resolve, "net: a host name is looked up on a worker thread (coop.corefix async_resolve)");

// updater/ (FIX_SWITCH)
COOP_FIX("update.dup", "BUG-0089", upd::g_dup_fix, "update: a pack that lists a path twice is refused");
COOP_FIX_UNSAFE("update.old_suffix", "BUG-0114", upd::g_old_suffix_fix, "update: backups are *.sporecoop-old, the cleanup deletes only those (off: every *.old)");
COOP_FIX_UNSAFE("update.paths", "BUG-0242", upd::g_path_fix, "update: pack paths Windows treats as one file, device and working names are refused");
COOP_FIX_UNSAFE("update.lock", "BUG-0243", upd::g_lock_fix, "update: file swaps under a lock per game folder; cleanup and pending swaps wait for it");
COOP_FIX("update.keep_failed", "BUG-0150", upd::g_keep_failed_fix, "update: a deferred file whose swap failed stays on the pending list");

// R6: unit-test only, no runtime switch (the off side reads received data out of bounds). Listed
// here so tests/fixes_tests.cpp knows they are not forgotten; never registered.
[[maybe_unused]] const bool* const kUnitOnly[] = {
    &dbpf::g_bounds_fix,  // net.dbbf_bounds, BUG-0208: net_tests --old-dbbf
    &dbpf::g_refpack_fix, // net.refpack_bound, BUG-0047: net_tests --old-refpack
};

} // namespace
