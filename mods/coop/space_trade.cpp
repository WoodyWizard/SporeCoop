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
#include <windows.h>
#include <shlobj.h>

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
#include "net/protocol.h"
#include "net/validate.h"
#include "space_ext.h"
#include "space_map.h"
#include "sporekit_game.hpp"
#include "fixes.h"
#include "own_world.h"


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
    // BUG-0678: a guest's own inventory across a rejoin or a reload (see "own inventory" below)
    K_INV = 77,        // guest -> host {to 0, serial, fresh u8, inventory}: what my cargo hold has now
    K_INV_KEPT = 78,   // host -> guest {to, serial, inventory}: your own inventory, kept since you were here
    K_INV_DONE = 79,   // guest -> host {to 0, serial, restored u16, failed u16}: applied (or not needed)
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
static bool inv_restore_pending();
static void pay_owed()
{
    if (g_owed.empty() || !here() || inv_restore_pending()) // BUG-0678: not into a hold about to be replaced
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

static void inv_message(uint8_t kind, uint8_t origin, uint32_t id, net::Reader& r);
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
    case K_INV:
    case K_INV_KEPT:
    case K_INV_DONE:
        inv_message(kind, origin, id, r);
        break;
    default:
        break;
    }
}

// ---------------- own inventory across a rejoin or a reload (BUG-0678) ----------------
// Every player flies in a copy of the host's save: the cargo hold (tools and their charges, spice,
// other cargo, the cargo slots) is the game's cPlayerInventory (cSimulatorSpaceGame +0x40,
// docs/re/space.md 7.7), saved with the world. A guest that leaves and joins again (or loads the
// host's world again in a session) loads the host's share save, so its own hold was replaced by the
// host's (Discord 2026-11-07: "their items disappear and the host's items appear").
// The guest sends its hold to the host whenever it changes (K_INV; the first one of every Space world
// it loaded from the host is marked fresh). The host keeps the last one per player name for the Space
// world it plays (world_epoch, memory only, like the kept colonies of space_map.cpp, BUG-0195); a fresh
// hold of a player it kept one for is the host's copy: the host sends the kept one back (K_INV_KEPT),
// takes no hold of that player until the answer (K_INV_DONE, resent after 10 s), and the guest makes its
// game's hold that one: the host's items it never had leave, its own come back (AddItemByKey for tools,
// the tool's charges, CreateTradingObject for spice, the stack count of other cargo, AddCargoSlots).
// A first join has nothing kept: the guest starts with the save's hold, as before.
// `coop.fix space.keep_items off`: nothing comes back, the rejoined guest has the host's hold (0.7.9).
static bool g_keep_items_fix = true;
COOP_FIX("space.keep_items", "BUG-0678", g_keep_items_fix, "space: a guest's own cargo hold comes back when it rejoins or reloads (the host keeps it)");
// BUG-0816: all of that only while the guest plays the host's Space save. A connected guest that loads a Space save of
// its own (own_world::space_other) sent that save's hold as a fresh one, the host answered with the co-op hold it kept
// (K_INV_KEPT) and the guest's game replaced its own save's tools, spice and cargo with it (and the host kept the
// own save's hold as the guest's co-op one). Off: as 0.7.12.
static bool g_inv_own_save_fix = true;
COOP_FIX("space.hold_own_save", "BUG-0816", g_inv_own_save_fix, "space: a guest's own Space save keeps its own cargo hold");
static uint32_t g_inv_own_held;
static bool inv_own_save() { return g_inv_own_save_fix && own_world::space_other(); }

struct InvItem {
    uint8_t type = 0; // cSpaceInventoryItem +0x14 (2 tool, 4 trading object / spice, 0/1/5/6 other cargo)
    Key key;
    int count = 0;    // +0x10
    int ammo = -1;    // tools that use charges: cSpaceToolData +0x7C, else -1
};
struct Inv {
    int slots = 0; // cargo slot items (type 3), vt+0x88
    std::vector<InvItem> items;
};

