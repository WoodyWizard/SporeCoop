// Test switches of the netcode fixes (`coop.netfix <name> 0|1`): 0 brings back the code before the
// fix, so a test can show that it still catches the bug. All on in normal play.
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
};
inline Fixes g_fix;

} // namespace net
