// Test switches of the netcode fixes (`coop.fix net.<name> on|off`): off brings back the code before
// the fix, so a test can show that it still catches the bug. All on in normal play. Every member is a
// portable fix switch (BUG-0214 R1): registered in mods/coop/fix_table.cpp, never with COOP_FIX here.
#pragma once

namespace net {

struct Fixes {
    bool big_reliable = true;  // BUG-0034: reliable messages above one packet are split, not stuck
    bool state_roster = true;  // BUG-0109: a guest ignores the state of a player not in its roster
    bool offset_window = true; // BUG-0133: the clock offset forgets an old minimum within seconds
    bool sender_time = true;   // BUG-0122: ship / vehicle velocity over the host's time, not arrival
    // wave 3
    bool first_ack = true;  // BUG-0030: packet seq starts at 1, the "ack 0" of a peer that got nothing acks nothing
    bool time_wrap = true;  // BUG-0053: snapshot order survives the sender's 32-bit millisecond wrap
    bool lan_prune = true;  // BUG-0039: the LAN game list forgets silent hosts and has a cap
    bool addr_check = true; // BUG-0138: host addresses trimmed, the port checked (1..65535)
    // wave 4
    bool async_resolve = true; // BUG-0204: a host name is looked up on a worker thread, not in the click
    // wave 5
    bool peer_caps = true;    // BUG-0219: split / join reliable messages only with a peer that announced it (HELLO caps)
    bool rejoin_token = true; // BUG-0235: a crash rejoin is the same game by its HELLO token, not by name and silence
    bool addr_label = true; // BUG-0236: a pasted "Friends connect to: a:p, ..." line joins its first host:port
    // wave 13
    bool lan_dedup = true; // BUG-0210: a host answering on two addresses is one LAN game (DiscoverReply id)
    // 0.7.5
    bool bulk_acks = true; // BUG-0405: an ack every 48 packets received; resend only on a later ack or a long silence
    // 0.7.13 (N1)
    bool name_clean = true;     // BUG-0774: names from the network lose control characters and are cut to kNameMax
    bool lan_only_disc = true;  // BUG-0776: LAN discovery is answered only from local networks, rate limited
    bool halfopen_cap = true;   // BUG-0778: a new peer only for a HELLO; a full table evicts the oldest half-open peer
    bool restart_gen = true;    // BUG-0861: host() / join() inside a delivery callback is not torn down by the old deferred leave
    // 0.7.14 (N2)
    bool join_seen = true;       // BUG-1001: a rejoin after the start by its token (or, without one, a name dropped recently
                                 // that nobody has now); every live entry counts against the player limit
    bool replace_checked = true; // BUG-1003: a joined guest's entry is replaced by a new connection only when checked
    bool welcome_split = true;   // BUG-1005: a joining guest that announced CAP_SPLIT joins the pieces of the host's WELCOME
    bool recv_skip_big = true;   // BUG-1007: a datagram too big for the read buffer is skipped, the reads of the frame go on
};
inline Fixes g_fix;

} // namespace net