static bool read_inv(Inv& out)
{
    void* inv = inventory();
    if (!inv)
        return false;
    out = Inv{};
    out.slots = vcall<int>(inv, 0x88 / 4);
    int n = vcall<int>(inv, 0x5C / 4);
    for (int i = 0; i < n && i < 256; i++) {
        void* it = vcall<void*>(inv, 0x60 / 4, i);
        if (!it || F<int>(it, 0x14) == 3)
            continue;
        InvItem x;
        x.type = (uint8_t)F<int>(it, 0x14);
        memcpy(x.key.v, vcall<const uint32_t*>(it, 0x18 / 4), 12);
        x.count = F<int>(it, 0x10);
        if (void* t = as_tool(it))
            x.ammo = (F<uint32_t>(t, kFlags) & 0x20) ? F<int>(t, kAmmo) : -1;
        out.items.push_back(x);
    }
    return true;
}

static void put_inv(net::Writer& w, const Inv& v)
{
    w.u16((uint16_t)std::clamp(v.slots, 0, 0xFFFF));
    uint16_t n = (uint16_t)std::min<size_t>(v.items.size(), 250);
    w.u16(n);
    for (uint16_t i = 0; i < n; i++) {
        const InvItem& x = v.items[i];
        w.u8(x.type);
        put_key(w, x.key);
        w.u32((uint32_t)x.count);
        w.u32((uint32_t)x.ammo);
    }
}

static bool get_inv(net::Reader& r, Inv& v)
{
    v = Inv{};
    v.slots = r.u16();
    uint16_t n = r.u16();
    if (!r.ok() || n > 250)
        return false;
    for (uint16_t i = 0; i < n && r.ok(); i++) {
        InvItem x;
        x.type = r.u8();
        x.key = get_key(r);
        x.count = (int)r.u32();
        x.ammo = (int)r.u32();
        if (x.count < 0 || x.count > 100000 || x.ammo < -1 || x.ammo > 100000)
            return false;
        v.items.push_back(x);
    }
    return r.ok();
}

static std::vector<uint8_t> inv_bytes(const Inv& v)
{
    net::Writer w;
    put_inv(w, v);
    return w.data;
}

static const InvItem* find_item(const Inv& v, uint8_t type, const Key& k)
{
    for (auto& x : v.items)
        if (x.type == type && x.key == k)
            return &x;
    return nullptr;
}

// BUG-0679 (docs/re/space.md#space-kept-hold-limits): the limits of space.keep_items.
// * space.keep_items_slots: a hold with fewer cargo slots than the host's copy had kept the host's extra (empty) slots;
//   now the free ones beyond the own count go (the game's RemoveItem, vt+0x78: a free slot item points at nothing).
// * space.keep_items_cargo: plant, animal and artifact cargo (types 0, 1/6, 5) the host's copy lacked could not be made
//   again; now the game's own factories make it (the ones its mission rewards use, 0xC53500 GOG 2024): animals
//   0xAC0C00 (by species key, type 6 with the flag), plants the plant cargo cache 0xB90560 + the template's Clone (vt
//   +0x24), artifacts 0xC87A60 (by key), then AddItem like spice.
// * space.keep_items_local: the kept hold lived in the host's memory only (a host restart, or another world loaded,
//   lost it) and what changed in the guest's hold while it was away never reached it. Now the guest also keeps its
//   own hold on its disk per host world (host name + planet file: <AppData>\Spore\SporeCoop\holds\<crc>.txt), written
//   whenever it changes in a world whose hold is its own (also after the session ended, as long as that world is
//   open), and a world loaded from the host again gets that hold at once (the host's kept one is then not needed).
static bool g_keep_slots_fix = true, g_keep_cargo_fix = true, g_keep_local_fix = true;
COOP_FIX("space.keep_items_slots", "BUG-0679", g_keep_slots_fix,
         "space: a guest's kept hold has its own number of cargo slots again, not the host's extra ones");
COOP_FIX("space.keep_items_cargo", "BUG-0679", g_keep_cargo_fix,
         "space: a guest's kept plant, animal and artifact cargo comes back too (the game's own factories)");
COOP_FIX("space.keep_items_local", "BUG-0679", g_keep_local_fix,
         "space: a guest keeps its own hold on its disk per host world (host restarts, changes while away)");
