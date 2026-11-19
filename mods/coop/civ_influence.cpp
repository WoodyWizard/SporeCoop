// The cities' religious and economic standing on the guests, and the host's preaching at cities it never looked at
// (WP-F2 of 0.7.16, fix/w56-sync; found by the Lineages fork (PR #196), its BUG-9084, 9081, 9086;
// docs/re/civ.md#civ-influence-relay).
//
// * BUG-1285 (civ.religious_state): the host's game converts the citizens (ConvertCitizens 0xBEB1E0 moves followers in
//   the city's map +0x5A0 and sets the converter +0x754); the city snapshot carried only the military capture meter, so
//   a guest's religious meter over the city (+0x750, which the city's update 0xBE2260 recomputes every tick from the
//   map: 200 * followers[converter] / (followers[converter] + followers[owner])) stayed at zero. Now the host sends each
//   city's follower map and converter once a second (MSG_CIV_STATE chunk kInfluenceChunk, chunks 0) and the guest
//   writes them with the game's own map index (0x643A00, unsigned keys; a value the host has not: 0, no node erased).
//   The guest's own game converts nobody then (its ConvertCitizens returns: the host's world decides).
// * BUG-1287 (civ.economic_state): the same chunk carries the buy meter (+0x758 float, the buyer +0x75C) and the
//   city's price map (+0x570 map<int, float>, the game's signed index 0xBE08E0): CanBuyCity 0xBD8110 needs the buyer
//   and the meter >= 100, GetBuyPrice 0xBE93A0 is -1 without the buyer's price entry, so a guest's Buy City never came.
//   The host's trade routes the guest has and the host has not any more are removed (civ.cpp guest_roads), with the
//   game's RemoveTradeRoute 0xBDFA70 at both ends, as the game's own removal 0xBDFB30 does.
// * BUG-1286 (civ.remote_preaching): a city's four preaching slots (+0x618, 0x80 bytes each) get their capacities
//   (slot +0 / +4) only from the crowd controller of the city the host's camera looks at (0xD57B00 -> 0xBD9FF0); at
//   any other city both are 0 and the preach tick 0xBEB310 converts nobody while the city still hits the preacher
//   (0xBEB6F8). The host's preach tick now gives such a slot the counts the controller gives (10 / 2: the layout's
//   trunc(r^2 / s^2 * 0.5) of radii 7 and 3, spacing 1.5) before the game's tick runs.
// An older guest drops the chunk (chunks 0 is a bad snapshot header there); an older host sends none, and a guest
// then keeps its own conversions (as 0.7.15).
#include "civ_internal.h"

