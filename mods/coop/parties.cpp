// Decision 6J: parties of 4, the messages (parties.h has the rules). The host keeps the book and sends it to every
// guest (MSG_PARTY kTable, reliable) on every change; a guest asks the host (invite, answer, leave, lead) and the host
// answers the asking player (kResult) and offers an invite to the invited one (kOffer).
// Decision 6K: connected to a galaxy server, the server keeps the parties instead (net/galaxy.h PARTY .. PLAYERS): they
// last across planets and hand-overs and work in the galaxy menu; this game holds its own party's view (g_gx).
#include "parties.h"
#include "parties_net.h"
#include "net/galaxy.h"
#include "net/utf8.h"

#include <cstdio>

namespace parties {

static const SkHost* H;
static SkMod* M;
static Ctx C;
static Book g_book;
static int g_offer_from = -1; // this game: who invited it (-1 none)
static double g_offer_at;
static uint32_t g_tables_sent, g_tables_got, g_requests;
// 6K: the galaxy server's view of this game's party
static struct {
    bool on = false; // a PARTYVIEW arrived while connected
    uint16_t party = 0, leader = 0, inviter = 0;
    std::vector<Member> members;
    uint64_t inviter_id = 0;
    std::string inviter_name;
    double invite_at = 0;
} g_gx;
static std::vector<Online> g_online;
static uint32_t g_stage_sent = 0xFFFFFFFF;
static uint32_t g_views_got, g_notes_got;
// BUG-9266: host: each guest's galaxy party (kGxParty); guest: the party id last told the host (-1: not yet)
static std::map<uint8_t, uint16_t> g_gx_of;
static int g_gx_sent = -1;

bool galaxy() { return g_gx.on && C.galaxy_welcomed && C.galaxy_welcomed(); }
static uint64_t ident(uint8_t id) { return C.identity_of ? C.identity_of(id) : 0; }
static bool in_gx_party(uint64_t id)
{
    if (!id)
        return false;
    for (auto& m : g_gx.members)
        if (m.identity == id)
            return true;
    return false;
}

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host; }
static uint8_t me() { return C.sess ? C.sess->my_id() : 0; }
static std::string name(uint8_t id) { return C.name_of ? C.name_of(id) : "player " + std::to_string(id); }
static void feed(const std::string& s)
{
    H->log(M, SK_INFO, "party: %s", s.c_str());
    if (C.feed)
        C.feed(s);
}

static void send_table()
{
    if (!is_host())
        return;
    net::Writer w;
    w.u8(kTable);
    const auto& all = g_book.all();
    w.u8((uint8_t)std::min<size_t>(all.size(), 255));
    for (size_t i = 0; i < all.size() && i < 255; i++) {
        w.u16(all[i].id);
        w.u8(all[i].leader);
        w.u8((uint8_t)all[i].members.size());
        for (uint8_t m : all[i].members)
            w.u8(m);
    }
    for (auto& p : C.sess->players())
        if (p.id != me())
            C.sess->send_reliable_to(p.id, net::MSG_PARTY, w);
    g_tables_sent++;
    if (C.changed)
        C.changed();
}

static void result_to(uint8_t to, uint8_t what, uint8_t other, Result r)
{
    if (to == me()) {
        if (r != Result::Ok)
            feed(std::string("Party: ") + text(r) + ".");
        return;
    }
    net::Writer w;
    w.u8(kResult);
    w.u8(what);
    w.u8(other);
    w.u8((uint8_t)r);
    C.sess->send_reliable_to(to, net::MSG_PARTY, w);
}

static void offer_to(uint8_t to, uint8_t from)
{
    if (to == me()) {
        g_offer_from = from;
        g_offer_at = H->time_seconds();
        feed(name(from) + " invites you to their party.");
        if (C.changed)
            C.changed();
        return;
    }
    net::Writer w;
    w.u8(kOffer);
    w.u8(from);
    C.sess->send_reliable_to(to, net::MSG_PARTY, w);
}

static uint32_t stage(uint8_t id) { return C.stage_of ? C.stage_of(id) : 0; }