static uintptr_t a_AnimalCargo, a_AnimalMgr, a_PlantMgr, a_PlantFind, a_ArtifactCargo;
static uint32_t g_inv_slots_removed, g_inv_cargo_made;

// a plant / animal / artifact cargo item made by the game's factory (nullptr: no factory, no such species)
static void* make_cargo(uint8_t type, const Key& k, int n)
{
    void* out = nullptr;
    if ((type == 1 || type == 6) && a_AnimalCargo && a_AnimalMgr) {
        void* mgr = cdecl_call<void*>(a_AnimalMgr);
        thiscall<void>(a_AnimalCargo, mgr, &out, k.v, n, type == 6);
    } else if (type == 0 && a_PlantMgr && a_PlantFind) {
        void* mgr = cdecl_call<void*>(a_PlantMgr);
        void* tpl = mgr ? thiscall<void*>(a_PlantFind, mgr, k.v) : nullptr;
        if (tpl)
            vcall<void>(tpl, 0x24 / 4, &out); // Clone
    } else if (type == 5 && a_ArtifactCargo)
        cdecl_call<void>(a_ArtifactCargo, &out, k.v[0], k.v[1], k.v[2]);
    if (out && (F<int>(out, 0x14) != type || IsBadReadPtr(out, 0x7C))) {
        H->log(M, SK_WARN, "trade: own hold: the factory made type %d for a type %d cargo", F<int>(out, 0x14), type);
        vcall<void>(out, 0x08 / 4);
        return nullptr;
    }
    if (out)
        F<int>(out, 0x10) = n;
    return out;
}

static bool add_cargo(uint8_t type, const Key& k, int n)
{
    void* inv = inventory();
    void* it = inv && n > 0 ? make_cargo(type, k, n) : nullptr;
    if (!it)
        return false;
    bool ok = vcall<bool>(inv, 0x80 / 4, it, (void*)nullptr, true); // AddItem
    vcall<void>(it, 0x08 / 4);                                       // Release
    g_inv_cargo_made += ok;
    return ok;
}

