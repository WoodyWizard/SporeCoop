// Support diagnostics of a co-op session (docs/REPORTING.md#reporting-support): what a player's "Report a problem" zip
// needs to find the cause of a report.
//   diag_sync.cpp   the session clock ("[s 123.456]" in every log line, tools/merge_logs.py), bug
//                   marks (/bug <text> in the co-op chat: a MARK line in every player's log) and the
//                   sync self-check (host and guests compare the shared world every 30 s)
//   diag_stats.cpp  FPS and hitches, the game's address space, network quality per peer, session
//                   settings, the session summary (also at the top of the report text)
// Cheap and rate limited: about 25 KB per hour of log at the default detail (docs/REPORTING.md#reporting-support-log-size).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/session.h"
#include "sporekit.h"
#include "syncdbg.h"
#include "trace.h"

namespace support {

struct Probe {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> feed;     // a notification
    std::function<void(const std::string&)> chat_log; // a line of the co-op window's chat
    std::function<std::string()> my_name;
    // every synchronized object here: players (p<id>) and the stage's objects (coop.sync)
    std::function<std::vector<syncdbg::Entry>()> entries;
    // players out of the world as this game knows it (dead, respawning, no recent state, not in a
    // world): their creatures are not compared
    std::function<std::vector<uint8_t>()> players_out;
    // BUG-0378: the speed (m/s) of each player's ship / creature as this game knows it (its own, the
    // partners' last states): the sync-check's lag allowance
    std::function<std::vector<std::pair<uint8_t, float>>()> player_speeds;
    std::function<uint32_t()> mode;  // game mode id
    std::function<uint32_t()> place; // the place (world) id of the stage, 0 = none
    std::function<bool()> playing;   // the player controls something (an avatar, a tribe, ...)
    // BUG-0588: the host's game is in the end of its stage (Tribe: from the City Hall editor on its animals are at
    // (0 0 0), its village is taken down): its world is no longer the one a guest plays, the sync-check waits
    std::function<bool()> stage_end;
    std::function<bool(float&)> dna; // the DNA total when there is one
    std::function<bool(int&)> cell_food;
    std::function<bool(uint32_t&)> vote; // an evolution vote is on (its editor id)
    // what the mod is doing right now for a hitch line: "world share", "editor", "load", ...
    std::function<std::string()> activity;
    // runtime settings that differ from the defaults ("share_world=0 ..."), "" = all default
    std::function<std::string()> settings;
    // BUG-0381: one line about a synchronized object of this game by its key text ("n327", "t16"):
    // written under a MISMATCH so the log says what the object apart was doing ("" = not known)
    std::function<std::string(const std::string& key)> describe;
};

void init(const SkHost* host, SkMod* mod, const Probe& probe);
void tick(double now); // every frame (first thing of the frame)
void on_unload();

// MSG_DIAG from player `from` (host: a guest, guest: the host)
void on_message(uint8_t from, net::Reader& r);
// "/bug <text>" typed in the co-op chat: true = handled (not sent as chat)
bool chat_command(const std::string& text);
void mark(const std::string& text); // a bug mark in every player's log

// Report screenshots (SporeKit 0.3.6+, docs/REPORTING.md#reporting-shots): [report] screenshot in
// coop.ini (default 1) and a SporeKit that can
bool shot_setting();      // [report] screenshot=1
// a screenshot of this frame for the report archive: 1 requested, 0 not (busy, too soon, no
// SporeKit support), -1 switched off; its name ("shots/...jpg") into name
int report_shot(const char* tag, const std::string& caption, std::string* name = nullptr);
int report_shot_state();  // 1 being taken, 0 written, -1 failed (SkHost::report_shot_pending)
void report_shots_forget(); // switched off: the kept screenshots are removed

void on_session_start();                    // host() / join() succeeded
void on_connected();                        // guest: welcomed by the host
void on_player_join(const net::PlayerInfo& p); // with its versions (CAP_VERSION, trace.h)
void on_player_leave(uint8_t id, uint8_t reason);
void on_disconnected(const std::string& why); // guest: lost / rejected / host left
void on_session_end(const char* why);       // the session ended (any path)
void on_fault();                            // a fault in the mod's code was caught
void on_shutdown();                         // the game ends

// console: coop.diag [status | sync now|on|off|<s>|force <what> | clock | net | perf | summary]
std::string command(const std::vector<std::string>& args);

// ---- shared between diag_sync.cpp and diag_stats.cpp ----
using trace::g_detail;     // [coop] log_detail (trace.h): 0 = the old lines and marks only, 1 = default, 2 = verbose
extern const SkHost* g_H;
extern SkMod* g_M;
extern Probe g_P;
double session_time(bool* ok = nullptr); // the session clock now (local time when there is none)
bool in_session();
// statistics for the summary
struct Counters {
    int marks = 0, faults = 0, rejoins = 0, joins = 0;
    int sync_checks = 0, sync_ok = 0, sync_differs = 0, sync_mismatch = 0;
    double sync_ms_sum = 0, sync_ms_max = 0; int sync_ms_n = 0;         // host: build + compare per check
    double sync_guest_ms_sum = 0, sync_guest_ms_max = 0; int sync_guest_ms_n = 0; // guest: build per check
    uint64_t sync_bytes = 0;  // digests sent and received
    int max_rtt = 0;
    double max_loss = 0;
    int min_fps = -1;
    int min_free_mb = -1, min_block_mb = -1;
    std::vector<std::string> stages; // stages played in the session, in order
    std::vector<std::string> peers;  // "Bob (coop 0.7.4, ...)"
};
extern Counters g_c;
void sync_tick(double now);
void sync_on_session_end();
void stats_tick(double now);
void stats_session_start();
std::string stats_status();
std::string sync_status();
std::string summary_line(const char* why);
void write_summary(const char* why); // the summary line into the log, the report headline and a file
std::string sync_command(const std::vector<std::string>& args);

} // namespace support
