// Shared diplomacy (Space 0.7 feature 8, docs/09-coop-design.md; game knowledge: docs/re/space.md 12).
//
// The players act as one people toward the NPC empires: every relationship event between the own
// empire and an NPC empire (a gift, a mission, a trade, an attack, a compliment...) and every change
// of the war / alliance state is replayed on the partners through the same game functions, and the
// host's whole relation table is sent from time to time (and when a guest asks) so that missed
// messages, rejoins and the game's own drift converge on the host's state.
//
// Where the state lives (both builds): cRelationshipManager (Get(), space mode) +0x24 is a
// map<pair<pol, pol>, cRelationshipData>; the pair is normalized so the player is second. Data:
// +0x00 cached value, +0x04 flags (bit 0 war, bit 1 alliance), +0x08 map<eventId, float> (the
// relation is the sum of the events, clamped by CalculateRelationship). The core of every event is
// the ApplyEvent hook (0xD05DD0); DeclareWar / DeclareAlliance / DeclarePeace / BreakAlliance set the
// flags and the cEmpire enemy/ally vectors.
//
// MSG_SPACE kinds 30..39:
//   30 EVENT  u32 npc pol, u8 player is the first pol, u32 event id, f32 scale
//   31 STATE  u32 npc pol, u8 what (1 war, 2 alliance, 3 peace, 4 alliance broken), u8 player first
//   32 SNAP   host -> all: u8 last chunk, u16 n, n x {u32 npc pol, u32 flags, u8 k, k x {u32 id, f32 v}}
//   33 ASK    guest -> host: send me the table
// In rivals mode (spaceext::rivals()) diplomacy is per player: nothing is sent or applied.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>
#include <vector>

#include "space_ext.h"
#include "sporekit_game.hpp"
#include "fixes.h"

using skg::cdecl_call;
using skg::thiscall;