// host: a request of player `from` (the host's own too)
static void host_request(uint8_t from, uint8_t kind, uint8_t other, uint8_t flag)
{
    const double now = H->time_seconds();
    g_requests++;
    Result r = Result::Ok;
    switch (kind) {
    case kInvite:
        r = g_book.invite(from, other, stage(from), stage(other), now);
        if (r == Result::Ok) {
            offer_to(other, from);
            feed(name(from) + " invited " + name(other) + " to their party.");
        }
        result_to(from, kind, other, r);
        break;
    case kAnswer: {
        r = g_book.answer(from, other, flag != 0, now);
        if (r != Result::NoInvite && r != Result::Unknown) // (2026-11-18 review: not for an answer to no invite)
            result_to(other, kind, from, r); // (the inviter hears how it went)
        if (r == Result::Ok) {
            feed(name(from) + " joined " + name(other) + "'s party.");
            send_table();
        } else if (r != Result::Declined)
            result_to(from, kind, other, r);
        else
            feed(name(from) + " declined " + name(other) + "'s invite.");
        break;
    }
    case kLeave: {
        const Party* was = g_book.find(from);
        const bool alone = !was || was->members.size() <= 1; // (nothing to leave: nothing announced)
        r = g_book.leave(from);
        if (r == Result::Ok && alone) {
            result_to(from, kind, from, r);
            break;
        }
        if (r == Result::Ok) {
            feed(name(from) + " left their party.");
            send_table();
        }
        result_to(from, kind, from, r);
        break;
    }
    case kKick: {
        r = g_book.kick(from, other);
        if (r == Result::Ok) {
            feed(name(from) + " removed " + name(other) + " from the party.");
            send_table();
            if (other != me()) { // (the removed player hears it)
                net::Writer w;
                w.u8(kResult);
                w.u8(kKick);
                w.u8(from);
                w.u8((uint8_t)Result::Kicked);
                C.sess->send_reliable_to(other, net::MSG_PARTY, w);
            } else
                feed(std::string("Party: ") + text(Result::Kicked) + ".");
        }
        result_to(from, kind, other, r);
        break;
    }
    case kLead:
        r = g_book.set_leader(from, other);
        if (r == Result::Ok) {
            feed(name(other) + " leads the party now.");
            send_table();
        }
        result_to(from, kind, other, r);
        break;
    default:
        break;
    }
}

static std::string request(uint8_t kind, uint8_t other, uint8_t flag)
{
    if (!C.sess || !C.sess->connected())
        return "Not in a session.";
    if (is_host()) {
        host_request(me(), kind, other, flag);
        return "";
    }
    net::Writer w;
    w.u8(kind);
    w.u8(other);
    w.u8(flag);
    C.sess->send_reliable_to(0, net::MSG_PARTY, w);
    return "";
}

void init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
}

void on_session_start()
{
    g_book.clear();
    g_offer_from = -1;
    g_gx_of.clear();
    g_gx_sent = -1;
    if (is_host())
        g_book.add(me());
}

void on_session_end()
{
    g_book.clear();
    g_offer_from = -1;
    g_gx_of.clear();
    g_gx_sent = -1;
    if (C.changed)
        C.changed();
}

void on_player_join(uint8_t id)
{
    if (!is_host() || galaxy())
        return;
    if (!g_book.find(me()))
        g_book.add(me());
    // a plain co-op join: the host's party ("the person you joined"); a galaxy planet: alone
    g_book.add(id, C.galaxy && C.galaxy() ? -1 : (int)me());
    send_table();
}

void on_player_leave(uint8_t id)
{
    if (g_offer_from == id)
        g_offer_from = -1;
    g_gx_of.erase(id);
    if (!is_host() || galaxy())
        return;
    g_book.remove(id);
    send_table();
}