// make the game's hold `want`; returns how many items could not be made as they were (logged)
static uint32_t g_inv_restored, g_inv_failed, g_inv_removed;
static int apply_inv(const Inv& want)
{
    void* inv = inventory();
    if (!inv)
        return -1;
    int failed = 0, removed = 0;
    // 1. what is not mine leaves (the host's items), what is more than mine shrinks: the game's TakeItem
    for (int guard = 0; guard < 256; guard++) {
        int n = vcall<int>(inv, 0x5C / 4), hit = -1, take = -1;
        for (int i = 0; i < n && i < 256 && hit < 0; i++) {
            void* it = vcall<void*>(inv, 0x60 / 4, i);
            if (!it || F<int>(it, 0x14) == 3)
                continue;
            Key k;
            memcpy(k.v, vcall<const uint32_t*>(it, 0x18 / 4), 12);
            uint8_t type = (uint8_t)F<int>(it, 0x14);
            const InvItem* w = find_item(want, type, k);
            if (!w)
                hit = i, take = -1; // the whole item
            else if (type != 2 && F<int>(it, 0x10) > w->count && w->count > 0)
                hit = i, take = F<int>(it, 0x10) - w->count;
        }
        if (hit < 0)
            break;
        void* out = nullptr;
        if (!vcall<bool>(inv, 0x7C / 4, hit, &out, take)) { // TakeItem
            failed++;
            H->log(M, SK_WARN, "trade: own hold: item %d not taken out", hit);
            break;
        }
        if (out)
            vcall<void>(out, 0x08 / 4); // Release
        removed++;
    }
    // 2. my cargo slots (bought upgrades): AddCargoSlots(n) vt+0x94 (0xFF39E0, ret 4); never fewer
    int slots = vcall<int>(inv, 0x88 / 4);
    if (want.slots > slots && want.slots - slots <= 64)
        vcall<void>(inv, 0x94 / 4, want.slots - slots);
    // BUG-0679: fewer: the free slots beyond mine go (the host's items left in step 1, so theirs are free)
    for (int guard = 0; g_keep_slots_fix && want.slots >= 0 && guard < 64 && vcall<int>(inv, 0x88 / 4) > want.slots; guard++) {
        void* free_slot = nullptr;
        int n = vcall<int>(inv, 0x5C / 4);
        for (int i = n - 1; i >= 0 && !free_slot; i--) {
            void* it = vcall<void*>(inv, 0x60 / 4, i);
            if (it && F<int>(it, 0x14) == 3 && vcall<bool>(inv, 0x98 / 4, it)) // IsSlotFree
                free_slot = it;
        }
        if (!free_slot || !vcall<bool>(inv, 0x78 / 4, free_slot)) // RemoveItem
            break;
        g_inv_slots_removed++;
    }
    // 3. mine comes back: tools first (an upgrade needs its base tool: a few passes), then cargo
    std::vector<const InvItem*> todo;
    for (auto& x : want.items)
        todo.push_back(&x);
    for (int pass = 0; pass < 4 && !todo.empty(); pass++) {
        std::vector<const InvItem*> again;
        for (const InvItem* x : todo) {
            int i = vcall<int>(inv, 0x64 / 4, (int)x->type, x->key.v); // FindItem
            void* it = i >= 0 ? vcall<void*>(inv, 0x60 / 4, i) : nullptr;
            if (x->type == 2) {
                if (!it && a_AddItemByKey) {
                    thiscall<void>(a_AddItemByKey, inv, x->key.v);
                    i = vcall<int>(inv, 0x64 / 4, 2, x->key.v);
                    it = i >= 0 ? vcall<void*>(inv, 0x60 / 4, i) : nullptr;
                }
                void* t = as_tool(it);
                if (!t) {
                    again.push_back(x);
                    continue;
                }
                if (x->ammo >= 0 && (F<uint32_t>(t, kFlags) & 0x20))
                    set_charges(t, x->ammo);
                continue;
            }
            int have = it ? F<int>(it, 0x10) : 0;
            if (have >= x->count)
                continue;
            if (it && x->type != 4) {
                F<int>(it, 0x10) = x->count; // a stack of mine: its count (no new slot needed)
                continue;
            }
            if (x->type == 4 && add_spice(x->key, x->count - have))
                continue;
            if (x->type != 4 && g_keep_cargo_fix && add_cargo(x->type, x->key, x->count - have)) // BUG-0679
                continue;
            again.push_back(x);
        }
        todo = std::move(again);
    }
    for (const InvItem* x : todo) {
        failed++;
        H->log(M, SK_WARN, "trade: own hold: %08X (type %d x%d) could not come back", x->key.v[0], x->type, x->count);
    }
    refresh_palettes();
    g_inv_removed += removed;
    H->log(M, SK_INFO, "trade: own hold back: %u items, %d of the host's out, %d not restored (BUG-0678)",
           (unsigned)want.items.size(), removed, failed);
    return failed;
}

struct KeptInv {
    Inv inv;
    bool have = false;
    uint8_t id = 0xFF;      // its session id now, 0xFF = not here
    bool restoring = false; // sent back, no answer yet
    uint32_t serial = 0;
    double sent = 0;
};
static std::map<std::string, KeptInv> g_kept_inv; // host: player name -> its own hold
static uint32_t g_kept_inv_epoch, g_inv_serial, g_inv_holds, g_inv_sent_back, g_inv_done;
// guest
static bool g_inv_join_seen;
static uint32_t g_inv_join_epoch, g_inv_fresh_epoch, g_inv_applied_epoch, g_inv_none_epoch, g_inv_sent;
static std::vector<uint8_t> g_inv_last;
static double g_inv_next, g_inv_fresh_t;

// ---- BUG-0679: the guest's own record of its hold, per host world, on its disk ----
static std::string g_rec_key;        // the host world (host name + planet file) of the record below
static uint32_t g_rec_epoch;         // the Space world whose hold is this player's own (written to the record)
static std::vector<uint8_t> g_rec_last;
static double g_rec_next, g_inv_epoch_t;
static uint32_t g_inv_epoch_seen, g_rec_written, g_rec_used;

