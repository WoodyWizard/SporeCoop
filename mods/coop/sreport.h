// One report for the session (0.7.12 F4, BUG-0767, docs/REPORTING.md#reporting-session): the Windows side of
// net/sreport.h. The host's "Report a problem" (with the host's own report) and "Ask the players for their reports"
// (without it) ask every guest; a guest's co-op window shows "The host asks for your report: Send / Don't send";
// a guest that sends makes the same zip its own "Report a problem" makes and sends it over the session; the host
// writes mods\logs\SporeCoop-session-report-<time>.zip with Host-<name>.zip, Guest-<name>.zip and SESSION.txt (who
// sent, who did not, who timed out, whose report was refused). Twin switch `coop.fix core.session_report off`: the
// host asks nobody (0.7.11: every player sent its own report).
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "net/session.h"
#include "sporekit.h"

namespace sreport {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<std::string(const char* reason)> make_report; // the game's own "Report a problem" zip ("" = cannot)
    std::function<int()> shot;        // a screenshot for it first: 1 requested, 0 not possible, -1 switched off
    std::function<int()> shot_state;  // 1 being taken, 0 written, -1 failed
    std::function<void(const std::string&)> feed;     // a notification panel and the feed
    std::function<void(const std::string&)> chat_log; // a line in the lobby's chat history
    std::function<void()> open_window;                // the co-op window on the lobby (a guest's question)
};

extern bool g_fix; // core.session_report

void init(const SkHost* host, SkMod* mod, Ctx ctx);
void tick(); // every frame
void on_message(uint8_t from, net::Reader& r);
void on_player_leave(uint8_t id);
void on_disconnect();

// host: ask every guest; host_zip = the host's own report ("" = without it). Returns the line the window shows.
std::string ask(const std::string& host_zip);
bool round_running();
// the host's lobby line about the round ("" = none) and the guest's question: 1 = asked now (line: the question),
// 0 = not (line: what happened to its report, "" = nothing)
std::string host_line();
int guest_prompt(std::string& line);
void answer(bool send); // the guest's click

std::string status(); // tests, coop.sreport
int command(void*, int argc, const char* const* argv, SkOut* out); // coop.sreport

} // namespace sreport