void on_message(uint8_t from, net::Reader& r)
{
    const uint8_t kind = r.u8();
    if (!r.ok())
        return;
    if (is_host() && kind == kGxParty) { // BUG-9266
        const uint16_t party = r.u16();
        if (r.ok() && from != me())
            g_gx_of[from] = party;
        return;
    }
    if (is_host()) {
        if (kind != kInvite && kind != kAnswer && kind != kLeave && kind != kLead && kind != kKick)
            return;
        const uint8_t other = r.u8(), flag = r.u8();
        if (r.ok())
            host_request(from, kind, other, flag);
        return;
    }
    if (from != 0) // a guest takes the book and its answers from the host only
        return;
    if (kind == kTable) {
        std::vector<Party> v;
        const uint8_t n = r.u8();
        for (uint8_t i = 0; i < n && r.ok(); i++) {
            Party q;
            q.id = r.u16();
            q.leader = r.u8();
            const uint8_t k = r.u8();
            for (uint8_t j = 0; j < k && r.ok(); j++)
                q.members.push_back(r.u8());
            if (q.members.size() > kMaxMembers || !q.has(q.leader))
                return; // (a broken table: keep the last good one)
            v.push_back(std::move(q));
        }
        if (!r.ok())
            return;
        g_book.set_all(std::move(v));
        g_tables_got++;
        if (C.changed)
            C.changed();
    } else if (kind == kOffer) {
        const uint8_t inviter = r.u8();
        if (r.ok())
            offer_to(me(), inviter);
    } else if (kind == kResult) {
        const uint8_t what = r.u8(), other = r.u8(), code = r.u8();
        if (!r.ok())
            return;
        const Result res = (Result)code;
        if (what == kAnswer && res == Result::Declined)
            feed(name(other) + " declined your invite.");
        else if (what == kAnswer && res == Result::Ok)
            feed(name(other) + " joined your party.");
        else if (res != Result::Ok)
            feed(std::string("Party: ") + text(res) + ".");
    }
}

void tick(double now)
{
    if (is_host())
        g_book.lapse(now);
    // BUG-9266: a guest on a galaxy planet tells the host its party (the host scales difficulty by each party's size)
    if (galaxy() && C.sess && C.sess->connected() && !is_host()) {
        if (g_gx_sent != (int)g_gx.party) {
            net::Writer w;
            w.u8(kGxParty);
            w.u16(g_gx.party);
            C.sess->send_reliable_to(0, net::MSG_PARTY, w);
            g_gx_sent = g_gx.party;
        }
    } else
        g_gx_sent = -1;
    if (C.galaxy_welcomed && C.galaxy_welcomed()) { // 6K: the server learns the stage this game plays (cross-stage rule)
        const uint32_t st = C.stage_of ? C.stage_of(me()) : 0;
        if (st != g_stage_sent && C.galaxy_send && C.galaxy_send(net::galaxy::kPartyStage, 0, 0, st))
            g_stage_sent = st;
    } else {
        g_stage_sent = 0xFFFFFFFF;
        if (g_gx.on) {
            g_gx = {};
            if (C.changed)
                C.changed();
        }
    }
    if (g_gx.inviter && now - g_gx.invite_at > kInviteSeconds) {
        g_gx.inviter = 0;
        if (C.changed)
            C.changed();
    }
    if (g_offer_from >= 0 && now - g_offer_at > kInviteSeconds) {
        g_offer_from = -1;
        if (C.changed)
            C.changed();
    }
}

static std::string gx_request(uint8_t kind, uint64_t other, uint8_t flag)
{
    if (!C.galaxy_send || !C.galaxy_send(kind, other, flag, C.stage_of ? C.stage_of(me()) : 0))
        return "Not connected to the galaxy server.";
    g_requests++;
    return "";
}
static std::string gx_by_id(uint8_t kind, uint8_t id)
{
    const uint64_t other = ident(id);
    if (!other)
        return "That player is not known to the galaxy yet.";
    return gx_request(kind, other, 0);
}
std::string invite(uint8_t to) { return galaxy() ? gx_by_id(net::galaxy::kPartyInvite, to) : request(kInvite, to, 0); }
std::string kick(uint8_t id) { return galaxy() ? gx_by_id(net::galaxy::kPartyKick, id) : request(kKick, id, 0); }
std::string invite_identity(uint64_t id) { return gx_request(net::galaxy::kPartyInvite, id, 0); }
std::string kick_identity(uint64_t id) { return gx_request(net::galaxy::kPartyKick, id, 0); }
std::string lead_identity(uint64_t id) { return gx_request(net::galaxy::kPartyLead, id, 0); }
std::string answer(bool yes)
{
    if (galaxy()) {
        if (!g_gx.inviter)
            return "No party invite to answer.";
        const uint64_t from = g_gx.inviter_id;
        g_gx.inviter = 0;
        if (C.changed)
            C.changed();
        return gx_request(net::galaxy::kPartyAnswer, from, yes ? 1 : 0);
    }
    if (g_offer_from < 0)
        return "No party invite to answer.";
    const uint8_t from = (uint8_t)g_offer_from;
    g_offer_from = -1;
    if (C.changed)
        C.changed();
    return request(kAnswer, from, yes ? 1 : 0);
}
std::string leave() { return galaxy() ? gx_request(net::galaxy::kPartyLeave, 0, 0) : request(kLeave, me(), 0); }
std::string make_leader(uint8_t to) { return galaxy() ? gx_by_id(net::galaxy::kPartyLead, to) : request(kLead, to, 0); }