static std::wstring rec_dir()
{
    wchar_t p[MAX_PATH];
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, p))) // profile-aware (hooked)
        return L"";
    std::wstring d = std::wstring(p) + L"\\Spore\\SporeCoop";
    CreateDirectoryW(d.c_str(), nullptr);
    d += L"\\holds";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}

// the host world this guest plays now: the host's name and the planet file (the same on every machine: the share
// keeps the file name, slotfs.cpp's marker); "" when unknown
static std::string rec_key_now()
{
    std::string host = spaceext::name_of(0), planet = modes::current_world_name();
    if (host.empty() || host == "?" || planet.size() < 5 || planet.compare(planet.size() - 4, 4, ".spo") != 0)
        return "";
    for (auto& c : planet)
        c = (char)tolower((unsigned char)c);
    return host + "|" + planet;
}

static std::wstring rec_file(const std::string& key)
{
    std::wstring d = rec_dir();
    if (d.empty() || key.empty())
        return L"";
    char n[16];
    snprintf(n, sizeof n, "%08X", net::crc32_update(0, (const uint8_t*)key.data(), key.size()));
    return d + L"\\" + std::wstring(n, n + strlen(n)) + L".txt";
}

static void rec_write(const std::string& key, const Inv& v)
{
    std::wstring f = rec_file(key);
    FILE* fp = f.empty() ? nullptr : _wfopen(f.c_str(), L"w");
    if (!fp)
        return;
    fprintf(fp, "world %s\nslots %d\n", key.c_str(), v.slots);
    for (auto& x : v.items)
        fprintf(fp, "item %d %08X %08X %08X %d %d\n", x.type, x.key.v[0], x.key.v[1], x.key.v[2], x.count, x.ammo);
    fclose(fp);
    g_rec_written++;
}

static bool rec_read(const std::string& key, Inv& v)
{
    std::wstring f = rec_file(key);
    FILE* fp = f.empty() ? nullptr : _wfopen(f.c_str(), L"r");
    if (!fp)
        return false;
    v = Inv{};
    v.slots = -1;
    bool same = false;
    char line[512];
    while (fgets(line, sizeof line, fp)) {
        std::string l(line);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r'))
            l.pop_back();
        InvItem x;
        int type = 0;
        if (l.rfind("world ", 0) == 0)
            same = l.substr(6) == key; // (a crc collision of two worlds is no record)
        else if (sscanf(l.c_str(), "slots %d", &v.slots) == 1) {
        } else if (sscanf(l.c_str(), "item %d %x %x %x %d %d", &type, &x.key.v[0], &x.key.v[1], &x.key.v[2], &x.count,
                          &x.ammo) == 6 && type >= 0 && type < 16 && x.count >= 0 && x.count <= 100000 && v.items.size() < 250) {
            x.type = (uint8_t)type;
            v.items.push_back(x);
        }
    }
    fclose(fp);
    return same && v.slots >= 0;
}

// the hold of a world whose hold is this player's own goes to the record when it changes (at most once a second;
// also while not connected, as long as that world is open: a refund paid at the session end, items picked up after)
static void rec_tick(double now)
{
    if (!g_keep_local_fix || !g_keep_items_fix || g_rec_key.empty() || !spaceext::in_space() ||
        spaceext::world_epoch() != g_rec_epoch || now < g_rec_next)
        return;
    g_rec_next = now + 1.0;
    Inv v;
    if (!read_inv(v))
        return;
    std::vector<uint8_t> b = inv_bytes(v);
    if (b == g_rec_last)
        return;
    g_rec_last = std::move(b);
    rec_write(g_rec_key, v);
}

// this world's hold is mine from now on (restored, or nothing was kept: the save's)
static void rec_own(uint32_t e)
{
    std::string k = rec_key_now();
    if (k.empty())
        return;
    g_rec_key = k;
    g_rec_epoch = e;
    g_rec_last.clear();
    g_rec_next = 0;
}