namespace diplo {

static const SkHost* H;
static SkMod* M;

enum Kind : uint8_t { K_EVENT = 30, K_STATE = 31, K_SNAP = 32, K_ASK = 33, K_CONTACT = 34 };
// Rec::flags besides war / alliance: the two levels of a first contact (cPlayer flags, see contact_flags)
constexpr uint32_t kMet = 0x10;       // cPlayer 0x20: met (cEmpire_MakeContact, arriving at its star)
constexpr uint32_t kContacted = 0x20; // cPlayer 0x02: contacted (cEmpire_SetContactedByPlayer, the first-contact talk)
enum What : uint8_t { W_WAR = 1, W_ALLY = 2, W_PEACE = 3, W_BREAK = 4 };

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

static uintptr_t a_RelGet, a_Apply, a_ApplyEv, a_War, a_Ally, a_Peace, a_Break, a_Reset, a_Calc, a_EvIndex,
    a_StarMgr, a_GetEmpire, a_StarRec, a_Contact, a_Spoken, a_NounMgr, a_PlayerEmpire, a_PlayerPol;

using ApplyEv_t = float(SK_THISCALL*)(void*, void*, uint32_t, uint32_t, uint32_t, float);
using State_t = void(SK_THISCALL*)(void*, void*, void*);
static ApplyEv_t o_ApplyEv;
static State_t o_War, o_Ally, o_Peace, o_Break;
using Contact_t = void(SK_THISCALL*)(void*, int);
static Contact_t o_Contact;
using Spoken_t = void(SK_THISCALL*)(void*);
static Spoken_t o_Spoken;

static int g_depth;     // inside a hooked relationship call (its inner events are part of it)
static bool g_applying; // replaying a partner's change: nothing is sent

struct Out {
    uint8_t kind;
    uint32_t npc;
    uint8_t first;
    uint32_t ev; // event id, or What for K_STATE
    float scale;
    std::string name; // the empire's name in the sender's copy (names and species are chosen per copy)
};
static std::vector<Out> g_out;            // collected by the hooks, sent by the tick
static std::vector<std::pair<uint8_t, Out>> g_in; // (origin, message) to replay outside the frame
static bool g_in_task;
static std::unordered_map<uint32_t, double> g_recent; // npc pol -> time of the last own change
static std::map<std::pair<uint32_t, uint32_t>, double> g_told; // (npc, event) -> last notice
static double g_last_snap;
// wall clock (the feature tick does not run while the game is paused, e.g. with the co-op window open)
static double now_s() { return H->time_seconds(); }
static bool g_was_space, g_ask_pending;
static uint32_t g_sent_contact, g_got_contact;
static uint32_t g_sent_ev, g_sent_state, g_got_ev, g_got_state, g_snaps_sent, g_snaps_got, g_fixed, g_unknown,
    g_skipped_rivals;
// BUG-0544: a partner's changes that arrive while this game is out of Space (an editor, an adventure
// entered from Space: the same Space world lives on) were dropped. On a guest the next table brings
// them back; on the host they were lost for good, and the host's next table took them away from the
// partner who made them too (a gift paid, its relation gone). Now they wait (in the Space world they
// came in) and are replayed once the game is back in Space. Off: dropped (before).
static bool g_keep_fix = true;
COOP_FIX("diplomacy.keep_away", "BUG-0544", g_keep_fix, "space: a partner's diplomacy changes made while I am out of Space are kept for me");
static uint32_t g_in_epoch;              // spaceext::world_epoch() the messages of g_in came in
static uint32_t g_kept_away, g_dropped_away;
constexpr size_t kKeepMax = 512;
static std::string g_last_notice;
static std::vector<std::string> g_log; // last messages (coop.space diplomacy log)

static void log_line(const std::string& s)
{
    g_log.push_back(s);
    if (g_log.size() > 40)
        g_log.erase(g_log.begin());
    H->log(M, SK_INFO, "diplomacy: %s", s.c_str());
}

// ---------------- game state ----------------
static bool is_empire(uint32_t pol) { return (pol & 0x80000000u) && pol != 0xFFFFFFFFu; }
// BUG-0071: the allies mirror empire (a partner's colonies) is local to each copy: its id means
// another empire, or a real NPC, in the partner's galaxy. Its record and events were shared by pol
// and the partner was allied with / contacted an NPC it never met. Now it is neither sent nor
// applied onto. `coop.space diplomacy mirrorfix 0` restores that (tests).
static bool g_mirror_fix = true;
COOP_FIX("diplomacy.mirror", "BUG-0071", g_mirror_fix, "space: the allies mirror empire's record is neither sent nor applied");
static uint32_t g_mirror_skipped;
static bool local_mirror(uint32_t pol)
{
    if (!g_mirror_fix || !spaceext::allies_mirror_pol(pol))
        return false;
    g_mirror_skipped++;
    return true;
}
static uint32_t my_pol() { return a_PlayerPol ? cdecl_call<uint32_t>(a_PlayerPol) : 0xFFFFFFFFu; }
static void* my_empire() { return a_PlayerEmpire ? cdecl_call<void*>(a_PlayerEmpire) : nullptr; }
static void* rel_mgr() { return a_RelGet ? cdecl_call<void*>(a_RelGet) : nullptr; }
static bool active() { return spaceext::in_space() && rel_mgr() && is_empire(my_pol()); }

static void* empire_of(uint32_t pol)
{
    void* sm = a_StarMgr ? cdecl_call<void*>(a_StarMgr) : nullptr;
    return sm && a_GetEmpire ? thiscall<void*>(a_GetEmpire, sm, pol) : nullptr;
}
static uint32_t pol_of(void* empire) { return empire ? F<uint32_t>(empire, 0x84) : 0xFFFFFFFFu; }

static std::string empire_name(void* e)
{
    if (!e)
        return "?";
    const uint16_t* b = F<const uint16_t*>(e, 0x3C);
    const uint16_t* en = F<const uint16_t*>(e, 0x40);
    std::string s;
    if (!b || en < b || en - b > 200 || !skg::readable(b, (size_t)(en - b) * 2 + 2))
        return "?";
    for (const uint16_t* p = b; p < en; p++) { // UTF-16 (BMP) -> UTF-8
        uint16_t c = *p;
        if (c < 0x80)
            s += (char)c;
        else if (c < 0x800) {
            s += (char)(0xC0 | (c >> 6));
            s += (char)(0x80 | (c & 0x3F));
        } else {
            s += (char)(0xE0 | (c >> 12));
            s += (char)(0x80 | ((c >> 6) & 0x3F));
            s += (char)(0x80 | (c & 0x3F));
        }
    }
    return s.empty() ? "?" : s;
}
static std::string name_of_pol(uint32_t pol) { return empire_name(empire_of(pol)); }
// the name for a notice about a partner's action: ours, and theirs when their copy calls it otherwise
static std::string shown_name(uint32_t pol, const std::string& who, const std::string& theirs)
{
    std::string mine = name_of_pol(pol);
    if (theirs.empty() || theirs == "?" || theirs == "unknown" || theirs == mine)
        return mine;
    if (mine == "?" || mine == "unknown")
        return theirs;
    return mine + " (" + who + ": " + theirs + ")";
}

// in-order walk of an EASTL map: root at map+0xC, node: right +0, left +4, value +0x10
template <typename Fn>
static void walk(void* map, Fn fn)
{
    if (!map || !skg::readable(map, 0x18))
        return;
    void* root = F<void*>(map, 0xC);
    std::vector<void*> st;
    void* n = root;
    int guard = 0;
    while ((n || !st.empty()) && guard++ < 20000) {
        while (n) {
            if (!skg::readable(n, 0x40) || st.size() > 64)
                return;
            st.push_back(n);
            n = F<void*>(n, 4);
        }
        n = st.back();
        st.pop_back();
        fn(n);
        n = F<void*>(n, 0);
    }
}

// the record of (npc, player): relationship map node + 0x18, or null (no contact yet)
static void* record(uint32_t npc, uint32_t me)
{
    void* mgr = rel_mgr();
    if (!mgr)
        return nullptr;
    void* found = nullptr;
    walk((uint8_t*)mgr + 0x24, [&](void* node) {
        uint32_t a = F<uint32_t>(node, 0x10), b = F<uint32_t>(node, 0x14);
        if ((a == npc && b == me) || (a == me && b == npc))
            found = (uint8_t*)node + 0x18;
    });
    return found;
}

// cPlayer (cGameNounManager +0x74) +0x11D8..+0x11DC: sorted vector of {u32 pol, u32 flags} per empire
// (docs/re/space.md 9.6, 10.1): 0x20 = met (IsContacted 0xC7AA10, set by cEmpire_MakeContact 0xC32430),
// 0x02 = contacted (PlayerContactedEmpire 0xC7A890, set with 0x04 by cEmpire_SetContactedByPlayer 0xC30A70)
static std::map<uint32_t, uint32_t> contact_flags()
{
    std::map<uint32_t, uint32_t> v;
    void* nm = a_NounMgr ? cdecl_call<void*>(a_NounMgr) : nullptr;
    void* pl = nm ? F<void*>(nm, 0x74) : nullptr;
    if (!pl || !skg::readable((uint8_t*)pl + 0x11D8, 8))
        return v;
    uint32_t* b = F<uint32_t*>(pl, 0x11D8);
    uint32_t* e = F<uint32_t*>(pl, 0x11DC);
    if (!b || e < b || e - b > 2 * 20000 || !skg::readable(b, (size_t)(e - b) * 4))
        return v;
    for (uint32_t* q = b; q + 1 < e; q += 2) {
        uint32_t f = (q[1] & 0x20 ? kMet : 0) | (q[1] & 0x02 ? kContacted : 0);
        if (f)
            v[q[0]] = f;
    }
    return v;
}
static uint32_t contact_of(uint32_t pol)
{
    auto v = contact_flags();
    auto it = v.find(pol);
    return it == v.end() ? 0 : it->second;
}
static bool contacted(uint32_t pol) { return contact_of(pol) & kMet; }

struct Rec {
    uint32_t npc;
    uint32_t flags; // bit 0 war, bit 1 alliance, kMet, kContacted
    std::vector<std::pair<uint32_t, float>> ev; // sorted by id
};

static std::vector<Rec> my_table()
{
    std::vector<Rec> v;
    void* mgr = rel_mgr();
    uint32_t me = my_pol();
    if (!mgr || !is_empire(me))
        return v;
    auto met = contact_flags();
    walk((uint8_t*)mgr + 0x24, [&](void* node) {
        uint32_t a = F<uint32_t>(node, 0x10), b = F<uint32_t>(node, 0x14);
        uint32_t npc = a == me ? b : b == me ? a : 0;
        if (!npc || !is_empire(npc) || npc == me || local_mirror(npc))
            return;
        auto mf = met.find(npc);
        Rec r{npc, (F<uint32_t>(node, 0x1C) & 3) | (mf == met.end() ? 0 : mf->second), {}};
        walk((uint8_t*)node + 0x20, [&](void* en) { r.ev.push_back({F<uint32_t>(en, 0x10), F<float>(en, 0x14)}); });
        v.push_back(std::move(r));
    });
    return v;
}

static float relation(uint32_t npc)
{
    void* mgr = rel_mgr();
    return mgr && a_Calc ? thiscall<float>(a_Calc, mgr, npc, my_pol(), 0) : 0.f;
}

// ---------------- what to share ----------------
// events the game computes on every copy by itself (archetype and personality of the empire, its
// own AI moods, embassies): replaying them would count them twice
static bool own_event(uint32_t ev)
{
    switch (ev) {
    case 0x05B6FCC9: case 0x05B6FCD4:                   // personality nice / mean
    case 0x05FF85B2: case 0x05FF85B3: case 0x05FF85B4: // archetype nice / mean / mods
    case 0x0601DF2A:                                    // super power
    case 0x068B2938:                                    // allied with the Grox
    case 0x0580E23B:                                    // embassy bonus
    case 0x0577909A: case 0x0577909B:                   // the empire AI is nice / nasty
    case 0x05B942D0:                                    // avoided contact (a timer of the game)
    case 0x0526E5F4:                                    // new empire and CTMG owner
        return true;
    }
    return false;
}

static const char* event_text(uint32_t ev, bool ru)
{
    switch (ev) {
    case 0x0526E51F: case 0x0526E4F2: return ru ? "сделал(а) подарок" : "gave a gift to";
    case 0x0526E519: return ru ? "выполнил(а) задание для" : "completed a mission for";
    case 0x0526E51C: return ru ? "провалил(а) задание для" : "failed a mission for";
    case 0x0526E527: case 0x0667AF08: case 0x0526E4EE: return ru ? "торгует с" : "traded with";
    case 0x0591F833: case 0x0526E53C: case 0x0526E542: case 0x0526E5CF: case 0x0526E5D4: case 0x0526E5D8:
    case 0x05776D99: case 0x068B2971: case 0x057B9100:
        return ru ? "напал(а) на" : "attacked";
    case 0x0526E4E5: case 0x05B6CF09: return ru ? "сделал(а) комплимент" : "complimented";
    case 0x0526E4FE: case 0x05F62736: return ru ? "оскорбил(а)" : "insulted";
    case 0x0526E50E: case 0x0594AFFF: return ru ? "отказал(а) в требовании" : "refused a demand of";
    }
    return nullptr;
}

static void tell(const std::string& who, uint32_t npc, uint32_t ev, const std::string& theirs)
{
    const char* t = event_text(ev, spaceext::russian());
    if (!t)
        return;
    auto key = std::make_pair(npc, ev);
    auto it = g_told.find(key);
    if (it != g_told.end() && now_s() - it->second < 20)
        return;
    g_told[key] = now_s();
    g_last_notice = who + " " + t + " " + shown_name(npc, who, theirs);
    spaceext::notice(g_last_notice);
}

static void tell_state(const std::string& who, uint32_t npc, uint8_t what, bool player_first, const std::string& theirs)
{
    std::string e = shown_name(npc, who, theirs);
    bool ru = spaceext::russian();
    std::string s;
    switch (what) {
    case W_WAR:
        s = player_first ? who + (ru ? " объявил(а) войну: " : " declared war on ") + e
                         : e + (ru ? " объявили войну: " : " declared war on ") + who;
        break;
    case W_ALLY: s = who + (ru ? " заключил(а) союз: " : " formed an alliance with ") + e; break;
    case W_PEACE: s = who + (ru ? " заключил(а) мир: " : " made peace with ") + e; break;
    case W_BREAK: s = who + (ru ? " разорвал(а) союз: " : " ended the alliance with ") + e; break;
    default: return;
    }
    g_last_notice = s;
    spaceext::notice(s);
}

static bool sharing() { return spaceext::connected() && !spaceext::rivals(); }

// ---------------- hooks ----------------
static float SK_THISCALL h_ApplyEv(void* mgr, void* map, uint32_t a, uint32_t b, uint32_t ev, float scale)
{
    if (g_applying || g_depth > 0 || !sharing() || !spaceext::in_space())
        return o_ApplyEv(mgr, map, a, b, ev, scale);
    g_depth++;
    float r = o_ApplyEv(mgr, map, a, b, ev, scale);
    g_depth--;
    uint32_t me = my_pol();
    if (map == (uint8_t*)mgr + 0x24 && mgr == rel_mgr() && is_empire(a) && is_empire(b) && a != b &&
        (a == me || b == me) && !own_event(ev) && scale != 0.f) {
        uint32_t npc = a == me ? b : a;
        if (!local_mirror(npc))
            g_out.push_back({K_EVENT, npc, (uint8_t)(a == me), ev, scale, {}});
    }
    return r;
}

static uint32_t flags_with(void* e1, void* e2)
{
    void* r = record(pol_of(e1), pol_of(e2));
    return r ? F<uint32_t>(r, 4) & 3 : 0;
}

static void state_hook(State_t orig, uint8_t what, void* mgr, void* e1, void* e2)
{
    void* me = my_empire();
    void* other = e2 ? e2 : me;
    bool watch = !g_applying && g_depth == 0 && sharing() && spaceext::in_space() && me && mgr == rel_mgr() &&
                 (e1 == me) != (other == me) && is_empire(pol_of(e1)) && is_empire(pol_of(other));
    uint32_t before = watch ? flags_with(e1, other) : 0;
    g_depth++;
    orig(mgr, e1, e2);
    g_depth--;
    if (!watch)
        return;
    if (flags_with(e1, other) == before)
        return; // nothing changed (war again, peace without a war, ...)
    uint32_t npc = pol_of(e1 == me ? other : e1);
    if (!local_mirror(npc))
        g_out.push_back({K_STATE, npc, (uint8_t)(e1 == me), what, 0, {}});
}
static void SK_THISCALL h_War(void* m, void* a, void* b) { state_hook(o_War, W_WAR, m, a, b); }
static void SK_THISCALL h_Ally(void* m, void* a, void* b) { state_hook(o_Ally, W_ALLY, m, a, b); }
static void SK_THISCALL h_Peace(void* m, void* a, void* b) { state_hook(o_Peace, W_PEACE, m, a, b); }
static void SK_THISCALL h_Break(void* m, void* a, void* b) { state_hook(o_Break, W_BREAK, m, a, b); }

// first contact with an empire (arriving at its star, landing on its planet, ...)
static void SK_THISCALL h_Contact(void* empire, int arg)
{
    uint32_t pol = pol_of(empire);
    bool watch = !g_applying && g_depth == 0 && sharing() && spaceext::in_space() && is_empire(pol) &&
                 pol != my_pol() && !contacted(pol);
    g_depth++;
    o_Contact(empire, arg);
    g_depth--;
    if (watch && contacted(pol) && !local_mirror(pol)) {
        g_out.push_back({K_CONTACT, pol, 0, kMet, 0, {}});
    }
}

// the first-contact talk (message 0x3AC86B5, handler 0xFE3233; also coop.space empire contact)
static void SK_THISCALL h_Spoken(void* empire)
{
    uint32_t pol = pol_of(empire);
    bool watch = !g_applying && g_depth == 0 && sharing() && spaceext::in_space() && is_empire(pol) &&
                 pol != my_pol() && !(contact_of(pol) & kContacted);
    g_depth++;
    o_Spoken(empire);
    g_depth--;
    if (watch && (contact_of(pol) & kContacted) && !local_mirror(pol))
        g_out.push_back({K_CONTACT, pol, 0, kContacted, 0, {}});
}
static bool busy(const Rec& r) { return r.flags || !r.ev.empty(); }

// ---------------- applying a partner's change (outside the frame, g_applying) ----------------
static bool set_state(uint8_t what, void* npc_e, bool player_first)
{
    void* mgr = rel_mgr();
    void* me = my_empire();
    if (!mgr || !me || !npc_e)
        return false;
    void* e1 = player_first ? me : npc_e;
    void* e2 = player_first ? npc_e : me;
    State_t f = what == W_WAR ? o_War : what == W_ALLY ? o_Ally : what == W_PEACE ? o_Peace : o_Break;
    if (!f)
        return false;
    f(mgr, e1, e2);
    return true;
}

static void apply_event(uint8_t from, const Out& m)
{
    void* mgr = rel_mgr();
    uint32_t me = my_pol();
    if (!mgr || !a_Apply || !empire_of(m.npc)) {
        g_unknown++;
        log_line("event " + std::to_string(m.ev) + " for an unknown empire");
        return;
    }
    if (local_mirror(m.npc))
        return; // our own mirror: the partner meant another empire
    uint32_t a = m.first ? me : m.npc, b = m.first ? m.npc : me;
    thiscall<float>(a_Apply, mgr, a, b, m.ev, m.scale);
    g_got_ev++;
    char buf[160];
    snprintf(buf, sizeof buf, "got event %08X x%.2f from %s for %08X '%s' -> %.2f", m.ev, m.scale,
             spaceext::name_of(from).c_str(), m.npc, name_of_pol(m.npc).c_str(), relation(m.npc));
    log_line(buf);
    tell(spaceext::name_of(from), m.npc, m.ev, m.name);
}

static void apply_state(uint8_t from, const Out& m)
{
    void* e = empire_of(m.npc);
    if (local_mirror(m.npc))
        return;
    if (!e || !set_state((uint8_t)m.ev, e, m.first)) {
        g_unknown++;
        log_line("state change for an unknown empire");
        return;
    }
    g_got_state++;
    char buf[160];
    snprintf(buf, sizeof buf, "got state %u from %s for %08X '%s' -> flags %u", m.ev, spaceext::name_of(from).c_str(),
             m.npc, empire_name(e).c_str(), flags_with(e, my_empire()));
    log_line(buf);
    tell_state(spaceext::name_of(from), m.npc, (uint8_t)m.ev, m.first, m.name);
}

static void apply_contact(uint8_t from, const Out& m)
{
    void* e = empire_of(m.npc);
    if (local_mirror(m.npc))
        return;
    if (!e) {
        g_unknown++;
        log_line("contact with an unknown empire");
        return;
    }
    bool had = contact_of(m.npc) & m.ev;
    if (!had && m.ev == kMet && o_Contact)
        o_Contact(e, 0);
    else if (!had && m.ev == kContacted && o_Spoken)
        o_Spoken(e);
    g_got_contact++;
    char buf[160];
    snprintf(buf, sizeof buf, "got contact %s from %s with %08X '%s'%s", m.ev == kMet ? "met" : "talk",
             spaceext::name_of(from).c_str(), m.npc, empire_name(e).c_str(), had ? " (known already)" : "");
    log_line(buf);
    if (!had) {
        g_last_notice = spaceext::name_of(from) + spaceext::tr(" made contact with ", " вступил(а) в контакт: ") +
                        shown_name(m.npc, spaceext::name_of(from), m.name);
        spaceext::notice(g_last_notice);
    }
}

// make the own record of (npc, player) equal to the host's: war / alliance through the game's
// functions, the events by value. Returns true when something had to change.
static bool reconcile(const Rec& h, bool asked)
{
    void* mgr = rel_mgr();
    void* me = my_empire();
    uint32_t mp = my_pol();
    void* e = empire_of(h.npc);
    if (local_mirror(h.npc))
        return false;
    if (!mgr || !me || !e) {
        g_unknown++;
        return false;
    }
    auto it = g_recent.find(h.npc);
    if (!asked && it != g_recent.end() && now_s() - it->second < 8) {
        char buf[96];
        snprintf(buf, sizeof buf, "%08X: own change %.1f s ago, kept", h.npc, now_s() - it->second);
        log_line(buf);
        return false; // our own change is on its way to the host
    }
    bool changed = false;
    uint32_t cf = contact_of(h.npc);
    if ((h.flags & kMet) && o_Contact && !(cf & kMet)) {
        o_Contact(e, 0); // the game's first contact: personality events, the empire on the map
        changed = true;
    }
    if ((h.flags & kContacted) && o_Spoken && !(cf & kContacted)) {
        o_Spoken(e);
        changed = true;
    }
    void* r = record(h.npc, mp);
    uint32_t lf = r ? F<uint32_t>(r, 4) & 3 : 0;
    uint32_t hf = h.flags & 3;
    if ((lf & 2) && !(hf & 2)) { o_Break(mgr, e, me); changed = true; }
    if ((lf & 1) && !(hf & 1)) { o_Peace(mgr, e, me); changed = true; }
    if ((hf & 1) && !(lf & 1)) { o_War(mgr, e, me); changed = true; }
    if ((hf & 2) && !(lf & 2)) { o_Ally(mgr, e, me); changed = true; }
    // events: compare after the state calls (they add events of their own)
    r = record(h.npc, mp);
    std::vector<std::pair<uint32_t, float>> mine;
    if (r)
        walk((uint8_t*)r + 8, [&](void* en) { mine.push_back({F<uint32_t>(en, 0x10), F<float>(en, 0x14)}); });
    bool same = r && mine.size() == h.ev.size();
    for (size_t i = 0; same && i < mine.size(); i++)
        same = mine[i].first == h.ev[i].first && std::fabs(mine[i].second - h.ev[i].second) < 1e-3f;
    if (!same && (r || !h.ev.empty()) && a_Reset && a_EvIndex) {
        thiscall<void>(a_Reset, mgr, h.npc, mp); // creates the record if needed, clears its events
        r = record(h.npc, mp);
        if (r) {
            for (auto& p : h.ev) {
                uint32_t id = p.first;
                float* v = thiscall<float*>(a_EvIndex, (uint8_t*)r + 8, &id);
                if (v)
                    *v = p.second;
            }
            if (a_Calc)
                thiscall<float>(a_Calc, mgr, h.npc, mp, 0);
        }
        changed = true;
        char buf[96];
        snprintf(buf, sizeof buf, "%08X: events set as the host has them (%u -> %u)", h.npc, (unsigned)mine.size(),
                 (unsigned)h.ev.size());
        log_line(buf);
    }
    if (r && (F<uint32_t>(r, 4) & 3) != hf) // the state calls need a record; set the bits as the host has them
        F<uint32_t>(r, 4) = (F<uint32_t>(r, 4) & ~3u) | hf;
    return changed;
}

static std::vector<Rec> g_snap_in; // chunks of the host's table until the last one
static bool g_snap_asked;         // it answers our Resync: the host had our messages before it
static void apply_snapshot()
{
    uint32_t n = 0;
    std::vector<uint32_t> hostset;
    for (auto& h : g_snap_in) {
        hostset.push_back(h.npc);
        if (reconcile(h, g_snap_asked))
            n++;
    }
    std::sort(hostset.begin(), hostset.end());
    for (auto& mine : my_table()) // ours only: events or a state the host's copy does not have
        if (!(mine.flags & (kMet | kContacted)) && busy(mine) && !std::binary_search(hostset.begin(), hostset.end(), mine.npc) &&
            reconcile(Rec{mine.npc, 0, {}}, g_snap_asked))
            n++;
    g_fixed += n;
    g_snaps_got++;
    char buf[96];
    snprintf(buf, sizeof buf, "host table: %u empires, %u fixed", (unsigned)g_snap_in.size(), n);
    log_line(buf);
    g_snap_in.clear();
}

static bool g_snap_ready;
static void send_snapshot(uint8_t reply_to);
static void inbox_task(void*)
{
    g_in_task = false;
    if (!active() || !sharing()) { // not in Space (loading, another stage): entering Space asks again
        if (g_keep_fix && sharing() && !g_in.empty() && g_in_epoch == spaceext::world_epoch()) {
            g_kept_away = (uint32_t)g_in.size(); // the same Space world: replayed when we are back (tick)
            g_snap_in.clear();
            g_snap_ready = false;
            return;
        }
        g_dropped_away += (uint32_t)g_in.size();
        g_in.clear();
        g_snap_in.clear();
        g_snap_ready = false;
        return;
    }
    if (g_in_epoch != spaceext::world_epoch()) { // kept for a world that is gone
        g_dropped_away += (uint32_t)g_in.size();
        g_in.clear();
    }
    if (g_kept_away && !g_in.empty())
        log_line(std::to_string(g_in.size()) + " partner changes made while out of Space replayed");
    g_kept_away = 0;
    auto in = std::move(g_in);
    g_in.clear();
    g_applying = true;
    for (auto& [from, m] : in) {
        if (m.kind == K_EVENT)
            apply_event(from, m);
        else if (m.kind == K_STATE)
            apply_state(from, m);
        else if (m.kind == K_CONTACT)
            apply_contact(from, m);
        else if (m.kind == K_ASK) {
            log_line(spaceext::name_of(from) + " asks for the table");
            g_applying = false;
            send_snapshot(from);
            g_applying = true;
        }
    }
    if (g_snap_ready) {
        g_snap_ready = false;
        apply_snapshot();
    }
    g_applying = false;
}
static void schedule();
// a partner's message to replay (outside the frame)
static void queue_in(uint8_t origin, const Out& m)
{
    if (g_in.empty() || g_in_epoch != spaceext::world_epoch()) {
        g_dropped_away += (uint32_t)g_in.size(); // (another Space world: they meant the old one)
        g_in.clear();
        g_in_epoch = spaceext::world_epoch();
    }
    if (g_in.size() >= kKeepMax) {
        g_in.erase(g_in.begin());
        g_dropped_away++;
    }
    g_in.push_back({origin, m});
    schedule();
}

static void schedule()
{
    if (!g_in_task) {
        g_in_task = true;
        H->run_outside_frame(inbox_task, nullptr);
    }
}

// ---------------- network ----------------
static void send_snapshot(uint8_t reply_to)
{
    if (!spaceext::is_host() || !active())
        return;
    auto t = my_table();
    net::Writer w;
    std::vector<const Rec*> chunk;
    size_t bytes = 0;
    auto flush = [&](bool last) {
        net::Writer b;
        b.u8(last);
        b.u8(reply_to); // 0xFF: the periodic table
        b.u16((uint16_t)chunk.size());
        for (auto* r : chunk) {
            b.u32(r->npc);
            b.u32(r->flags);
            size_t k = std::min<size_t>(r->ev.size(), 100);
            b.u8((uint8_t)k);
            for (size_t i = 0; i < k; i++) {
                b.u32(r->ev[i].first);
                b.f32(r->ev[i].second);
            }
        }
        spaceext::send(K_SNAP, b);
        chunk.clear();
        bytes = 0;
    };
    for (auto& r : t) {
        if (!busy(r))
            continue; // most empires of the galaxy: never met, no events
        size_t sz = 9 + 8 * std::min<size_t>(r.ev.size(), 100);
        if (bytes + sz > 900 && !chunk.empty())
            flush(false);
        chunk.push_back(&r);
        bytes += sz;
    }
    flush(true);
    g_snaps_sent++;
    g_last_snap = now_s();
}

static void on_message(uint8_t kind, uint8_t origin, net::Reader& r)
{
    if (spaceext::rivals()) {
        g_skipped_rivals++;
        return;
    }
    if (kind == K_CONTACT) {
        Out m{kind, r.u32(), 0, 0, 0, {}};
        m.ev = r.u8() == 2 ? kContacted : kMet;
        m.name = r.str8();
        if (r.ok())
            queue_in(origin, m);
    } else if (kind == K_EVENT || kind == K_STATE) {
        Out m{kind, r.u32(), 0, 0, 0, {}};
        m.first = r.u8();
        if (kind == K_EVENT) {
            m.ev = r.u32();
            m.scale = r.f32();
        } else
            m.ev = r.u8();
        m.name = r.str8();
        if (!r.ok())
            return;
        queue_in(origin, m);
    } else if (kind == K_SNAP) {
        if (spaceext::is_host())
            return;
        bool last = r.u8() != 0;
        uint8_t reply_to = r.u8();
        uint16_t n = r.u16();
        std::vector<Rec> part;
        for (uint16_t i = 0; i < n && r.ok(); i++) {
            Rec h{r.u32(), r.u32() & (3 | kMet | kContacted), {}};
            uint8_t k = r.u8();
            for (uint8_t j = 0; j < k && r.ok(); j++) {
                uint32_t id = r.u32();
                float v = r.f32();
                h.ev.push_back({id, v});
            }
            std::sort(h.ev.begin(), h.ev.end());
            part.push_back(std::move(h));
        }
        if (!r.ok()) { // a cut chunk: the table is incomplete, wait for the next one
            if (!g_snap_ready)
                g_snap_in.clear();
            return;
        }
        for (auto& h : part)
            g_snap_in.push_back(std::move(h));
        if (last) {
            g_snap_ready = true;
            g_snap_asked = reply_to == spaceext::my_id();
            schedule();
        }
    } else if (kind == K_ASK) {
        if (spaceext::is_host()) { // after the messages that came before it (same queue)
            queue_in(origin, Out{K_ASK, 0, origin, 0, 0, {}});
        }
    }
}

static void tick(double now)
{
    bool sp = spaceext::in_space();
    if (!spaceext::connected()) {
        g_out.clear();
        g_was_space = false;
        return;
    }
    if (sp && !g_was_space && !spaceext::is_host())
        g_ask_pending = true; // entered space (joined, loaded): ask for the host's table
    g_was_space = sp;
    if (!sp) {
        g_out.clear();
        return;
    }
    if (g_kept_away && !g_in.empty() && !g_in_task)
        schedule(); // back in Space: the partners' changes kept meanwhile (BUG-0544)
    if (spaceext::rivals()) {
        g_out.clear();
        return;
    }
    if (g_ask_pending && active()) {
        g_ask_pending = false;
        spaceext::send(K_ASK, net::Writer());
    }
    for (auto& m : g_out) {
        net::Writer w;
        w.u32(m.npc);
        if (m.kind != K_CONTACT)
            w.u8(m.first);
        if (m.kind == K_CONTACT) {
            w.u8(m.ev == kContacted ? 2 : 1);
            g_sent_contact++;
        }
        else if (m.kind == K_EVENT) {
            w.u32(m.ev);
            w.f32(m.scale);
            g_sent_ev++;
        } else {
            w.u8((uint8_t)m.ev);
            g_sent_state++;
        }
        w.str8(name_of_pol(m.npc));
        spaceext::send(m.kind, w);
        g_recent[m.npc] = now_s(); // a host table sent before this arrives there must not undo it
        char buf[120];
        snprintf(buf, sizeof buf, "sent %s %08X for %08X '%s'",
                 m.kind == K_EVENT ? "event" : m.kind == K_STATE ? "state" : "contact", m.ev, m.npc,
                 name_of_pol(m.npc).c_str());
        log_line(buf);
    }
    g_out.clear();
    // (not while partners' changes wait to be replayed: the table would not have them yet)
    if (spaceext::is_host() && active() && now - g_last_snap > 20 && (g_in.empty() || !g_keep_fix))
        send_snapshot(0xFF);
}

static void on_session_end()
{
    g_out.clear();
    g_in.clear();
    g_snap_in.clear();
    g_snap_ready = false;
    g_recent.clear();
    g_was_space = false;
}

// ---------------- the Space page and debug ----------------
static std::vector<spaceext::Row> rows()
{
    std::vector<spaceext::Row> v;
    if (!spaceext::in_space() || !spaceext::connected())
        return v;
    bool ru = spaceext::russian();
    spaceext::Row r;
    if (spaceext::rivals()) {
        r.text = ru ? "Дипломатия: у каждого своя (режим соперников)" : "Diplomacy: your own (rivals mode)";
        v.push_back(r);
        return v;
    }
    auto t = my_table();
    int allies = 0, wars = 0, met = 0;
    for (auto& x : t) {
        met += (x.flags & (kMet | kContacted)) != 0;
        allies += (x.flags & 2) != 0;
        wars += (x.flags & 1) != 0;
    }
    char buf[160];
    snprintf(buf, sizeof buf, ru ? "Общая дипломатия: %d империй, союзы %d, войны %d"
                                 : "Shared diplomacy: %d empires, %d allies, %d wars",
             met, allies, wars);
    r.text = buf;
    if (!spaceext::is_host()) {
        r.button = ru ? "Сверить" : "Resync";
        r.on_click = [] {
            spaceext::send(K_ASK, net::Writer());
            log_line("asked the host for its table (Resync)");
        };
    }
    v.push_back(r);
    if (!g_last_notice.empty()) {
        spaceext::Row n;
        n.text = g_last_notice;
        n.color = 0xFFB0D0FF;
        v.push_back(n);
    }
    return v;
}

static std::string status()
{
    char b[360];
    snprintf(b, sizeof b,
             "diplomacy: %s sent ev=%u state=%u contact=%u got ev=%u state=%u contact=%u snaps sent=%u got=%u fixed=%u"
             " unknown=%u rivals_skipped=%u kept_away=%u dropped_away=%u",
             a_ApplyEv && o_ApplyEv ? "on" : "no hooks", g_sent_ev, g_sent_state, g_sent_contact, g_got_ev, g_got_state,
             g_got_contact, g_snaps_sent,
             g_snaps_got, g_fixed, g_unknown, g_skipped_rivals, (unsigned)(g_kept_away ? g_in.size() : 0), g_dropped_away);
    return b;
}

static uint32_t parse_pol(const std::string& s) { return (uint32_t)strtoul(s.c_str(), nullptr, 16); }

static std::string debug(const std::vector<std::string>& a)
{
    char b[400];
    std::string out;
    if (a.empty() || a[0] == "status")
        return status() + "\n";
    if (!active())
        return "not in space\n";
    void* mgr = rel_mgr();
    uint32_t me = my_pol();
    if (a[0] == "list") { // the empires of the galaxy: those with a name, events or a state (list all: every one)
        bool all = a.size() > 1 && a[1] == "all";
        void* sm = cdecl_call<void*>(a_StarMgr);
        auto t = my_table();
        std::map<uint32_t, const Rec*> by;
        for (auto& r : t)
            by[r.npc] = &r;
        int n = 0, shown = 0, named = 0;
        walk((uint8_t*)sm + 0x150, [&](void* node) {
            uint32_t pol = F<uint32_t>(node, 0x10);
            void* e = F<void*>(node, 0x14);
            if (pol == me || !e)
                return;
            n++;
            auto it = by.find(pol);
            std::string nm = empire_name(e);
            bool has = it != by.end() && (it->second->flags || !it->second->ev.empty());
            named += nm != "unknown";
            if (!all && !has && nm == "unknown")
                return;
            if (shown++ >= 60)
                return;
            snprintf(b, sizeof b, "empire %08X '%s' home=%u rel=%.2f flags=%u events=%u\n", pol, nm.c_str(),
                     F<uint32_t>(e, 0xB0), relation(pol), it != by.end() ? it->second->flags : 0u,
                     it != by.end() ? (unsigned)it->second->ev.size() : 0);
            out += b;
        });
        int busy = 0;
        for (auto& r : t)
            busy += r.flags || !r.ev.empty();
        snprintf(b, sizeof b, "player %08X '%s' empires %d named %d records %d with events or state %d\n", me,
                 empire_name(my_empire()).c_str(), n, named, (int)t.size(), busy);
        return b + out;
    }
    if (a[0] == "near" && a_StarRec) { // the empires nearest to our home star (tests: where to make contact)
        void* sm = cdecl_call<void*>(a_StarMgr);
        void* hs = thiscall<void*>(a_StarRec, sm, F<uint32_t>(my_empire(), 0xB0));
        if (!hs)
            return "no home star\n";
        float* hp = (float*)((uint8_t*)hs + 0x3C);
        std::vector<std::pair<float, uint32_t>> d;
        walk((uint8_t*)sm + 0x150, [&](void* node) {
            void* e = F<void*>(node, 0x14);
            uint32_t pol = F<uint32_t>(node, 0x10);
            if (!e || pol == me)
                return;
            void* st = thiscall<void*>(a_StarRec, sm, F<uint32_t>(e, 0xB0));
            if (!st)
                return;
            float* p = (float*)((uint8_t*)st + 0x3C);
            float dx = p[0] - hp[0], dy = p[1] - hp[1], dz = p[2] - hp[2];
            d.push_back({std::sqrt(dx * dx + dy * dy + dz * dz), pol});
        });
        std::sort(d.begin(), d.end());
        size_t n = a.size() > 1 ? (size_t)atoi(a[1].c_str()) : 8;
        for (size_t i = 0; i < d.size() && i < n; i++) {
            void* e = empire_of(d[i].second);
            snprintf(b, sizeof b, "near %08X '%s' home=%u (%08X) dist=%.2f rel=%.2f contact=%u\n", d[i].second,
                     empire_name(e).c_str(), F<uint32_t>(e, 0xB0), F<uint32_t>(e, 0xB0), d[i].first,
                     relation(d[i].second), contact_of(d[i].second));
            out += b;
        }
        return out;
    }
    if (a[0] == "table") { // only the contacts, with their events (compare two games)
        for (auto& r : my_table()) {
            if (!busy(r))
                continue;
            snprintf(b, sizeof b, "rec %08X flags=%u rel=%.2f", r.npc, r.flags, relation(r.npc));
            out += b;
            for (auto& e : r.ev) {
                snprintf(b, sizeof b, " %08X=%.3f", e.first, e.second);
                out += b;
            }
            out += "\n";
        }
        return out.empty() ? "no contacts\n" : out;
    }
    if (a[0] == "log") {
        for (auto& l : g_log)
            out += l + "\n";
        return out.empty() ? "empty\n" : out;
    }
    if (a[0] == "mirrorfix" && a.size() > 1) { // BUG-0071: 0 = share the allies mirror's record (tests)
        g_mirror_fix = a[1] != "0";
        snprintf(b, sizeof b, "mirrorfix %d skipped=%u\n", (int)g_mirror_fix, g_mirror_skipped);
        return b;
    }
    if (a[0] == "sync") {
        if (spaceext::is_host())
            g_last_snap = -1e9;
        else
            spaceext::send(K_ASK, net::Writer());
        return "asked\n";
    }
    // the game's own functions, as the player's actions would call them (shared through the hooks);
    // "local" before the verb: not shared (tests: make the copies differ)
    bool local = a[0] == "local";
    std::vector<std::string> v(a.begin() + (local ? 1 : 0), a.end());
    if (v.size() >= 2 && (v[0] == "apply" || v[0] == "war" || v[0] == "ally" || v[0] == "peace" || v[0] == "break" ||
                          v[0] == "contact")) {
        uint32_t npc = parse_pol(v[1]);
        void* e = empire_of(npc);
        if (!e || npc == me)
            return "no such empire\n";
        g_applying = local;
        if (v[0] == "contact") {
            if (a_Contact)
                thiscall<void>(a_Contact, e, 0); // through the hook, as arriving at the empire's star does
            if (a_Spoken && v.size() > 2 && v[2] == "talk")
                thiscall<void>(a_Spoken, e); // and the first-contact talk
        } else if (v[0] == "apply") {
            uint32_t ev = v.size() > 2 ? parse_pol(v[2]) : 0x0526E51F; // default: a gift
            float sc = v.size() > 3 ? (float)atof(v[3].c_str()) : 1.f;
            thiscall<float>(a_Apply, mgr, npc, me, ev, sc);
        } else {
            void* f = (void*)(v[0] == "war" ? a_War : v[0] == "ally" ? a_Ally : v[0] == "peace" ? a_Peace : a_Break);
            thiscall<void>((uintptr_t)f, mgr, e, my_empire()); // through the hook like the game's own call
        }
        g_applying = false;
        snprintf(b, sizeof b, "%s %08X '%s': rel=%.2f flags=%u\n", v[0].c_str(), npc, empire_name(e).c_str(),
                 relation(npc), flags_with(e, my_empire()));
        return b;
    }
    return "usage: coop.space diplomacy [status | list | table | log | sync | [local] apply <pol> [event] [scale] |"
           " [local] war|ally|peace|break|contact <pol> | near [n]]\n";
}

static bool init(const SkHost* h, SkMod* m)
{
    H = h;
    M = m;
    auto S = [](const char* n) { return H->sym(n); };
    a_RelGet = S("Simulator::cRelationshipManager::Get");
    a_Apply = S("Simulator::cRelationshipManager::ApplyRelationship");
    a_ApplyEv = S("SporeCoop::cRelationshipManager_ApplyEvent");
    a_War = S("Simulator::cRelationshipManager::DeclareWar");
    a_Ally = S("Simulator::cRelationshipManager::DeclareAlliance");
    a_Peace = S("Simulator::cRelationshipManager::DeclarePeace");
    a_Break = S("Simulator::cRelationshipManager::BreakAlliance");
    a_Reset = S("Simulator::cRelationshipManager::ResetRelationship");
    a_Calc = S("Simulator::cRelationshipManager::CalculateRelationship");
    a_EvIndex = S("SporeCoop::RelEventMap_Index");
    a_StarMgr = S("Simulator::cStarManager::Get");
    a_GetEmpire = S("Simulator::cStarManager::GetEmpire");
    a_StarRec = S("Simulator::cStarManager::GetStarRecord");
    a_PlayerEmpire = S("Simulator::GetPlayerEmpire");
    a_PlayerPol = S("Simulator::GetPlayerEmpireID");
    a_Contact = S("SporeCoop::cEmpire_MakeContact");
    a_Spoken = S("SporeCoop::cEmpire_SetContactedByPlayer");
    a_NounMgr = S("Simulator::cGameNounManager::Get");
    if (!(a_RelGet && a_Apply && a_ApplyEv && a_War && a_Ally && a_Peace && a_Break && a_StarMgr && a_GetEmpire &&
          a_PlayerEmpire && a_PlayerPol))
        return false;
    bool ok = H->hook_create(M, (void*)a_ApplyEv, (void*)h_ApplyEv, (void**)&o_ApplyEv) == 0;
    ok = H->hook_create(M, (void*)a_War, (void*)h_War, (void**)&o_War) == 0 && ok;
    ok = H->hook_create(M, (void*)a_Ally, (void*)h_Ally, (void**)&o_Ally) == 0 && ok;
    ok = H->hook_create(M, (void*)a_Peace, (void*)h_Peace, (void**)&o_Peace) == 0 && ok;
    ok = H->hook_create(M, (void*)a_Break, (void*)h_Break, (void**)&o_Break) == 0 && ok;
    if (a_Contact && a_NounMgr) // optional: without it first contacts arrive with the host's table only
        ok = H->hook_create(M, (void*)a_Contact, (void*)h_Contact, (void**)&o_Contact) == 0 && ok;
    if (a_Spoken && a_NounMgr)
        ok = H->hook_create(M, (void*)a_Spoken, (void*)h_Spoken, (void**)&o_Spoken) == 0 && ok;
    return ok && o_ApplyEv && o_War && o_Ally && o_Peace && o_Break;
}

// coop.wstate (census.h, the divergence detector): every NPC empire with a relation record, as the game
// computes the relation (CalculateRelationship) and its war / alliance / contact flags
std::string wstate()
{
    std::string out;
    char b[96];
    if (!rel_mgr() || !is_empire(my_pol()))
        return out;
    for (auto& r : my_table()) {
        snprintf(b, sizeof b, "emp pol=%08X rel=%.2f flags=%u\n", r.npc, relation(r.npc), r.flags);
        out += b;
        if (out.size() > 6000)
            break;
    }
    return out;
}

static bool reg = spaceext::add({.name = "diplomacy",
                                 .kind_lo = 30,
                                 .kind_hi = 39,
                                 .init = init,
                                 .tick = tick,
                                 .on_message = on_message,
                                 .on_session_end = on_session_end,
                                 .rows = rows,
                                 .status = status,
                                 .debug = debug});

} // namespace diplo