int pending_invite() { return galaxy() ? (g_gx.inviter ? (int)g_gx.inviter : -1) : g_offer_from; }
std::string pending_name()
{
    if (galaxy())
        return g_gx.inviter ? g_gx.inviter_name : "";
    return g_offer_from >= 0 ? name((uint8_t)g_offer_from) : "";
}
bool active() { return galaxy() || !g_book.all().empty(); }
std::vector<Member> members()
{
    std::vector<Member> v;
    if (galaxy())
        return g_gx.members;
    const Party* q = g_book.find(me());
    if (!q) {
        Member m;
        m.name = name(me()), m.leader = true, m.me = true;
        v.push_back(m);
        return v;
    }
    for (uint8_t p : q->members) {
        Member m;
        m.identity = ident(p);
        m.name = name(p);
        m.leader = p == q->leader;
        m.me = p == me();
        v.push_back(m);
    }
    return v;
}
std::vector<Online> online() { return g_online; }
void ask_online()
{
    if (C.galaxy_who)
        C.galaxy_who();
}
void on_galaxy(uint8_t type, net::Reader& r)
{
    using namespace net::galaxy;
    if (type == PARTYVIEW) {
        decltype(g_gx) v;
        v.on = true;
        v.party = r.u16();
        v.leader = r.u16();
        const uint8_t n = r.u8();
        const uint16_t mine = C.galaxy_slot ? C.galaxy_slot() : 0;
        for (uint8_t i = 0; i < n && r.ok(); i++) {
            Member m;
            m.slot = r.u16();
            m.identity = r.u64();
            m.name = utf8::player_name(r.str8()); // (2026-11-18 review: cleaned as the session's names are)
            m.planet = utf8::clean_text(r.str8(), 64);
            m.stage = r.u32();
            m.leader = m.slot == v.leader;
            m.me = m.slot == mine;
            v.members.push_back(std::move(m));
        }
        v.inviter = r.u16();
        v.inviter_id = r.u64();
        v.inviter_name = utf8::player_name(r.str8());
        if (!r.ok() || n > kMaxMembers)
            return;
        const bool new_invite = v.inviter && v.inviter != g_gx.inviter;
        const size_t was = g_gx.members.size();
        v.invite_at = new_invite ? H->time_seconds() : g_gx.invite_at;
        g_gx = std::move(v);
        g_views_got++;
        if (new_invite)
            feed(g_gx.inviter_name + " invites you to their party.");
        if (g_gx.members.size() != was && was)
            H->log(M, SK_INFO, "party: %zu in this game's party now", g_gx.members.size());
        if (C.changed)
            C.changed();
    } else if (type == PARTYNOTE) {
        const uint8_t kind = r.u8(), code = r.u8();
        const std::string other = utf8::player_name(r.str8());
        if (!r.ok())
            return;
        g_notes_got++;
        const Result res = (Result)code;
        if (kind == kPartyAnswer && res == Result::Declined)
            feed(other + " declined your invite.");
        else if (kind == kPartyAnswer && res == Result::Ok)
            feed(other + " joined your party.");
        else if (kind == kPartyKick && res == Result::Kicked)
            feed(other + " removed you from the party.");
        else if (kind == kPartyKick && res == Result::Ok)
            feed(other + " is out of your party.");
        else if (kind == kPartyInvite && res == Result::Ok)
            feed("Invited " + other + ".");
        else if (res != Result::Ok)
            feed(std::string("Party: ") + text(res) + ".");
    } else if (type == PLAYERS) {
        std::vector<Online> v;
        const uint16_t n = r.u16();
        for (uint16_t i = 0; i < n && r.ok(); i++) {
            Online o;
            o.slot = r.u16();
            o.identity = r.u64();
            o.name = utf8::player_name(r.str8());
            o.planet = utf8::clean_text(r.str8(), 64);
            o.party = r.u16();
            v.push_back(std::move(o));
        }
        if (r.ok())
            g_online = std::move(v);
        if (C.changed)
            C.changed();
    }
}
const Book& book() { return g_book; }
bool same_party(uint8_t a, uint8_t b)
{
    if (galaxy()) // (only this game's own party is known here)
        return a == b || (in_gx_party(ident(a)) && in_gx_party(ident(b)));
    const Party* q = g_book.find(a);
    return q && q->has(b);
}
std::vector<uint8_t> my_party()
{
    if (galaxy()) { // the session's players in this game's party
        std::vector<uint8_t> v{me()};
        if (C.sess)
            for (auto& p : C.sess->players())
                if (p.id != me() && in_gx_party(ident(p.id)))
                    v.push_back(p.id);
        return v;
    }
    const Party* q = g_book.find(me());
    return q ? q->members : std::vector<uint8_t>{me()};
}
// a guest's reported party, 0 when unknown or when it names the host's party without being in it (the host's own
// view, from the server, decides who is in its party)
static uint16_t reported_party(uint8_t p)
{
    auto it = g_gx_of.find(p);
    return it == g_gx_of.end() || it->second == g_gx.party ? 0 : it->second;
}
unsigned party_size(uint8_t p)
{
    if (!C.sess || !C.sess->connected())
        return 1;
    if (!galaxy()) {
        const Party* q = g_book.find(p);
        return q ? (unsigned)q->members.size() : 1u;
    }
    if (p == me() || (is_host() && in_gx_party(ident(p))))
        return (unsigned)my_party().size();
    if (!is_host()) // (a guest knows its own party only)
        return same_party(p, me()) ? (unsigned)my_party().size() : 1u;
    std::vector<uint16_t> keys;
    for (auto& q : C.sess->players())
        if (q.id != me() && q.id != p)
            keys.push_back(reported_party(q.id));
    return party_count(reported_party(p), keys);
}
uint64_t identity(uint8_t p) { return ident(p); }
uint32_t table_key(uint8_t p)
{
    if (!galaxy() || !C.sess || !C.sess->connected() || p == me() || in_gx_party(ident(p)))
        return 0;
    const uint16_t k = reported_party(p);
    return k ? k : 0x10000u | p;
}
unsigned largest_party()
{
    unsigned n = 1;
    if (C.sess && C.sess->connected())
        for (auto& q : C.sess->players())
            n = std::max(n, party_size(q.id));
    return n;
}
int leader_of(uint8_t p)
{
    if (galaxy()) {
        const uint64_t id = ident(p);
        for (auto& m : g_gx.members)
            if (m.leader && m.identity == id)
                return p;
        return -1;
    }
    const Party* q = g_book.find(p);
    return q ? q->leader : -1;
}
bool i_lead()
{
    if (galaxy())
        return g_gx.members.size() <= 1 || (C.galaxy_slot && g_gx.leader == C.galaxy_slot());
    const Party* q = g_book.find(me());
    return !q || q->leader == me();
}