namespace civ {

static bool g_religious_fix = true;
COOP_FIX("civ.religious_state", "BUG-1285", g_religious_fix,
         "civ: a guest sees the host's religious conversion meter and followers of every city (its own game converts nobody)");
static bool g_economic_fix = true;
COOP_FIX("civ.economic_state", "BUG-1287", g_economic_fix,
         "civ: a guest gets the host's buy meter and trade prices of every city (Buy City), and routes the host lost go");
static bool g_preach_fix = true;
COOP_FIX("civ.remote_preaching", "BUG-1286", g_preach_fix,
         "civ: preachers convert at a city the host's camera never looked at (its preaching places are set up)");

constexpr uintptr_t kCityFollowers = 0x5A0, kCityConvert = 0x750, kCityConvertId = 0x754, kCityBuy = 0x758,
                    kCityBuyer = 0x75C, kCityPrices = 0x570, kCityPreachSlots = 0x618;
constexpr size_t kMaxEntries = 16;
static uintptr_t a_FollowersIndex, a_PricesIndex, a_SetPreachCap, a_RemoveRoute;
using PreachTick_t = void(SK_THISCALL*)(void*, int, int);
static PreachTick_t o_PreachTick;
static double g_sent_at = -100;
struct InfluenceCounters {
    uint32_t tx = 0, rx = 0, cities_set = 0, guest_converts_blocked = 0, preach_slots_set = 0, preach_ticks = 0,
             routes_removed = 0, bad = 0;
};
static InfluenceCounters g_in;

// eastl::map<uint32, T> (rbtree): anchor at +4 (right +0, left +4, parent = root +0xC), size +0x14; node right +0,
// left +4, key +0x10, value +0x14 (docs/re/civ.md#civ-lineages-religion)
template <typename V>
static void map_read(void* map, std::vector<std::pair<uint32_t, V>>& out)
{
    out.clear();
    if (!map || !skg::readable(map, 0x18))
        return;
    uint32_t n = F<uint32_t>(map, 0x14);
    if (n > 64)
        return;
    std::vector<uint8_t*> stack;
    uint8_t* anchor = (uint8_t*)map + 4;
    uint8_t* cur = F<uint8_t*>(map, 0xC);
    size_t guard = 0;
    while ((cur && cur != anchor) || !stack.empty()) {
        if (++guard > 256)
            return;
        while (cur && cur != anchor) {
            if (!skg::readable(cur, 0x18) || stack.size() > 64)
                return;
            stack.push_back(cur);
            cur = *(uint8_t**)(cur + 4); // left
        }
        if (stack.empty())
            break;
        cur = stack.back();
        stack.pop_back();
        out.push_back({*(uint32_t*)(cur + 0x10), *(V*)(cur + 0x14)});
        cur = *(uint8_t**)cur; // right
    }
}

static bool city_ok(void* c) { return c && skg::readable(c, 0x760) && city_listed(c); }

// ---------------------------------------------------------------- host
void influence_host_tick(double now)
{
    if ((!g_religious_fix && !g_economic_fix) || now - g_sent_at < 1.0)
        return;
    g_sent_at = now;
    size_t n = std::min<size_t>(g_host_cities.size(), 250);
    std::vector<std::pair<uint32_t, int>> fol;
    std::vector<std::pair<uint32_t, float>> pri;
    size_t i = 0;
    while (i < n) {
        net::Writer w;
        w.u16(0); w.u8(kInfluenceChunk); w.u8(0);
        size_t count_at = w.size();
        w.u8(0);
        uint8_t k = 0;
        for (; i < n && w.size() < 900 && k < 255; i++) {
            void* c = g_host_cities[i];
            if (!city_ok(c))
                continue;
            map_read((uint8_t*)c + kCityFollowers, fol);
            map_read((uint8_t*)c + kCityPrices, pri);
            if (fol.size() > kMaxEntries)
                fol.resize(kMaxEntries);
            if (pri.size() > kMaxEntries)
                pri.resize(kMaxEntries);
            w.u8((uint8_t)i);
            w.u32(F<uint32_t>(c, kCityConvertId));
            w.u8((uint8_t)fol.size());
            for (auto& [p, v] : fol) {
                w.u32(p);
                w.u32((uint32_t)v);
            }
            w.f32(F<float>(c, kCityBuy));
            w.u32(F<uint32_t>(c, kCityBuyer));
            w.u8((uint8_t)pri.size());
            for (auto& [p, v] : pri) {
                w.u32(p);
                w.f32(v);
            }
            k++;
        }
        if (!k)
            break;
        w.data[count_at] = k;
        for (auto& pl : C.sess->players())
            if (pl.id != 0)
                C.sess->send_unreliable_to(pl.id, net::MSG_CIV_STATE, w);
        g_in.tx++;
    }
}

// BUG-1286: the host's preach tick of a slot whose capacities the camera's crowd controller never set
static void SK_THISCALL h_PreachTick(void* city, int slot, int preachers)
{
    g_in.preach_ticks++;
    if (g_preach_fix && a_SetPreachCap && slot >= 0 && slot < 4 && city && skg::readable(city, 0x620)) {
        uint8_t* slots = F<uint8_t*>(city, kCityPreachSlots);
        uint8_t* end = F<uint8_t*>(city, kCityPreachSlots + 4);
        if (slots && end >= slots + (slot + 1) * 0x80 && skg::readable(slots + slot * 0x80, 0x7C)) {
            int* s = (int*)(slots + slot * 0x80);
            // only a slot with its live spatial object (+0x78, the fork's verified condition: the slot is in use)
            if (s[0] == 0 && s[1] == 0 && s[0x78 / 4] != 0) {
                thiscall<void>(a_SetPreachCap, city, slot, 10, 2); // the controller's counts (0xD57D16)
                if (g_in.preach_slots_set++ < 8)
                    H->log(M, SK_INFO, "civ: preaching slot %d of city %p had no places (no camera there): 10 / 2 set (BUG-1286)",
                           slot, city);
            }
        }
    }
    o_PreachTick(city, slot, preachers);
}

// ---------------------------------------------------------------- guest
static bool g_host_relays = false; // a chunk came from this host (a host before 0.7.16 sends none)
static double g_rx_at = -100;
bool guest_converts_blocked()
{
    if (!g_religious_fix || !is_guest() || !g_host_relays || H->time_seconds() - g_rx_at > 10.0)
        return false;
    g_in.guest_converts_blocked++;
    return true;
}

template <typename V>
static void map_apply(uintptr_t index, void* map, const std::vector<std::pair<uint32_t, V>>& want)
{
    std::vector<std::pair<uint32_t, V>> have;
    map_read(map, have);
    for (auto& [p, v] : want) {
        uint32_t key = p;
        V* slot = thiscall<V*>(index, map, &key);
        if (slot && skg::readable(slot, sizeof(V)) && *slot != v)
            *slot = v;
    }
    for (auto& [p, v] : have) {
        bool kept = std::any_of(want.begin(), want.end(), [&](const std::pair<uint32_t, V>& x) { return x.first == p; });
        if (kept || v == V{})
            continue;
        uint32_t key = p;
        V* slot = thiscall<V*>(index, map, &key);
        if (slot && skg::readable(slot, sizeof(V)))
            *slot = V{};
    }
}

void guest_influence(net::Reader& r)
{
    uint8_t n = r.u8();
    struct Row {
        uint8_t ci;
        uint32_t conv;
        std::vector<std::pair<uint32_t, int>> fol;
        float buy;
        uint32_t buyer;
        std::vector<std::pair<uint32_t, float>> pri;
    };
    std::vector<Row> rows;
    for (int k = 0; k < n && r.ok(); k++) {
        Row x;
        x.ci = r.u8();
        x.conv = r.u32();
        uint8_t nf = r.u8();
        for (int j = 0; j < nf && j < 64 && r.ok(); j++) {
            uint32_t p = r.u32();
            x.fol.push_back({p, (int)r.u32()});
        }
        x.buy = r.f32();
        x.buyer = r.u32();
        uint8_t np = r.u8();
        for (int j = 0; j < np && j < 64 && r.ok(); j++) {
            uint32_t p = r.u32();
            x.pri.push_back({p, r.f32()});
        }
        rows.push_back(std::move(x));
    }
    if (!r.ok()) {
        g_in.bad++;
        return;
    }
    g_in.rx++;
    g_host_relays = true;
    g_rx_at = H->time_seconds();
    if (local_play() || game::mode_id() != modes::kCiv)
        return;
    for (auto& x : rows) {
        void* c = x.ci < g_host_cities.size() ? g_host_cities[x.ci] : nullptr;
        if (!city_ok(c) || planner_holds(c))
            continue;
        callguard::Hold applying(g_applying);
        if (g_religious_fix && a_FollowersIndex) {
            map_apply<int>(a_FollowersIndex, (uint8_t*)c + kCityFollowers, x.fol);
            F<uint32_t>(c, kCityConvertId) = x.conv; // (+0x750 follows from the map in the city's next update)
        }
        if (g_economic_fix) {
            if (std::isfinite(x.buy))
                F<float>(c, kCityBuy) = std::clamp(x.buy, 0.f, 1000.f);
            F<uint32_t>(c, kCityBuyer) = x.buyer;
            if (a_PricesIndex) {
                std::vector<std::pair<uint32_t, float>> pri;
                for (auto& p : x.pri)
                    if (std::isfinite(p.second))
                        pri.push_back(p);
                map_apply<float>(a_PricesIndex, (uint8_t*)c + kCityPrices, pri);
            }
        }
        g_in.cities_set++;
    }
}

// guest (guest_roads, BUG-1287): route a-b the host does not have any more: both ends, as the game's 0xBDFB30
bool remove_route(void* a, void* b)
{
    if (!g_economic_fix || !a_RemoveRoute || !city_ok(a) || !city_ok(b))
        return false;
    {
        callguard::Hold applying(g_applying);
        thiscall<void>(a_RemoveRoute, a, b);
        if (has_route(b, a))
            thiscall<void>(a_RemoveRoute, b, a);
    }
    g_in.routes_removed++;
    return !has_route(a, b) && !has_route(b, a);
}

void influence_init()
{
    a_FollowersIndex = H->sym("SporeCoop::MapU32Int_StageMap_At");
    a_PricesIndex = H->sym("SporeCoop::CityTradeMap_Index");
    a_SetPreachCap = H->sym("SporeCoop::City_SetPreachingCapacity");
    a_RemoveRoute = H->sym("Simulator::cCity::RemoveTradeRoute");
    if (uintptr_t t = H->sym("SporeCoop::City_PreachTick"))
        H->hook_create(M, (void*)t, (void*)h_PreachTick, (void**)&o_PreachTick);
    if (!a_FollowersIndex || !a_PricesIndex || !a_SetPreachCap || !a_RemoveRoute)
        H->log(M, SK_WARN, "civ: a symbol of the influence relay is missing (followers %d, prices %d, preach %d, routes %d)",
               (int)!!a_FollowersIndex, (int)!!a_PricesIndex, (int)!!a_SetPreachCap, (int)!!a_RemoveRoute);
}

// coop.civ influence [<host city index>] | convert <city> <count> (host) | unroad <a> <b> (host) | canbuy <city> (tests): the
// game's own state of the cities
std::string influence_debug(const std::vector<std::string>& a)
{
    char b[256];
    std::string out;
    if (a.size() >= 3 && a[0] == "convert") {
        size_t ci = (size_t)atoi(a[1].c_str());
        void* c = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
        uintptr_t f = H->sym("Simulator::cCity::ConvertCitizens");
        void* pc = player_civ();
        if (!city_ok(c) || !f || !pc)
            return "no such city / civ / symbol\n";
        thiscall<void>(f, c, pol_of(pc), atoi(a[2].c_str())); // through the hook (civ.cpp h_Convert)
        snprintf(b, sizeof b, "converted for %08X\n", pol_of(pc));
        out += b;
    }
    if (a.size() >= 3 && a[0] == "unroad" && is_host()) { // unroad a b: the game's RemoveTradeRoute at both ends
        size_t i = (size_t)atoi(a[1].c_str()), j = (size_t)atoi(a[2].c_str());
        if (i >= g_host_cities.size() || j >= g_host_cities.size() || i == j || !a_RemoveRoute)
            return "no such cities / symbol\n";
        void* x = g_host_cities[i];
        void* y = g_host_cities[j];
        thiscall<void>(a_RemoveRoute, x, y);
        if (has_route(y, x))
            thiscall<void>(a_RemoveRoute, y, x);
        snprintf(b, sizeof b, "route %zu-%zu removed: %s\n", i, j, has_route(x, y) || has_route(y, x) ? "no" : "yes");
        out += b;
    }
    if (a.size() >= 2 && a[0] == "canbuy") { // canbuy <city>: the game's own CanBuyCity / GetBuyPrice for the player civ
        size_t ci = (size_t)atoi(a[1].c_str());
        void* c = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
        uintptr_t cb = H->sym("Simulator::cCity::CanBuyCity"), gp = H->sym("Simulator::cCity::GetBuyPrice");
        void* pc = player_civ();
        if (!city_ok(c) || !cb || !gp || !pc)
            return "no such city / civ / symbol\n";
        bool can = thiscall<bool>(cb, c, pol_of(pc));
        float price = thiscall<float>(gp, c, pol_of(pc));
        snprintf(b, sizeof b, "canbuy %zu can=%d price=%.1f\n", ci, (int)can, price);
        return b;
    }
    snprintf(b, sizeof b, "influence: tx=%u rx=%u set=%u blocked=%u preach_set=%u preach_ticks=%u routes_removed=%u bad=%u "
             "relays=%d fixes=%d%d%d\n", g_in.tx, g_in.rx, g_in.cities_set, g_in.guest_converts_blocked, g_in.preach_slots_set,
             g_in.preach_ticks, g_in.routes_removed, g_in.bad, (int)g_host_relays, (int)g_religious_fix, (int)g_economic_fix,
             (int)g_preach_fix);
    out += b;
    void* pc = player_civ();
    snprintf(b, sizeof b, "player_pol=%08X\n", pc ? pol_of(pc) : 0u);
    out += b;
    size_t from = 0, to = g_host_cities.size();
    if (a.size() >= 2 && a[0] == "influence")
        from = (size_t)atoi(a[1].c_str()), to = std::min(to, from + 1);
    std::vector<std::pair<uint32_t, int>> fol;
    std::vector<std::pair<uint32_t, float>> pri;
    for (size_t i = from; i < to; i++) {
        void* c = g_host_cities[i];
        if (!city_ok(c))
            continue;
        map_read((uint8_t*)c + kCityFollowers, fol);
        map_read((uint8_t*)c + kCityPrices, pri);
        snprintf(b, sizeof b, "city %u %p owner=%08X convert=%.1f convert_id=%08X buy=%.1f buyer=%08X followers:", (unsigned)i, c,
                 pol_of(c), F<float>(c, kCityConvert), F<uint32_t>(c, kCityConvertId), F<float>(c, kCityBuy),
                 F<uint32_t>(c, kCityBuyer));
        out += b;
        for (auto& [p, v] : fol) {
            snprintf(b, sizeof b, " %08X=%d", p, v);
            out += b;
        }
        out += " prices:";
        for (auto& [p, v] : pri) {
            snprintf(b, sizeof b, " %08X=%.2f", p, v);
            out += b;
        }
        out += " slots:";
        uint8_t* s = F<uint8_t*>(c, kCityPreachSlots);
        uint8_t* e = F<uint8_t*>(c, kCityPreachSlots + 4);
        for (int k = 0; s && e >= s + (k + 1) * 0x80 && k < 4 && skg::readable(s + k * 0x80, 0xC); k++) {
            snprintf(b, sizeof b, " %d/%d/%d", ((int*)(s + k * 0x80))[0], ((int*)(s + k * 0x80))[1], ((int*)(s + k * 0x80))[2]);
            out += b;
        }
        out += "\n";
    }
    return out;
}

} // namespace civ