// guest: a world loaded from the host whose hold may still be replaced by the kept one (no answer yet to its
// fresh hold); at most 20 s after it went (a host without the fix never answers)
static bool inv_restore_pending()
{
    if (!g_keep_items_fix || !spaceext::connected() || spaceext::is_host() || !spaceext::in_space())
        return false;
    uint32_t e = spaceext::world_epoch();
    if (!e || e == g_inv_join_epoch || e == g_inv_applied_epoch || e == g_inv_none_epoch)
        return false;
    return e != g_inv_fresh_epoch || H->time_seconds() - g_inv_fresh_t < 20.0;
}

static void send_kept_inv(uint8_t id, KeptInv& k)
{
    net::Writer w;
    w.u8(id);
    w.u32(k.serial = ++g_inv_serial);
    w.u8(1); // a hold follows
    put_inv(w, k.inv);
    spaceext::send(K_INV_KEPT, w);
    k.restoring = true;
    k.sent = H->time_seconds();
    g_inv_sent_back++;
    H->log(M, SK_INFO, "trade: the own hold of %s (%u items) sent back to it (BUG-0678)", spaceext::name_of(id).c_str(),
           (unsigned)k.inv.items.size());
}

static void inv_tick(double now)
{
    if (!spaceext::connected()) {
        rec_tick(now); // BUG-0679: the own hold while away
        return;
    }
    if (spaceext::is_host()) {
        uint32_t e = spaceext::world_epoch();
        if (e && e != g_kept_inv_epoch) { // another Space world: the holds kept were of the old one
            g_kept_inv.clear();
            g_kept_inv_epoch = e;
        }
        for (auto& [n, k] : g_kept_inv) {
            if (k.id != 0xFF && !partner_here(k.id))
                k.id = 0xFF, k.restoring = false; // left
            else if (k.id != 0xFF && k.restoring && now - k.sent > 10.0)
                send_kept_inv(k.id, k); // no answer (lost, the guest still loading)
        }
        return;
    }
    if (!g_inv_join_seen) {
        g_inv_join_seen = true;
        g_inv_join_epoch = spaceext::in_space() ? spaceext::world_epoch() : 0;
    }
    if (inv_own_save()) { // BUG-0816: a Space save of the guest's own: its hold is nobody else's business
        g_inv_own_held++;
        return;
    }
    if (g_inv_own_save_fix && spaceext::in_space() && !modes::current_world())
        return; // BUG-0816: which save this is is not known yet (its hold goes once it is)
    // only the hold of a world loaded from the host since the join (the first one may be the guest's own world)
    uint32_t e = spaceext::world_epoch();
    if (!spaceext::in_space() || !e || e == g_inv_join_epoch || now < g_inv_next)
        return;
    g_inv_next = now + 1.0;
    Inv v;
    if (!read_inv(v))
        return;
    bool fresh = e != g_inv_fresh_epoch;
    if (e != g_inv_epoch_seen)
        g_inv_epoch_seen = e, g_inv_epoch_t = now;
    if (fresh && g_keep_local_fix && g_keep_items_fix && e != g_inv_applied_epoch) {
        // BUG-0679: this player's own record of the host world: its hold at once (after 2 s of the world, so the
        // game's load has settled), the host's kept one is not needed; the hold goes to the host as not fresh
        std::string key = rec_key_now();
        Inv own;
        if (!key.empty() && rec_read(key, own)) {
            if (now - g_inv_epoch_t < 2.0)
                return;
            int failed = apply_inv(own);
            if (failed >= 0) {
                g_inv_applied_epoch = e;
                g_inv_restored++;
                g_rec_used++;
                g_inv_failed += (uint32_t)failed;
                g_inv_fresh_epoch = e;
                rec_own(e);
                spaceext::notice(spaceext::tr("Your cargo hold is back.", "Ваш трюм восстановлен."));
                H->log(M, SK_INFO, "trade: own hold from this game's record of %s (BUG-0679)", key.c_str());
                if (!read_inv(v))
                    return;
                fresh = false;
            }
        }
    }
    rec_tick(now);
    std::vector<uint8_t> b = inv_bytes(v);
    if (!fresh && b == g_inv_last)
        return;
    g_inv_last = std::move(b);
    if (fresh)
        g_inv_fresh_t = now;
    g_inv_fresh_epoch = e;
    net::Writer w;
    w.u8(0);
    w.u32(++g_inv_sent);
    w.u8(fresh ? 1 : 0);
    put_inv(w, v);
    spaceext::send(K_INV, w);
}