std::string status()
{
    std::string s;
    char b[160];
    snprintf(b, sizeof b, "parties: %zu (tables sent %u, got %u, requests %u), my invite from %d\n", g_book.all().size(),
             g_tables_sent, g_tables_got, g_requests, g_offer_from);
    s = b;
    if (galaxy()) { // 6K
        snprintf(b, sizeof b, "galaxy party %u (views %u, notes %u), leader slot %u, invite from %u '%s', online %zu:",
                 g_gx.party, g_views_got, g_notes_got, g_gx.leader, g_gx.inviter, g_gx.inviter_name.c_str(), g_online.size());
        s += b;
        for (auto& m : g_gx.members)
            s += " " + std::to_string(m.slot) + " '" + m.name + "'" + (m.leader ? " (leader)" : "") + (m.me ? " (me)" : "") +
                 (m.planet.empty() ? " [menu]" : " [" + m.planet + "]");
        s += "\n";
        if (C.sess && C.sess->connected()) { // BUG-9266: the party size each player's difficulty counts
            s += "party sizes here:";
            for (auto& q : C.sess->players())
                s += " " + std::to_string(q.id) + "=" + std::to_string(party_size(q.id)) +
                     (q.id != me() && is_host() ? "(p" + std::to_string(g_gx_of.count(q.id) ? g_gx_of[q.id] : 0) + ")" : "");
            s += "\n";
        }
        return s;
    }
    for (auto& q : g_book.all()) {
        snprintf(b, sizeof b, "  party %u: leader %u '%s', members", q.id, q.leader, name(q.leader).c_str());
        s += b;
        for (uint8_t m : q.members)
            s += " " + std::to_string(m) + " '" + name(m) + "'";
        s += "\n";
    }
    return s;
}

} // namespace parties
