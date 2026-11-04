// Space feature "trade" (MSG_SPACE kinds 70..79): tool gifts and spice trade between players
// (docs/09-coop-design.md "Space 0.7", features 3 and 5; game knowledge: docs/re/space.md 7.7, 12).
//
// Tool gifts: the F9 Space page lists the consumable tools of the cargo hold (bombs,
// terraforming charges, colony tools, monoliths...) with "Give N to <partner>". The charges leave
// the giver's tool at once (cPlayerInventory::TakeItem, the game's own split of a tool's ammo) and
// go to the partner, who gets them on the same tool (AddAmmo, clamped at the tool's maximum) or on
// a new one (AddItemByKey, the way mission rewards come). What does not fit comes back.
//
// Spice trade: at a star where a partner has a colony (the roster of space_map.h), the page offers
// to sell the spice of the cargo hold to that colony at twice its market value. Money is one
// shared budget by default, so the galaxy pays that price (an export bonus: the seller's game adds
// it, the shared budget carries it to everybody) and the partner's colony gets the spice: it lands
// in the partner's cargo hold, to resell or use. In rivals mode (own budgets) the seller offers the
// spice at 1.5x its value and the partner pays it from their own budget after accepting on their
// page. The receiver of a gift, a sale or an offer decides (adds it or not, pays or not) and
// answers; the sender keeps what is on its way and gives it back on a "no" or when the partner
// leaves before answering.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "game.h"
#include "modes.h"
#include "net/validate.h"
#include "space_ext.h"
#include "space_map.h"
#include "sporekit_game.hpp"
#include "fixes.h"


using skg::cdecl_call;
using skg::thiscall;
using skg::vcall;