static void inv_message(uint8_t kind, uint8_t origin, uint32_t id, net::Reader& r)
{
    if (kind == K_INV || kind == K_INV_DONE) {
        if (!spaceext::is_host())
            return;
        std::string name = spaceext::name_of(origin);
        if (name.empty() || name == "?")
            return;
        KeptInv& k = g_kept_inv[name];
        if (kind == K_INV_DONE) {
            if (k.restoring && k.id == origin && id == k.serial) {
                k.restoring = false;
                g_inv_done++;
            }
            return;
        }
        bool fresh = r.u8() != 0;
        Inv v;
        if (!get_inv(r, v))
            return;
        if (k.id != origin) // its first hold since it joined
            k.id = origin, k.restoring = false;
        if (fresh && k.have && g_keep_items_fix) { // the host's copy: its own goes back
            send_kept_inv(origin, k);
            return;
        }
        if (fresh) { // nothing kept (a first join): the guest keeps the save's hold, no restore to wait for
            net::Writer w;
            w.u8(origin);
            w.u32(0);
            w.u8(0);
            spaceext::send(K_INV_KEPT, w);
        }
        if (k.restoring)
            return; // the old copy, sent before it got its own back
        k.inv = std::move(v);
        k.have = true;
        g_inv_holds++;
        return;
    }
    // K_INV_KEPT, for me
    if (spaceext::is_host())
        return;
    uint32_t e = spaceext::world_epoch();
    if (!r.u8()) { // nothing kept for me: the hold of this world is mine
        if (r.ok() && e == g_inv_fresh_epoch) {
            g_inv_none_epoch = e;
            if (e != g_inv_applied_epoch)
                rec_own(e); // BUG-0679
        }
        return;
    }
    Inv v;
    if (!get_inv(r, v))
        return;
    int failed = 0;
    if (inv_own_save()) // BUG-0816: not applied to a Space save of our own (answered as done: the host stops resending)
        g_inv_own_held++;
    else if (g_keep_items_fix && spaceext::in_space() && inventory() && e != g_inv_applied_epoch && e != g_inv_join_epoch) {
        failed = apply_inv(v);
        if (failed < 0)
            return; // no hold now: the host sends it again
        g_inv_applied_epoch = e;
        g_inv_restored++;
        g_inv_failed += (uint32_t)failed;
        rec_own(e); // BUG-0679
        g_inv_last.clear(); // the hold now, at once
        g_inv_next = 0;
        spaceext::notice(spaceext::tr("Your cargo hold is back.", "Ваш трюм восстановлен."));
    } else if (e != g_inv_applied_epoch && g_keep_items_fix)
        return; // not in this world yet: the host sends it again
    net::Writer w;
    w.u8(0);
    w.u32(id);
    w.u16(0);
    w.u16((uint16_t)failed);
    spaceext::send(K_INV_DONE, w);
    (void)origin;
}

static void inv_session_end()
{
    g_inv_join_seen = false;
    g_inv_fresh_epoch = 0;
    g_inv_none_epoch = 0;
    g_inv_last.clear();
    g_inv_next = 0;
    for (auto& [n, k] : g_kept_inv)
        k.id = 0xFF, k.restoring = false;
}

static std::string inv_text(const Inv& v)
{
    std::string s;
    char b[160];
    snprintf(b, sizeof b, "slots %d items %u\n", v.slots, (unsigned)v.items.size());
    s += b;
    for (auto& x : v.items) {
        snprintf(b, sizeof b, "item type=%d key=%08X %08X %08X count=%d ammo=%d\n", x.type, x.key.v[0], x.key.v[1],
                 x.key.v[2], x.count, x.ammo);
        s += b;
    }
    return s;
}

static std::string inv_status()
{
    std::string s;
    char b[480];
    snprintf(b, sizeof b,
             "keep_items: fix=%d kept=%u holds=%u sent_back=%u done=%u restored=%u failed=%u removed=%u join_epoch=%u "
             "applied_epoch=%u world=%u slots_removed=%u cargo_made=%u record_written=%u record_used=%u record_world=%u own_save_held=%u\n",
             (int)g_keep_items_fix, (unsigned)g_kept_inv.size(), g_inv_holds, g_inv_sent_back, g_inv_done, g_inv_restored,
             g_inv_failed, g_inv_removed, g_inv_join_epoch, g_inv_applied_epoch, spaceext::world_epoch(), g_inv_slots_removed,
             g_inv_cargo_made, g_rec_written, g_rec_used, g_rec_epoch, g_inv_own_held);
    s += b;
    for (auto& [n, k] : g_kept_inv) {
        snprintf(b, sizeof b, "  kept '%s' id=%d have=%d restoring=%d items=%u slots=%d\n", n.c_str(), k.id == 0xFF ? -1 : k.id,
                 (int)k.have, (int)k.restoring, (unsigned)k.inv.items.size(), k.inv.slots);
        s += b;
    }
    return s;
}

// ---------------- frame ----------------
static void tick(double now)
{
    inv_tick(now);
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
    inv_session_end();
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
    if (a[0] == "inv") { // BUG-0678: the game's hold as the game holds it
        Inv v;
        return read_inv(v) ? inv_text(v) : "no inventory\n";
    }
    if (a[0] == "kept") // BUG-0678: the host's kept holds, the guest's restore
        return inv_status();
    if (a[0] == "cargo" && a.size() >= 3) { // tests (BUG-0679): cargo <type 0|1|5|6> <hex instance> [n]: the game's factory
        Key k;
        k.v[0] = (uint32_t)strtoul(a[2].c_str(), nullptr, 16);
        int n = a.size() > 3 ? atoi(a[3].c_str()) : 1;
        bool ok = add_cargo((uint8_t)atoi(a[1].c_str()), k, n);
        snprintf(b, sizeof b, "cargo %s %08X x%d: %s\n", a[1].c_str(), k.v[0], n, ok ? "added" : "not made");
        return b;
    }
    if (a[0] == "forget") { // tests (BUG-0679): this game's hold records go (memory and disk)
        g_rec_key.clear();
        g_rec_epoch = 0;
        g_rec_last.clear();
        std::wstring d = rec_dir();
        WIN32_FIND_DATAW fd;
        int n = 0;
        HANDLE h = d.empty() ? INVALID_HANDLE_VALUE : FindFirstFileW((d + L"\\*.txt").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do
                n += DeleteFileW((d + L"\\" + fd.cFileName).c_str()) ? 1 : 0;
            while (FindNextFileW(h, &fd));
            FindClose(h);
        }
        snprintf(b, sizeof b, "forgot %d hold records\n", n);
        return b;
    }
    if (a[0] == "slots" && a.size() >= 2) { // tests (BUG-0679): slots <n>: AddCargoSlots(n)
        void* inv = inventory();
        int n = atoi(a[1].c_str());
        if (!inv || n <= 0 || n > 64)
            return "no inventory or bad n\n";
        vcall<void>(inv, 0x94 / 4, n);
        snprintf(b, sizeof b, "slots %d\n", vcall<int>(inv, 0x88 / 4));
        return b;
    }
    if (a[0] == "money") {
        snprintf(b, sizeof b, "money %d\n", money());
        return b;
    }
    return "usage: coop.space trade [tools | all | give <inst> <n> [to] | ammo <inst> <n> | spices | spice <i> <n> | "
           "take <inst> <n> | deal | sell [n] | offers | accept <id> | decline <id> | money | refundfix 0|1 | "
           "away 0|1 | hold 0|1|2 | kept | cargo <type> <inst> [n] | slots <n>]\n";
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
    a_AnimalCargo = S("SporeCoop::AnimalCargo_Create");     // BUG-0679
    a_AnimalMgr = S("SporeCoop::AnimalCargo_Manager");
    a_PlantMgr = S("SporeCoop::PlantCargo_Manager");
    a_PlantFind = S("SporeCoop::PlantCargo_Template");
    a_ArtifactCargo = S("SporeCoop::ArtifactCargo_Create");
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