namespace spacetrade {

enum Kind : uint8_t {
    K_TOOL = 70,       // {to, id, key[3], n, name}: tool charges for `to`
    K_TOOL_ACK = 71,   // {to, id, accepted, back}: the receiver kept `accepted`, `back` returns
    K_SELL = 72,       // {to, id, planet, key[3], n, price, name}: spice sold to `to`'s colony (export bonus)
    K_SELL_ACK = 73,   // {to, id, accepted}: spice taken (the seller is paid for these), the rest returns
    K_OFFER = 74,      // rivals: {to, id, planet, key[3], n, price, name}: an offer `to` may accept
    K_OFFER_ACK = 75,  // rivals: {to, id, accepted, reason}: accepted = paid and taken, 0 = declined
    K_OFFER_CANCEL = 76, // rivals: {to, id}: the seller withdraws (the receiver answers with a decline)
};

constexpr uint32_t kToolType = 0x707459F0; // cSpaceToolData::TYPE (Cast)
constexpr uint32_t kTradingGroup = 0x034D97FA; // spacetrading~
constexpr uint32_t kPropCost = 0x034F1A4A;     // spacetrading~ value of a commodity (float)
constexpr uint32_t kTuneSpicePrice = 0x1C2E590D; // difficulty tuning: commodity price multiplier
constexpr uint32_t kMsgInventoryChanged = 0x057F3749; // posted by AddItem / TakeItem: the palettes refresh
constexpr double kSalePremium = 2.0, kOfferPremium = 1.5;
constexpr int kSaleStep = 10, kDemand = 30;   // a partner colony buys up to kDemand units ...
constexpr double kDemandWindow = 600.0;        // ... per 10 minutes
constexpr size_t kMaxOffers = 16;             // partners' offers waiting for an answer
constexpr double kOfferLife = 120.0;           // an unanswered offer is declined after 2 minutes

static const SkHost* H;
static SkMod* M;
static uintptr_t a_SpaceGame, a_AddItemByKey, a_AddAmmo, a_ConsumeAmmo, a_Trading, a_CreateTrading, a_AddMoney,
    a_PlayerEmpire, a_PropMgr, a_GetFloat, a_Tune, a_MessageMgr;

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

struct Key {
    uint32_t v[3] = {};
    bool operator==(const Key& o) const { return v[0] == o.v[0] && v[1] == o.v[1] && v[2] == o.v[2]; }
};

static void put_key(net::Writer& w, const Key& k)
{
    for (uint32_t x : k.v)
        w.u32(x);
}
static Key get_key(net::Reader& r)
{
    Key k;
    for (auto& x : k.v)
        x = r.u32();
    return k;
}

// ---------------- state ----------------
struct Out {               // something of ours on its way to a partner
    uint32_t id;
    uint8_t kind;          // K_TOOL, K_SELL, K_OFFER
    uint8_t to;
    Key key;
    int n, price;
    uint32_t planet;
    std::string name;
    double t;
    bool cancel_sent = false;
    void* emp = nullptr;   // BUG-0058: the player's cEmpire it was taken from (a load makes a new one)
    uint32_t saves = 0;    // spaceext::saves() when it was taken
    spacelogic::WorldTag tag; // BUG-0225: the Space world and save file it was taken from
    // BUG-0542: the reliable message that carried it ([rel_lo, rel_hi) on the connection it went
    // out on) and whether the partner's side acknowledged it (then it arrived: the receiver adds a
    // gift or a sale at once, only an offer waits for a click)
    bool rel_marked = false, delivered = false;
    uint16_t rel_lo = 0, rel_hi = 0;
};
// BUG-0058: what comes back while we are not in Space (an editor, the menu, a load) waits here and
// is paid once we are back in Space. Before, the refund ran at once (outside Space: "no room, lost",
// or into an inventory the mod did not know to be valid) and a session end outside Space dropped
// everything on its way. `coop.space trade refundfix 0` restores that (tests).
struct Owed {
    Out o;
    int n;
    std::string why;
};
static std::vector<Owed> g_owed;
static bool g_refund_fix = true;
COOP_FIX("trade.refund", "BUG-0058", g_refund_fix, "space: gifts that come back outside Space wait until Space");
static bool g_away;        // tests: the feature acts as outside Space (no inventory), BUG-0058
static int g_hold;         // tests: 1 do not answer gifts (the answer is still on its way); 2 add
                           // gifts and sales but do not answer (the answer is lost, BUG-0542)
// BUG-0542: the own session ends with a gift or a sale on its way: what the partner's side had
// acknowledged arrived (the receiver keeps it) and does not come back, which doubled it. Off: all
// of it comes back (before).
static bool g_delivered_fix = true;
COOP_FIX("trade.delivered", "BUG-0542", g_delivered_fix, "space: a gift the partner had received does not come back when the session ends");
static uint32_t g_kept_at_end;
static uint32_t g_owed_paid, g_owed_dropped;
static uint32_t g_owed_kept;        // BUG-0225: owed items kept for another game (as last decided)
static uint32_t g_owed_seen_epoch;  // the world the kept items were last decided in
static double g_owed_seen_t;        // and when (decided again every 2 s or on a new world / item)
static bool g_owed_new;
struct Offer {             // rivals: a partner's offer to us
    uint32_t id;
    uint8_t from;
    Key key;
    int n, price;
    uint32_t planet;
    std::string name;
    double t;
};
static std::vector<Out> g_out;
static std::vector<Offer> g_offers;
static uint32_t g_next_id;
static uint32_t g_sel_tool;        // instance of the selected tool on the page
static uint8_t g_sel_to = 0xFF;    // the partner gifts go to
static std::map<uint32_t, std::pair<double, int>> g_demand; // colony PlanetID -> (window start, units bought)
static uint32_t g_gifts_sent, g_gifts_got, g_charges_sent, g_charges_got, g_charges_back, g_sales, g_sold,
    g_bought, g_offers_sent, g_offers_got, g_refunds;
static int64_t g_earned, g_paid;

// ---------------- the game ----------------
static void* inventory()
{
    if (g_away)
        return nullptr;
    void* sg = a_SpaceGame ? *(void**)a_SpaceGame : nullptr;
    return sg ? F<void*>(sg, 0x40) : nullptr;
}

static void* empire() { return a_PlayerEmpire ? cdecl_call<void*>(a_PlayerEmpire) : nullptr; }
static int money()
{
    void* e = empire();
    return e ? F<int>(e, 0xD0) : 0;
}
static void add_money(int n)
{
    void* e = empire();
    if (e && a_AddMoney && n)
        thiscall<void>(a_AddMoney, e, n);
}

static void refresh_palettes()
{
    void* mm = a_MessageMgr ? cdecl_call<void*>(a_MessageMgr) : nullptr;
    if (mm)
        vcall<void>(mm, 0x18 / 4, kMsgInventoryChanged, (void*)nullptr, (void*)nullptr, 0); // MessagePost
}

static std::string utf8(const char16_t* s, size_t max = 80)
{
    std::string o;
    for (size_t i = 0; s && s[i] && i < max; i++) {
        uint32_t c = s[i];
        if (c >= 0xD800 && c < 0xE000)
            c = '?';
        if (c < 0x80)
            o += (char)c;
        else if (c < 0x800) {
            o += (char)(0xC0 | c >> 6);
            o += (char)(0x80 | (c & 0x3F));
        } else {
            o += (char)(0xE0 | c >> 12);
            o += (char)(0x80 | ((c >> 6) & 0x3F));
            o += (char)(0x80 | (c & 0x3F));
        }
    }
    return o;
}

// cSpaceInventoryItem (docs/re/space.md 7.7): +0x10 count, +0x14 type, vt+0x18 key, vt+0x28 name
static std::string item_name(void* it)
{
    std::string s = utf8(vcall<const char16_t*>(it, 0x28 / 4));
    if (s.empty()) {
        char b[16];
        snprintf(b, sizeof b, "%08X", vcall<const uint32_t*>(it, 0x18 / 4)[0]);
        s = b;
    }
    return s;
}

static void* as_tool(void* it)
{
    if (!it || F<int>(it, 0x14) != 2)
        return nullptr;
    return vcall<void*>((uint8_t*)it + 0x0C, 0x0C / 4, kToolType); // ISimulatorSerializable Cast
}

// cSpaceToolData: +0x7C charges, +0x1A0 max charges, +0x174 flags (0x20 uses ammo), +0x18 price
constexpr uintptr_t kAmmo = 0x7C, kMaxAmmo = 0x1A0, kFlags = 0x174;

// Ammo tools that are not for giving (docs/re/space.md 14.1): Galactic Adventures captain tools and
// potions, NPC ship weapons, and the archetype / consequence abilities (they recharge on their own
// and belong to one empire's play style). Every other ammo tool (bombs, terraforming charges,
// colony and city tools, monoliths, fireworks, tools of mods) is giftable; tools without charges
// (weapons, drives, scan, abduct, ...) are permanent unlocks and never are.
static bool gift_denied(uint32_t inst)
{
    static const uint32_t deny[] = {
        0x957260CE, // Create Creature (adventures)
        0x6A913B86, // Edit Creature (adventures)
        0xE463F2FC, 0xBDE7AB7E, // Energy Potion (adventures)
        0xF5B4B113, 0xB1C81165, // Repair Potion (adventures)
        0x1E180798, // Epicize (adventures)
        0x45C3D5E9, // Eradicate (adventures)
        0x4E71BFA6, // AOE Repair (adventures)
        0xF06A8152, // Defense Missile (NPC ships)
        0x3AAB4C0E, // Place Embassy (archetype ability)
        0x405DC1DF, // Zoo Planet (archetype ability)
        0xCACA744A, // Global Happiness (archetype ability)
        0x39D77A0C, // Planet Scan (archetype ability)
    };
    for (uint32_t d : deny)
        if (d == inst)
            return true;
    return false;
}

struct ToolInfo {
    int index;
    Key key;
    std::string name;
    int ammo, max;
    bool giftable;
};

static std::vector<ToolInfo> tools(bool all = false)
{
    std::vector<ToolInfo> v;
    void* inv = inventory();
    if (!inv)
        return v;
    int n = vcall<int>(inv, 0x5C / 4);
    for (int i = 0; i < n && i < 256; i++) {
        void* it = vcall<void*>(inv, 0x60 / 4, i);
        void* t = as_tool(it);
        if (!t)
            continue;
        ToolInfo ti;
        ti.index = i;
        memcpy(ti.key.v, vcall<const uint32_t*>(it, 0x18 / 4), 12);
        ti.ammo = F<int>(t, kAmmo);
        ti.max = F<int>(t, kMaxAmmo);
        ti.giftable = (F<uint32_t>(t, kFlags) & 0x20) && !gift_denied(ti.key.v[0]);
        if (!ti.giftable && !all)
            continue;
        if (!all && ti.ammo <= 0)
            continue;
        ti.name = item_name(it);
        v.push_back(std::move(ti));
    }
    return v;
}

static int gift_step(const ToolInfo& t) { return std::min(t.ammo, t.max >= 10 ? 5 : 1); }

// spice of the cargo hold: trading objects (type 4) whose key is one of cSpaceTrading::mSpices
// (+0x20..+0x24, ResourceKeys, filled by BuildSpiceCommodities 0x103C950)
struct SpiceInfo {
    int index;
    Key key;
    std::string name;
    int count;
};

static std::vector<Key> spice_keys()
{
    std::vector<Key> v;
    void* tr = a_Trading ? cdecl_call<void*>(a_Trading) : nullptr;
    if (!tr)
        return v;
    uint8_t* b = F<uint8_t*>(tr, 0x20);
    uint8_t* e = F<uint8_t*>(tr, 0x24);
    if (!b || e < b || e - b > 12 * 64)
        return v;
    for (uint8_t* p = b; p + 12 <= e; p += 12) {
        Key k;
        memcpy(k.v, p, 12);
        v.push_back(k);
    }
    return v;
}

static std::vector<SpiceInfo> spices()
{
    std::vector<SpiceInfo> v;
    void* inv = inventory();
    if (!inv)
        return v;
    auto keys = spice_keys();
    int n = vcall<int>(inv, 0x5C / 4);
    for (int i = 0; i < n && i < 256; i++) {
        void* it = vcall<void*>(inv, 0x60 / 4, i);
        if (!it || F<int>(it, 0x14) != 4 || F<int>(it, 0x10) <= 0)
            continue;
        Key k;
        memcpy(k.v, vcall<const uint32_t*>(it, 0x18 / 4), 12);
        bool spice = false;
        for (auto& s : keys)
            spice |= s.v[0] == k.v[0];
        if (spice)
            v.push_back({i, k, item_name(it), F<int>(it, 0x10)});
    }
    return v;
}

// the market value of one unit: spacetrading~ value * the difficulty's price multiplier; NPC stores
// pay that times 0.6..3.0 for the three spices they want and 5% for the others (docs/re/space.md 14.2)
static float spice_value(const Key& k)
{
    void* pm = a_PropMgr ? cdecl_call<void*>(a_PropMgr) : nullptr;
    if (!pm || !a_GetFloat)
        return 0;
    void* pl = nullptr;
    if (!vcall<bool>(pm, 0x2C / 4, k.v[0], k.v[2] ? k.v[2] : kTradingGroup, &pl) || !pl) // GetPropertyList
        return 0;
    float base = 0;
    bool ok = cdecl_call<bool>(a_GetFloat, pl, kPropCost, &base);
    vcall<int>(pl, 0x04 / 4); // Release
    if (!ok || !(base > 0))
        return 0;
    float mult = 1.f;
    void* nm = game::noun_manager();
    void* tune = nm ? F<void*>(nm, 0x74) : nullptr;
    if (tune && a_Tune)
        mult = thiscall<float>(a_Tune, tune, kTuneSpicePrice);
    if (!(mult > 0) || mult > 100)
        mult = 1.f;
    return base * mult;
}

static int unit_price(const Key& k) { return (int)std::lround(spice_value(k) * (spaceext::rivals() ? kOfferPremium : kSalePremium)); }

// add n spice of `key` to the cargo hold (stacks on the same spice, else needs a free slot)
static bool add_spice(const Key& key, int n)
{
    void* inv = inventory();
    void* tr = a_Trading ? cdecl_call<void*>(a_Trading) : nullptr;
    if (!inv || !tr || !a_CreateTrading || n <= 0)
        return false;
    void* item = nullptr;
    thiscall<void>(a_CreateTrading, tr, &item, key.v, n, 0.f);
    if (!item)
        return false;
    bool ok = vcall<bool>(inv, 0x80 / 4, item, (void*)nullptr, true);
    vcall<void>(item, 0x08 / 4); // Release
    return ok;
}

// take n spice of `key` out of the cargo hold, false if there is not that much
static bool take_spice(const Key& key, int n)
{
    void* inv = inventory();
    if (!inv || n <= 0)
        return false;
    int i = vcall<int>(inv, 0x64 / 4, 4, key.v); // IndexOf(type, key)
    if (i < 0)
        return false;
    void* it = vcall<void*>(inv, 0x60 / 4, i);
    int have = it ? F<int>(it, 0x10) : 0;
    if (have < n)
        return false;
    void* out = nullptr;
    if (!vcall<bool>(inv, 0x7C / 4, i, &out, n == have ? -1 : n)) // TakeItem
        return false;
    if (out)
        vcall<void>(out, 0x08 / 4);
    return true;
}

// take n charges of the tool `key` (the game's own split, TakeItem with a count: ConsumeAmmo)
static bool take_charges(const Key& key, int n)
{
    void* inv = inventory();
    if (!inv || n <= 0)
        return false;
    int i = vcall<int>(inv, 0x64 / 4, 2, key.v);
    void* t = i >= 0 ? as_tool(vcall<void*>(inv, 0x60 / 4, i)) : nullptr;
    if (!t || !(F<uint32_t>(t, kFlags) & 0x20) || F<int>(t, kAmmo) < n)
        return false;
    void* out = nullptr;
    if (!vcall<bool>(inv, 0x7C / 4, i, &out, n))
        return false;
    if (out)
        vcall<void>(out, 0x08 / 4);
    return true;
}

static void set_charges(void* t, int want)
{
    int cur = F<int>(t, kAmmo);
    if (cur > want && a_ConsumeAmmo)
        thiscall<void>(a_ConsumeAmmo, t, cur - want);
    else if (cur < want && a_AddAmmo)
        thiscall<void>(a_AddAmmo, t, want - cur);
}

// add n charges of the tool `key`: onto the same tool (clamped at its maximum) or a new tool
// (AddItemByKey: as a mission reward, with the "new tool" event; then exactly n charges). Returns
// how many were kept; a tool the game refuses (an upgrade of a tool we lack) keeps none.
static int add_charges(const Key& key, int n, bool& is_new)
{
    is_new = false;
    void* inv = inventory();
    if (!inv || n <= 0 || !a_AddAmmo)
        return 0;
    void* t = as_tool(vcall<void*>(inv, 0x68 / 4, key.v)); // GetTool
    if (t) {
        if (!(F<uint32_t>(t, kFlags) & 0x20))
            return 0;
        int before = F<int>(t, kAmmo);
        thiscall<void>(a_AddAmmo, t, n);
        refresh_palettes();
        return std::max(0, F<int>(t, kAmmo) - before);
    }
    if (!a_AddItemByKey)
        return 0;
    thiscall<void>(a_AddItemByKey, inv, key.v);
    t = as_tool(vcall<void*>(inv, 0x68 / 4, key.v));
    if (!t)
        return 0;
    is_new = true;
    set_charges(t, std::min(n, std::max(1, F<int>(t, kMaxAmmo))));
    refresh_palettes();
    return F<int>(t, kAmmo);
}

static bool partner_here(uint8_t id)
{
    if (!spaceext::connected())
        return false;
    for (auto& p : spaceext::ctx().sess->players())
        if (p.id == id)
            return true;
    return false;
}

static uint32_t star_of(uint32_t place)
{
    if (place & 0x80000000u)
        return place & 0x7FFFFFFFu;
    if (place & 0x40000000u)
        return place & 0xFFFFFFu;
    return 0;
}

static uint32_t my_star()
{
    auto& c = spaceext::ctx();
    return star_of(c.my_place ? c.my_place() : 0);
}

// ---------------- partner colonies (the roster of space_map.h) ----------------
struct Col {
    uint8_t owner;
    uint32_t star, planet;
    std::string name;
    uint32_t spice; // instance of the colony's own spice, 0 = none
};

static std::vector<Col> colonies_at(uint32_t star)
{
    std::vector<Col> v;
    if (!star)
        return v;
    uint8_t me = spaceext::my_id();
    for (uint8_t id : spacemap::roster_ids()) {
        const spacemap::Roster* r = id == me ? nullptr : spacemap::roster_of(id);
        if (!r)
            continue;
        for (auto& c : r->colonies)
            if (c.star == star)
                v.push_back({id, c.star, c.planet, c.name.empty() ? spacemap::star_name(c.star) : c.name, c.spice[0]});
    }
    return v;
}

static int demand_left(uint32_t planet, double now)
{
    auto it = g_demand.find(planet);
    if (it == g_demand.end() || now - it->second.first > kDemandWindow)
        return kDemand;
    return std::max(0, kDemand - it->second.second);
}

static void note_demand(uint32_t planet, int n, double now)
{
    auto& d = g_demand[planet];
    if (now - d.first > kDemandWindow)
        d = {now, 0};
    d.second += n;
}

struct Deal {
    Col col;
    SpiceInfo spice;
    int price, n;
};

// the best sale here: the partner colony and the cargo spice with the highest price (a colony does
// not buy the spice it makes itself)
static bool best_deal(Deal& d, std::string* why = nullptr)
{
    auto cols = colonies_at(my_star());
    if (cols.empty())
        return false;
    auto sp = spices();
    double now = H->time_seconds();
    bool found = false;
    for (auto& c : cols) {
        int left = spaceext::rivals() ? kSaleStep : demand_left(c.planet, now);
        if (left <= 0) {
            if (why)
                *why = spaceext::name_of(c.owner) + spaceext::tr("'s colony has all the spice it needs for now",
                                                                 ": колонии пока хватает специй");
            continue;
        }
        for (auto& s : sp) {
            if (s.key.v[0] == c.spice)
                continue;
            int p = unit_price(s.key);
            if (p <= 0 || (found && p <= d.price))
                continue;
            d = {c, s, p, std::min({kSaleStep, s.count, left})};
            found = true;
        }
    }
    if (!found && why && why->empty() && !sp.empty())
        *why = spaceext::tr("the partner colonies here make the spice you carry",
                            "колонии партнёров здесь сами делают ваши специи");
    return found;
}

// ---------------- actions ----------------
// the connection a message of ours to `to` goes out on (a guest sends everything to the host)
static net::Connection* conn_for(uint8_t to)
{
    if (!spaceext::connected())
        return nullptr;
    return spaceext::ctx().sess->conn_to(spaceext::is_host() ? to : 0);
}

// send `o`'s message and mark it (BUG-0542), then keep it until the answer
static void send_out(Out& o, const net::Writer& w)
{
    net::Connection* c = conn_for(o.to);
    uint16_t lo = c ? c->next_rel_id() : 0;
    spaceext::send(o.kind, w);
    if (c && c == conn_for(o.to) && c->next_rel_id() != lo) {
        o.rel_marked = true;
        o.rel_lo = lo;
        o.rel_hi = c->next_rel_id();
    }
    g_out.push_back(o);
}

static uint32_t new_id() { return (uint32_t)spaceext::my_id() << 24 | (++g_next_id & 0xFFFFFF); }

static std::string give_tool(const Key& key, int n, uint8_t to)
{
    if (!spaceext::in_space() || !spaceext::connected())
        return "not in space";
    if (!partner_here(to))
        return "no such partner";
    std::string nm;
    for (auto& t : tools())
        if (t.key == key)
            nm = t.name;
    if (nm.empty())
        return "no such tool to give";
    if (!take_charges(key, n))
        return "not enough charges";
    Out o{new_id(), K_TOOL, to, key, n, 0, 0, nm, H->time_seconds()};
    o.emp = empire();
    o.saves = spaceext::saves();
    o.tag = spaceext::world_tag();
    net::Writer w;
    w.u8(to);
    w.u32(o.id);
    put_key(w, key);
    w.u32((uint32_t)n);
    w.str8(nm);
    send_out(o, w);
    g_gifts_sent++;
    g_charges_sent += n;
    spaceext::notice(std::to_string(n) + " x " + nm + spaceext::tr(" sent to ", " отправлено: ") + spaceext::name_of(to));
    return "";
}

static std::string sell(int n_max = kSaleStep)
{
    if (!spaceext::in_space() || !spaceext::connected())
        return "not in space";
    Deal d;
    std::string why;
    if (!best_deal(d, &why))
        return why.empty() ? "no partner colony here buys spice" : why;
    int n = std::min(d.n, n_max);
    if (!take_spice(d.spice.key, n))
        return "the spice is gone";
    bool rivals = spaceext::rivals();
    Out o{new_id(), rivals ? K_OFFER : K_SELL, d.col.owner, d.spice.key, n, d.price, d.col.planet, d.spice.name,
          H->time_seconds()};
    o.emp = empire();
    o.saves = spaceext::saves();
    o.tag = spaceext::world_tag();
    if (!rivals)
        note_demand(d.col.planet, n, o.t);
    net::Writer w;
    w.u8(o.to);
    w.u32(o.id);
    w.u32(o.planet);
    put_key(w, o.key);
    w.u32((uint32_t)n);
    w.u32((uint32_t)o.price);
    w.str8(o.name);
    send_out(o, w);
    if (rivals) {
        g_offers_sent++;
        spaceext::notice(spaceext::tr("Offer sent to ", "Предложение отправлено: ") + spaceext::name_of(o.to) + ": " +
                         std::to_string(n) + " " + o.name + " x $" + std::to_string(o.price));
    }
    return "";
}

static bool here() { return !g_away && spaceext::in_space() && inventory() && empire(); }

static void pay_now(const Out& o, int n, const std::string& why);
static void refund(const Out& o, int n, const std::string& why)
{
    if (n <= 0)
        return;
    if (g_refund_fix && !here()) { // not in Space now: owed until we are back
        g_owed.push_back({o, n, why});
        g_owed_new = true;
        return;
    }
    // BUG-0225: in Space, but maybe another Space world than the one it was taken from (a load came
    // between): pay_owed decides with the world tag
    if (g_refund_fix && spaceext::owed_id() && o.tag.epoch != spaceext::world_epoch()) {
        g_owed.push_back({o, n, why});
        g_owed_new = true;
        return;
    }
    pay_now(o, n, why);
}
static void pay_now(const Out& o, int n, const std::string& why)
{
    bool ok = o.kind == K_TOOL ? [&] { bool is_new; return add_charges(o.key, n, is_new) > 0; }() : add_spice(o.key, n);
    g_refunds++;
    spaceext::notice(std::to_string(n) + " x " + o.name + spaceext::tr(" came back", " вернулось") +
                     (why.empty() ? "" : " (" + why + ")") +
                     (ok ? "" : spaceext::tr(": no room, lost", ": нет места, потеряно")));
}

static void answer_offer(uint32_t id, bool accept, int from = -1)
{
    for (size_t i = 0; i < g_offers.size(); i++) {
        if (g_offers[i].id != id || (from >= 0 && g_offers[i].from != from))
            continue;
        Offer of = g_offers[i];
        g_offers.erase(g_offers.begin() + i);
        uint8_t reason = 0; // 0 declined, 1 no money, 2 no cargo room, 3 not in space
        int got = 0;
        int total = 0;
        if (!net::money_total(of.n, of.price, total))
            accept = false; // (K_OFFER keeps the sum in range)
        if (accept) {
            if (!spaceext::in_space() || !inventory())
                reason = 3;
            else if (money() < total)
                reason = 1;
            else if (!add_spice(of.key, of.n))
                reason = 2;
            else {
                got = of.n;
                add_money(-total);
                g_bought += got;
                g_paid += total;
            }
            const char* en[] = {"", "not enough money", "no free cargo slot", "not in space"};
            const char* ru[] = {"", "не хватает денег", "нет места в трюме", "вы не в космосе"};
            spaceext::notice(got ? spaceext::tr("Bought ", "Куплено: ") + std::to_string(got) + " " + of.name +
                                       spaceext::tr(" for $", " за $") + std::to_string(total)
                                 : spaceext::tr("Can't buy: ", "Не купить: ") + std::string(spaceext::tr(en[reason], ru[reason])));
        }
        net::Writer w;
        w.u8(of.from);
        w.u32(of.id);
        w.u32((uint32_t)got);
        w.u8(reason);
        spaceext::send(K_OFFER_ACK, w);
        return;
    }
}

// ---------------- messages ----------------
static Out* pending(uint32_t id, uint8_t kind, uint8_t to)
{
    for (auto& o : g_out)
        if (o.id == id && o.kind == kind && o.to == to)
            return &o;
    return nullptr;
}
static void drop(uint32_t id)
{
    g_out.erase(std::remove_if(g_out.begin(), g_out.end(), [id](const Out& o) { return o.id == id; }), g_out.end());
}

// back in Space: pay what is owed, once. Taken from this very game (the same cEmpire) or saved
// since it was taken: it comes back. Else a load brought back a game from before it was taken,
// which still has it: nothing to pay (it would be doubled).
static void pay_owed()
{
    if (g_owed.empty() || !here())
        return;
    if (spaceext::owed_id()) { // BUG-0225: kept items wait for their game; decide again only when needed
        double now = H->time_seconds();
        if (!g_owed_new && g_owed_seen_epoch == spaceext::world_epoch() && now - g_owed_seen_t < 2.0)
            return;
        g_owed_new = false;
        g_owed_seen_epoch = spaceext::world_epoch();
        g_owed_seen_t = now;
        g_owed_kept = 0;
    }
    auto owed = std::move(g_owed);
    g_owed.clear();
    void* emp = empire();
    for (auto& w : owed) {
        if (spaceext::owed_id()) { // BUG-0225: by the world and its save file
            switch (spaceext::owed_action(w.o.tag)) {
            case spacelogic::OwedAction::Pay:
                g_owed_paid++;
                pay_now(w.o, w.n, w.why);
                break;
            case spacelogic::OwedAction::Drop:
                g_owed_dropped++;
                H->log(M, SK_INFO, "trade: %d x %s owed from world %u (%s) dropped: that game still has it or is gone",
                       w.n, w.o.name.c_str(), w.o.tag.epoch, w.o.tag.game.c_str());
                break;
            case spacelogic::OwedAction::Keep:
                g_owed_kept++;
                g_owed.push_back(std::move(w)); // another game: paid when that one is loaded again
                break;
            }
            continue;
        }
        if (w.o.emp == emp || spaceext::saves() != w.o.saves) {
            g_owed_paid++;
            refund(w.o, w.n, w.why);
        } else
            g_owed_dropped++;
    }
    while (g_owed.size() > spacelogic::kOwedKeepMax) {
        g_owed_dropped++;
        g_owed.erase(g_owed.begin());
    }
}

static void on_message(uint8_t kind, uint8_t origin, net::Reader& r)
{
    uint8_t to = r.u8();
    uint32_t id = r.u32();
    if (!r.ok() || to != spaceext::my_id())
        return;
    std::string from = spaceext::name_of(origin);
    switch (kind) {
    case K_TOOL: {
        Key k = get_key(r);
        int n = (int)r.u32();
        std::string nm = r.str8();
        if (!r.ok() || n <= 0 || n > 999)
            return;
        if (g_hold == 1)
            return; // tests: the answer is still on its way
        bool is_new = false;
        int got = spaceext::in_space() ? add_charges(k, n, is_new) : 0;
        g_gifts_got++;
        g_charges_got += got;
        if (g_hold == 2)
            return; // tests: kept, the answer is lost
        net::Writer w;
        w.u8(origin);
        w.u32(id);
        w.u32((uint32_t)got);
        w.u32((uint32_t)(n - got));
        spaceext::send(K_TOOL_ACK, w);
        if (got)
            spaceext::notice(from + spaceext::tr(" gave you ", " дарит вам ") + std::to_string(got) + " x " + nm +
                             (got < n ? spaceext::tr(" (the rest did not fit)", " (остальное не поместилось)") : ""));
        else
            spaceext::notice(from + spaceext::tr("'s gift (", ": подарок (") + nm +
                             spaceext::tr(") did not fit: it goes back", ") не поместился и вернётся"));
        break;
    }
    case K_TOOL_ACK: {
        int got = (int)r.u32(), back = (int)r.u32();
        Out* o = pending(id, K_TOOL, origin);
        if (!r.ok() || !o)
            return;
        Out copy = *o;
        drop(id);
        if (back > 0) {
            g_charges_back += back;
            refund(copy, std::min(back, copy.n), from + spaceext::tr(" had no room", ": нет места"));
        }
        if (got > 0)
            spaceext::notice(from + spaceext::tr(" got ", " получил(а) ") + std::to_string(got) + " x " + copy.name);
        break;
    }
    case K_SELL: {
        uint32_t planet = r.u32();
        Key k = get_key(r);
        int n = (int)r.u32(), price = (int)r.u32();
        std::string nm = r.str8();
        if (!r.ok() || n <= 0 || n > 999)
            return;
        (void)planet;
        int got = spaceext::in_space() && add_spice(k, n) ? n : 0;
        g_bought += got;
        if (g_hold == 2)
            return; // tests: kept, the answer is lost
        net::Writer w;
        w.u8(origin);
        w.u32(id);
        w.u32((uint32_t)got);
        spaceext::send(K_SELL_ACK, w);
        spaceext::notice(got ? from + spaceext::tr(" sold ", " продал(а) вашей колонии ") + std::to_string(n) + " " + nm +
                                   spaceext::tr(" to your colony: it is in your cargo hold (the galaxy paid $",
                                                ": они в вашем трюме (галактика заплатила $") +
                                   std::to_string((long long)n * price) + ")"
                             : from + spaceext::tr(" wanted to sell you spice: no free cargo slot",
                                                   " хотел(а) продать вам специи: в трюме нет места"));
        break;
    }
    case K_SELL_ACK: {
        int got = (int)r.u32();
        Out* o = pending(id, K_SELL, origin);
        if (!r.ok() || !o)
            return;
        Out copy = *o;
        drop(id);
        got = std::clamp(got, 0, copy.n);
        if (got > 0) {
            int total = got * copy.price;
            add_money(total); // the export bonus: shared money carries it to everybody
            g_sales++;
            g_sold += got;
            g_earned += total;
            spaceext::notice(spaceext::tr("Sold ", "Продано: ") + std::to_string(got) + " " + copy.name + spaceext::tr(" to ", ", ") +
                             from + spaceext::tr("'s colony: +$", ": +$") + std::to_string(total) +
                             spaceext::tr(" (twice the market value)", " (вдвое дороже рынка)"));
        }
        if (got < copy.n) {
            auto it = g_demand.find(copy.planet);
            if (it != g_demand.end())
                it->second.second = std::max(0, it->second.second - (copy.n - got));
            refund(copy, copy.n - got, from + spaceext::tr(" had no free cargo slot", ": нет места в трюме"));
        }
        break;
    }
    case K_OFFER: {
        uint32_t planet = r.u32();
        Key k = get_key(r);
        int n = (int)r.u32(), price = (int)r.u32();
        std::string nm = r.str8();
        int sum = 0;
        if (!r.ok() || n <= 0 || n > 999 || !net::money_total(n, price, sum))
            return;
        for (auto& of : g_offers)
            if (of.id == id && of.from == origin)
                return; // this offer again
        if (g_offers.size() >= kMaxOffers)
            return; // the page shows a few; an answer frees a place
        g_offers.push_back({id, origin, k, n, price, planet, nm, H->time_seconds()});
        g_offers_got++;
        spaceext::notice(from + spaceext::tr(" offers ", " предлагает ") + std::to_string(n) + " " + nm + spaceext::tr(" for $", " за $") +
                         std::to_string(sum) + spaceext::tr(": accept on the Space page (F9)", ": принять на странице Космос (F9)"));
        break;
    }
    case K_OFFER_ACK: {
        int got = (int)r.u32();
        uint8_t reason = r.u8();
        Out* o = pending(id, K_OFFER, origin);
        if (!r.ok() || !o)
            return;
        Out copy = *o;
        drop(id);
        got = std::clamp(got, 0, copy.n);
        if (got > 0) {
            int total = got * copy.price;
            add_money(total); // rivals: the partner paid this from its own budget
            g_sales++;
            g_sold += got;
            g_earned += total;
            spaceext::notice(from + spaceext::tr(" bought ", " купил(а) ") + std::to_string(got) + " " + copy.name + ": +$" +
                             std::to_string(total));
        }
        const char* en[] = {"declined", "not enough money", "no free cargo slot", "not in space"};
        const char* ru[] = {"отказ", "не хватает денег", "нет места в трюме", "не в космосе"};
        if (got < copy.n)
            refund(copy, copy.n - got, from + ": " + spaceext::tr(en[std::min<int>(reason, 3)], ru[std::min<int>(reason, 3)]));
        break;
    }
    case K_OFFER_CANCEL:
        answer_offer(id, false, origin);
        break;
    default:
        break;
    }
}

// ---------------- frame ----------------
static void tick(double now)
{
    if (g_refund_fix)
        pay_owed(); // also after the session ended
    if (!spaceext::connected())
        return;
    for (auto& o : g_out) // BUG-0542: did the partner's side get it?
        if (o.rel_marked && !o.delivered) {
            net::Connection* c = conn_for(o.to);
            o.delivered = c && c->rel_acked(o.rel_lo, o.rel_hi);
        }
    // a partner who left before answering: what was on its way comes back
    for (size_t i = 0; i < g_out.size();) {
        if (partner_here(g_out[i].to)) {
            i++;
            continue;
        }
        Out o = g_out[i];
        g_out.erase(g_out.begin() + i);
        refund(o, o.n, spaceext::name_of(o.to) + spaceext::tr(" left", " вышел(а)"));
    }
    for (size_t i = 0; i < g_offers.size();) {
        if (!partner_here(g_offers[i].from)) {
            g_offers.erase(g_offers.begin() + i);
            continue;
        }
        // unanswered for too long, or the host ended rivals mode (one budget again: nothing to pay)
        if (now - g_offers[i].t > kOfferLife || !spaceext::rivals()) {
            answer_offer(g_offers[i].id, false); // erases it
            continue;
        }
        i++;
    }
}

static void on_session_end()
{
    // nobody can answer any more: what was on its way comes back (it may have arrived: we can't know)
    auto out = g_out;
    g_out.clear();
    for (auto& o : out) {
        // BUG-0542: the partner's side had it: a gift or a sale was kept there at once (what did not
        // fit would have come back with the answer that is now lost); only an offer waits for a click
        if (g_delivered_fix && o.delivered && o.kind != K_OFFER) {
            g_kept_at_end++;
            H->log(M, SK_INFO, "trade: %d x %s reached %s before the session ended: not given back", o.n, o.name.c_str(),
                   spaceext::name_of(o.to).c_str());
            if (o.kind == K_SELL && here()) { // the sale went through: the export bonus as with the answer
                int total = o.n * o.price;
                add_money(total);
                g_sales++;
                g_sold += o.n;
                g_earned += total;
            }
            continue;
        }
        if (g_refund_fix || spaceext::in_space())
            refund(o, o.n, ""); // outside Space: owed until we are back
    }
    g_offers.clear();
    g_sel_to = 0xFF;
}

// ---------------- the Space page ----------------
static std::vector<spacecoop::Partner> gift_partners()
{
    auto& c = spaceext::ctx();
    return c.partners ? c.partners() : std::vector<spacecoop::Partner>{};
}

static std::vector<spaceext::Row> rows()
{
    std::vector<spaceext::Row> v;
    using spaceext::tr;
    if (!spaceext::in_space() || !spaceext::connected() || !inventory())
        return v;
    // rivals: offers to us first (they wait for an answer)
    for (auto& of : g_offers) {
        spaceext::Row r;
        r.text = spaceext::name_of(of.from) + tr(" offers ", " предлагает ") + std::to_string(of.n) + " " + of.name + " $" +
                 std::to_string(of.n * of.price);
        r.color = 0xFFFFD040;
        r.button = tr("Accept", "Принять");
        uint32_t id = of.id;
        r.on_click = [id] { answer_offer(id, true); };
        v.push_back(std::move(r));
        spaceext::Row d;
        d.text = tr("  (declined by itself after 2 minutes)", "  (через 2 минуты отклонится само)");
        d.button = tr("Decline", "Отказаться");
        d.on_click = [id] { answer_offer(id, false); };
        v.push_back(std::move(d));
        break; // one at a time
    }
    // our offers waiting for an answer
    for (auto& o : g_out)
        if (o.kind == K_OFFER) {
            spaceext::Row r;
            r.text = tr("Offer to ", "Предложение: ") + spaceext::name_of(o.to) + ": " + std::to_string(o.n) + " " + o.name +
                     tr(", waiting", ", ждём ответа");
            if (!o.cancel_sent) {
                r.button = tr("Withdraw", "Отозвать");
                uint32_t id = o.id;
                uint8_t to = o.to;
                r.on_click = [id, to] {
                    for (auto& x : g_out)
                        if (x.id == id)
                            x.cancel_sent = true;
                    net::Writer w;
                    w.u8(to);
                    w.u32(id);
                    spaceext::send(K_OFFER_CANCEL, w);
                };
            }
            v.push_back(std::move(r));
            break;
        }
    // tool gifts
    auto ps = gift_partners();
    if (!ps.empty()) {
        auto ts = tools();
        size_t pi = 0;
        for (size_t i = 0; i < ps.size(); i++)
            if (ps[i].id == g_sel_to)
                pi = i;
        auto& to = ps[pi];
        if (!ts.empty()) {
            size_t ti = 0;
            for (size_t i = 0; i < ts.size(); i++)
                if (ts[i].key.v[0] == g_sel_tool)
                    ti = i;
            auto& t = ts[ti];
            int n = gift_step(t);
            spaceext::Row r;
            // (the button is narrow: the partner's name goes into the text)
            r.text = t.name + " x" + std::to_string(t.ammo) +
                     (ts.size() > 1 ? " (" + std::to_string(ti + 1) + "/" + std::to_string(ts.size()) + ")" : "") +
                     tr(", for ", ", для: ") + to.name;
            r.button = tr("Give ", "Дать ") + std::to_string(n);
            Key k = t.key;
            uint8_t id = to.id;
            r.on_click = [k, n, id] {
                std::string e = give_tool(k, n, id);
                if (!e.empty())
                    spaceext::notice(spaceext::tr("Can't give: ", "Не отдать: ") + e);
            };
            v.push_back(std::move(r));
            if (ts.size() > 1) {
                spaceext::Row nx;
                nx.text = tr("  more tools to give", "  ещё инструменты для подарка");
                nx.button = tr("Next tool", "Следующий");
                uint32_t next = ts[(ti + 1) % ts.size()].key.v[0];
                nx.on_click = [next] { g_sel_tool = next; };
                v.push_back(std::move(nx));
            }
        }
        if (ps.size() > 1) {
            spaceext::Row r;
            r.text = tr("Gifts go to ", "Подарки получает: ") + to.name;
            r.button = tr("Other partner", "Другой партнёр");
            uint8_t next = ps[(pi + 1) % ps.size()].id;
            r.on_click = [next] { g_sel_to = next; };
            v.push_back(std::move(r));
        }
    }
    // spice trade with a partner colony here
    Deal d;
    std::string why;
    if (best_deal(d, &why)) {
        spaceext::Row r;
        bool rivals = spaceext::rivals();
        r.text = spaceext::name_of(d.col.owner) + tr("'s ", ": ") + (d.col.name.empty() ? tr("colony", "колония") : d.col.name) +
                 tr(" buys ", " купит ") + d.spice.name + " $" + std::to_string(d.price) +
                 (rivals ? tr(" (1.5x market)", " (1,5x рынка)") : tr(" (2x market)", " (2x рынка)"));
        r.color = 0xFF80FF80;
        r.button = (rivals ? tr("Offer ", "Предложить ") : tr("Sell ", "Продать ")) + std::to_string(d.n);
        r.on_click = [] {
            std::string e = sell();
            if (!e.empty())
                spaceext::notice(spaceext::tr("Can't sell: ", "Не продать: ") + e);
        };
        v.push_back(std::move(r));
    } else if (!why.empty()) {
        spaceext::Row r;
        r.text = why;
        v.push_back(std::move(r));
    }
    return v;
}

static std::string status()
{
    char b[560];
    snprintf(b, sizeof b,
             "trade: gifts_sent=%u gifts_got=%u charges_sent=%u charges_got=%u charges_back=%u sales=%u sold=%u "
             "bought=%u earned=%lld paid=%lld offers_sent=%u offers_got=%u refunds=%u pending=%u offers=%u owed=%u "
             "owed_paid=%u owed_dropped=%u owed_kept=%u world=%u delivered=%u kept_at_end=%u\n",
             g_gifts_sent, g_gifts_got, g_charges_sent, g_charges_got, g_charges_back, g_sales, g_sold, g_bought,
             (long long)g_earned, (long long)g_paid, g_offers_sent, g_offers_got, g_refunds, (unsigned)g_out.size(),
             (unsigned)g_offers.size(), (unsigned)g_owed.size(), g_owed_paid, g_owed_dropped, g_owed_kept, spaceext::world_epoch(),
             (unsigned)std::count_if(g_out.begin(), g_out.end(), [](const Out& o) { return o.delivered; }), g_kept_at_end);
    return b;
}

static Key tool_key(const std::string& s)
{
    Key k;
    k.v[0] = (uint32_t)strtoul(s.c_str(), nullptr, 16);
    return k;
}

// coop.space trade [tools | all | give <inst> <n> [to] | ammo <inst> <n> | spices | spice <i> <n> |
//                   take <inst> <n> | sell [n] | deal | offers | accept <id> | decline <id> | money]
static std::string debug(const std::vector<std::string>& a)
{
    if (a.empty())
        return status();
    char b[300];
    std::string s;
    if (a[0] == "tools" || a[0] == "all") {
        for (auto& t : tools(a[0] == "all")) {
            snprintf(b, sizeof b, "tool %08X %08X %08X '%s' ammo=%d max=%d giftable=%d\n", t.key.v[0], t.key.v[1], t.key.v[2],
                     t.name.c_str(), t.ammo, t.max, (int)t.giftable);
            s += b;
        }
        return s.empty() ? "no tools\n" : s;
    }
    if (a[0] == "give" && a.size() > 2) {
        uint8_t to = a.size() > 3 ? (uint8_t)atoi(a[3].c_str()) : 0xFF;
        if (to == 0xFF)
            for (auto& p : gift_partners())
                to = p.id;
        std::string e = give_tool(tool_key(a[1]), atoi(a[2].c_str()), to);
        return e.empty() ? "given\n" : e + "\n";
    }
    if (a[0] == "ammo" && a.size() > 2) { // tests: set the charges of an own tool
        void* inv = inventory();
        Key k = tool_key(a[1]);
        void* t = inv ? as_tool(vcall<void*>(inv, 0x68 / 4, k.v)) : nullptr;
        if (!t)
            return "no such tool\n";
        set_charges(t, atoi(a[2].c_str()));
        refresh_palettes();
        snprintf(b, sizeof b, "ammo %d max %d\n", F<int>(t, kAmmo), F<int>(t, kMaxAmmo));
        return b;
    }
    if (a[0] == "spices") {
        auto keys = spice_keys();
        for (size_t i = 0; i < keys.size(); i++) {
            snprintf(b, sizeof b, "spice %zu %08X %08X %08X value=%.1f price=%d\n", i, keys[i].v[0], keys[i].v[1], keys[i].v[2],
                     spice_value(keys[i]), unit_price(keys[i]));
            s += b;
        }
        for (auto& c : spices()) {
            snprintf(b, sizeof b, "cargo %08X '%s' x%d\n", c.key.v[0], c.name.c_str(), c.count);
            s += b;
        }
        snprintf(b, sizeof b, "free cargo slots %d\n", inventory() ? vcall<int>(inventory(), 0x8C / 4) : -1);
        return s + b;
    }
    if (a[0] == "spice" && a.size() > 2) { // tests: n spice of the i-th spice commodity into the cargo
        auto keys = spice_keys();
        size_t i = (size_t)atoi(a[1].c_str());
        if (i >= keys.size())
            return "no such spice\n";
        return add_spice(keys[i], atoi(a[2].c_str())) ? "added\n" : "not added (no free cargo slot?)\n";
    }
    if (a[0] == "take" && a.size() > 2) { // tests: remove n spice of instance <hex>
        Key k;
        for (auto& c : spices())
            if (c.key.v[0] == tool_key(a[1]).v[0])
                k = c.key;
        return take_spice(k, atoi(a[2].c_str())) ? "taken\n" : "not taken\n";
    }
    if (a[0] == "deal") {
        Deal d;
        std::string why;
        if (!best_deal(d, &why))
            return "no deal: " + why + "\n";
        snprintf(b, sizeof b, "deal %s planet %08X spice %08X '%s' x%d price %d\n", spaceext::name_of(d.col.owner).c_str(),
                 d.col.planet, d.spice.key.v[0], d.spice.name.c_str(), d.n, d.price);
        return b;
    }
    if (a[0] == "sell") {
        std::string e = sell(a.size() > 1 ? atoi(a[1].c_str()) : kSaleStep);
        return e.empty() ? "sent\n" : e + "\n";
    }
    if (a[0] == "offers") {
        for (auto& o : g_offers) {
            snprintf(b, sizeof b, "offer %08X from %u %s x%d price %d\n", o.id, o.from, o.name.c_str(), o.n, o.price);
            s += b;
        }
        return s.empty() ? "no offers\n" : s;
    }
    if ((a[0] == "accept" || a[0] == "decline") && a.size() > 1) {
        answer_offer((uint32_t)strtoul(a[1].c_str(), nullptr, 16), a[0] == "accept");
        return "answered\n";
    }
    if ((a[0] == "refundfix" || a[0] == "away" || a[0] == "hold") && a.size() > 1) {
        // refundfix 0: refunds at once, wherever we are (before BUG-0058); away 1: act as outside
        // Space (no inventory); hold 1: leave gifts unanswered (tests)
        bool on = a[1] != "0";
        if (a[0] == "hold") { // 2: add, but do not answer (BUG-0542)
            g_hold = std::clamp(atoi(a[1].c_str()), 0, 2);
            return "hold " + std::to_string(g_hold) + "\n";
        }
        (a[0] == "refundfix" ? g_refund_fix : g_away) = on;
        return a[0] + (on ? " 1\n" : " 0\n");
    }
    if (a[0] == "money") {
        snprintf(b, sizeof b, "money %d\n", money());
        return b;
    }
    return "usage: coop.space trade [tools | all | give <inst> <n> [to] | ammo <inst> <n> | spices | spice <i> <n> | "
           "take <inst> <n> | deal | sell [n] | offers | accept <id> | decline <id> | money | refundfix 0|1 | "
           "away 0|1 | hold 0|1|2]\n";
}

static bool init(const SkHost* h, SkMod* m)
{
    H = h;
    M = m;
    auto S = [](const char* n) { return H->sym(n); };
    a_SpaceGame = S("Simulator::cSimulatorSpaceGame::ptr");
    a_AddItemByKey = S("Simulator::cPlayerInventory::AddItemByKey");
    a_AddAmmo = S("Simulator::cSpaceToolData::AddAmmo");
    a_ConsumeAmmo = S("Simulator::cSpaceToolData::ConsumeAmmo");
    a_Trading = S("Simulator::cSpaceTrading::Get");
    a_CreateTrading = S("Simulator::cSpaceTrading::CreateTradingObject");
    a_AddMoney = S("Simulator::cEmpire::AddMoney");
    a_PlayerEmpire = S("Simulator::GetPlayerEmpire");
    a_PropMgr = S("App::IPropManager::Get");
    a_GetFloat = S("App::Property::GetFloat");
    a_Tune = S("SporeCoop::DifficultyTuning_GetFloat");
    a_MessageMgr = S("App::IMessageManager::Get");
    bool ok = a_SpaceGame && a_AddItemByKey && a_AddAmmo && a_ConsumeAmmo && a_Trading && a_CreateTrading && a_AddMoney &&
              a_PlayerEmpire;
    H->log(M, ok ? SK_INFO : SK_WARN, "space trade: gifts %s, spice prices %s", ok ? "on" : "off",
           a_PropMgr && a_GetFloat ? (a_Tune ? "on" : "on (no difficulty multiplier)") : "off");
    return ok;
}

static bool reg = spaceext::add({.name = "trade",
                                 .kind_lo = 70,
                                 .kind_hi = 79,
                                 .init = init,
                                 .tick = tick,
                                 .on_message = on_message,
                                 .on_session_end = on_session_end,
                                 .rows = rows,
                                 .status = status,
                                 .debug = debug});

} // namespace spacetrade
