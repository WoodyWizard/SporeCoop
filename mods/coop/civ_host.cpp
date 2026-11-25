// The host's side of the Civ stage: E1's owner claims (BUG-1153), dealing the cities, the vehicle table pass and
// the snapshot it sends (host_tick), the buildings' hit points, the guests' commands (host_command); moved out of
// civ.cpp (C8 of 0.7.15, BUG-1164).
#include "civ_internal.h"
#include "civ_rivals.h"
#include "civ_wallet.h"
#include "net/compat.h"
#include "own_world.h"
#include "rivals.h"

namespace civ {

// ---------------- host ----------------
// E1 (BUG-1153), every host tick: who is here; the store's claims of a world just opened go to its cities and vehicles
static void claims_tick()
{
    g_present.clear();
    for (auto& p : C.sess->players()) {
        std::string k = store::key_of(p.id);
        if (!k.empty())
            g_present[k] = p.id;
    }
    if (!g_store_fix)
        return;
    pstore::Store* w = store::world();
    if (w && store::serial() != g_claims_serial) {
        g_claims_serial = store::serial();
        g_city_spots.clear();
        g_veh_spots.clear();
        g_city_claim.clear();
        g_veh_claim.clear();
        g_cities_matched = g_vehs_matched = false;
        if (const pstore::Record* r = w->get_world("civ.owners"); r && r->ver == 1) {
            pstore::In in(r->data);
            if (!claims::get_spots(in, g_city_spots) || !claims::get_spots(in, g_veh_spots)) {
                g_city_spots.clear();
                g_veh_spots.clear();
            }
        }
    }
    void* pc = player_civ();
    if (!pc)
        return;
    if (!g_cities_matched) {
        auto cities = vec_of((uint8_t*)pc + kCivCities);
        if (cities.empty())
            return;
        std::vector<std::array<float, 3>> pos;
        for (void* c : cities) {
            net::Vec3 q = city_pos(c);
            pos.push_back({q.x, q.y, q.z});
        }
        auto m = claims::match(pos, g_city_spots, 5.0f);
        for (size_t i = 0; i < cities.size(); i++)
            if (m[i] >= 0) {
                const auto& sp = g_city_spots[m[i]];
                g_city_claim[cities[i]] = sp.key;
                if (sp.flags & 1)
                    g_city_gifted.insert(cities[i]);
                if (sp.flags & 2)
                    g_city_captured.insert(cities[i]);
                g_n.claim_cities++;
            }
        g_cities_matched = true;
        g_city_spots.clear();
    }
    if (!g_vehs_matched && hv_count()) {
        uint32_t ppol = pol_of(pc);
        std::vector<void*> vs;
        std::vector<std::array<float, 3>> pos;
        hv_each([&](const HvRef& x) {
            if (x.h->pol == ppol) {
                vs.push_back(x.v);
                net::Vec3 q = veh_pos(x.v);
                pos.push_back({q.x, q.y, q.z});
            }
        });
        auto m = claims::match(pos, g_veh_spots, 6.0f);
        for (size_t i = 0; i < vs.size(); i++)
            if (m[i] >= 0) {
                const auto& sp = g_veh_spots[m[i]];
                g_veh_claim[vs[i]] = sp.key;
                int id = present_id(sp.key);
                if (HvRef x = hv_find(vs[i]))
                    x.h->owner = id >= 0 ? (uint8_t)id : C.sess->my_id();
                g_n.claim_vehs++;
            }
        g_vehs_matched = true;
        g_veh_spots.clear();
    }
}

// BUG-1418 (audit CIV-6 A): a host is connected() alone (net/session.h), so host_tick deals every city to it and this
// refresh stamped the host's key on each 2 s later: when a guest joined, every non-capital city was claimed by the host
// and the joiner got only the shared capital. A deal made while the host plays alone is no claim now: a city keeps the
// claim it has (from the store, a capture, a gift or a purchase, which claim at the event) and an unclaimed one stays
// unclaimed for the deal at the next join. Off: the solo deal claims (0.7.16).
static bool g_fix_solo_claims = true;
COOP_FIX("civ.solo_claims", "BUG-1418", g_fix_solo_claims,
         "civ: a host playing alone claims no city by its own deal (a joiner gets a city of its own)");
// BUG-1418 (audit CIV-6 B): `coop.store adopt` renamed the claim keys but not the owner table: the cities and vehicles
// the host held for the absent player stayed the host's, and the next refresh wrote the host's key back over the
// adopted claims. Now the adopted player owns them at once. Off: the owner table is left as it was (0.7.16).
static bool g_fix_adopt_owners = true;
COOP_FIX("civ.adopt_owners", "BUG-1418", g_fix_adopt_owners,
         "civ: coop.store adopt gives the adopted player its claimed cities and vehicles at once");
uint32_t g_solo_unclaimed, g_adopt_owned;

// every 2 s: the claims follow the owners of the objects whose claimed player is here (a gift, a capture, a deal); an
// absent player's claim stays
static void claims_refresh()
{
    if (!g_store_fix || !g_cities_matched)
        return;
    void* pc = player_civ();
    if (!pc)
        return;
    std::unordered_map<void*, std::string> cc;
    bool solo = g_fix_solo_claims && C.sess->players().size() < 2; // BUG-1418: the host alone
    for (void* c : vec_of((uint8_t*)pc + kCivCities)) {
        auto old = g_city_claim.find(c);
        bool has_old = old != g_city_claim.end();
        auto o = g_city_owner.find(c);
        bool shared = o != g_city_owner.end() && o->second == kShared;
        std::string owner_key = o != g_city_owner.end() && !shared && o->second < 0xFE ? store::key_of(o->second) : "";
        std::string k = civlogic::city_claim(has_old, has_old ? old->second : std::string(),
                                             has_old && present_id(old->second) < 0, shared, owner_key, solo);
        if (!k.empty())
            cc[c] = k;
        else if (solo && !has_old)
            g_solo_unclaimed++; // BUG-1418: left for the deal at the next join
    }
    g_city_claim = std::move(cc);
    uint32_t ppol = pol_of(pc);
    std::unordered_map<void*, std::string> vc;
    hv_each([&](const HvRef& x) {
        if (x.h->pol != ppol)
            return;
        auto old = g_veh_claim.find(x.v);
        if (old != g_veh_claim.end() && present_id(old->second) < 0)
            vc[x.v] = old->second;
        else if (x.h->owner < 0xFE && !store::key_of(x.h->owner).empty())
            vc[x.v] = store::key_of(x.h->owner);
    });
    if (g_vehs_matched)
        g_veh_claim = std::move(vc);
}

// the game saves: the claims by position into the store (world record civ.owners v1: city spots, vehicle spots)
static void claims_save(pstore::Store& s, int64_t now)
{
    if (!g_store_fix || !g_claims_serial || g_claims_serial != store::serial() || &s != store::world() ||
        game::mode_id() != modes::kCiv)
        return;
    void* pc = player_civ();
    if (!pc)
        return;
    std::vector<claims::Spot> cs = g_city_spots, vs = g_veh_spots; // (not matched yet: as they were read)
    for (void* c : vec_of((uint8_t*)pc + kCivCities))
        if (auto it = g_city_claim.find(c); it != g_city_claim.end()) {
            net::Vec3 q = city_pos(c);
            cs.push_back({q.x, q.y, q.z, it->second,
                          (uint8_t)((g_city_gifted.count(c) ? 1 : 0) | (g_city_captured.count(c) ? 2 : 0))});
        }
    for (auto& [v, k] : g_veh_claim)
        if (hv_find(v)) {
            net::Vec3 q = veh_pos(v);
            vs.push_back({q.x, q.y, q.z, k, 0});
        }
    if (cs.empty() && vs.empty() && !s.get_world("civ.owners"))
        return;
    pstore::Out o;
    claims::put_spots(o, cs);
    claims::put_spots(o, vs);
    s.put_world("civ.owners", 1, o.d);
    std::vector<std::string> keys; // BUG-1500: the owners keep their names in the store
    for (auto* v : {&cs, &vs})
        for (auto& sp : *v)
            keys.push_back(sp.key);
    store::mark_owners(s, "civ.owner", keys, now);
}
// BUG-1418 (CIV-6 B): the adopted player (here now with the key `to`) owns its claimed cities (not the capital) and
// vehicles at once
static void adopt_owners(const std::string& to)
{
    if (!g_fix_adopt_owners || !C.sess)
        return;
    int id = -1;
    for (auto& p : C.sess->players())
        if (store::key_of(p.id) == to)
            id = p.id;
    void* pc = player_civ();
    if (id < 0 || !pc)
        return;
    auto cities = vec_of((uint8_t*)pc + kCivCities);
    for (auto& [c, k] : g_city_claim)
        if (k == to && civlogic::adopt_takes_city(std::find(cities.begin(), cities.end(), c) != cities.end(),
                                                  !cities.empty() && cities[0] == c)) {
            g_city_owner[c] = (uint8_t)id;
            g_adopt_owned++;
        }
    for (auto& [v, k] : g_veh_claim)
        if (k == to)
            if (HvRef x = hv_find(v)) {
                x.h->owner = (uint8_t)id;
                g_adopt_owned++;
            }
    H->log(M, SK_INFO, "civ: the adopted player %d owns its claimed cities and vehicles now (BUG-1418)", id);
}
static void claims_opened(); // NET-3 (BUG-1499), below
static bool g_claims_reg = store::add({"civ.owners", claims_save, [](const std::string& from, const std::string& to) {
                                           for (auto* m : {&g_city_claim, &g_veh_claim})
                                               for (auto& [o, k] : *m)
                                                   if (k == from)
                                                       k = to;
                                           for (auto* v : {&g_city_spots, &g_veh_spots})
                                               for (auto& sp : *v)
                                                   if (sp.key == from)
                                                       sp.key = to;
                                           adopt_owners(to); // BUG-1418
                                       }, claims_opened});
std::string solo_status()
{
    char b[160];
    snprintf(b, sizeof b, "solo_claims: fix=%d unclaimed=%u adopt_fix=%d adopt_owned=%u\n", (int)g_fix_solo_claims,
             g_solo_unclaimed, (int)g_fix_adopt_owners, g_adopt_owned);
    return b;
}
// tests (BUG-1418): city ci of the player civ as a save that was never played in co-op leaves it: no claim, no
// capture or gift mark (`coop.civ claims forget <ci>`)
std::string claims_forget(void* city)
{
    size_t n = g_city_claim.erase(city) + g_city_captured.erase(city) + g_city_gifted.erase(city) + g_capture_by.erase(city);
    return "claims forget: " + std::to_string(n) + " entries of the city dropped\n";
}

static void host_assign_cities(const std::vector<uint8_t>& players)
{
    if (civ_rivals::active()) {
        civ_rivals::host_sync_owners();
        g_split_for = players;
        return;
    }
    void* pc = player_civ();
    if (!pc || players.empty())
        return;
    auto cities = vec_of((uint8_t*)pc + kCivCities);
    // drop cities the player lost
    for (auto it = g_city_owner.begin(); it != g_city_owner.end();) {
        if (std::find(cities.begin(), cities.end(), it->first) == cities.end())
            it = g_city_owner.erase(it);
        else
            ++it;
    }
    if (g_fix_capture_keeps) { // BUG-0875: a capture whose owner change came after its cinematic gets its captor
        double now = H->time_seconds();
        for (auto it = g_capture_by.begin(); it != g_capture_by.end();) {
            bool here = std::find(cities.begin(), cities.end(), it->first) != cities.end();
            if (here && !g_city_owner.count(it->first)) {
                g_city_owner[it->first] = it->second.owner;
                g_city_captured.insert(it->first);
            }
            it = here || now - it->second.at > 120.0 ? g_capture_by.erase(it) : std::next(it);
        }
    }
    // (E1: a world whose claims came from the host's disk gets its owners from them, by key, not by name)
    if (g_fix_owners_kept && !g_kept_tried && !cities.empty() && !(g_store_fix && !g_city_claim.empty())) { // BUG-0950
        g_kept_tried = true;
        g_kept_key = world::current_game_key();
        auto it = g_kept_cities.find(g_kept_key);
        size_t got = 0;
        if (!g_kept_key.empty() && it != g_kept_cities.end() && g_city_owner.empty()) {
            for (void* c : cities) {
                net::Vec3 p = city_pos(c);
                const KeptCity* best = nullptr;
                for (auto& k : it->second)
                    if (net::dist(k.pos, p) < 5.0f && (!best || net::dist(k.pos, p) < net::dist(best->pos, p)))
                        best = &k;
                if (!best)
                    continue;
                uint8_t id = 0xFF;
                if (best->owner.empty())
                    id = kShared;
                else
                    for (auto& pl : C.sess->players())
                        if (pl.name == best->owner)
                            id = pl.id;
                if (id == 0xFF)
                    continue;
                g_city_owner[c] = id;
                if (best->gifted)
                    g_city_gifted.insert(c);
                if (best->captured)
                    g_city_captured.insert(c);
                got++;
            }
            g_n.kept_restored += (uint32_t)got;
            H->log(M, SK_INFO, "civ: %u of %u player cities got their owners of this world back (BUG-0950)", (unsigned)got,
                   (unsigned)cities.size());
        }
        if (got == cities.size())
            g_split_for = players; // every city has its owner again: nothing is dealt anew
    }
    bool resplit = players != g_split_for;
    g_split_for = players;
    if (cities.empty())
        return;
    // the first city of the player civ is the capital: the host's; the others in turns
    net::Vec3 cap = city_pos(cities[0]);
    std::sort(cities.begin() + 1, cities.end(),
              [&](void* a, void* b) { return net::dist(city_pos(a), cap) < net::dist(city_pos(b), cap); });
    // fewer cities than players: the capital is shared so that everybody can play from the start
    bool shared = cities.size() < players.size();
    auto present = [&](uint8_t o) { return std::find(players.begin(), players.end(), o) != players.end(); };
    // BUG-0875: a city its captor (or a gift's receiver) keeps while that player is here
    auto kept = [&](void* c) {
        auto it = g_city_owner.find(c);
        return it != g_city_owner.end() && present(it->second) &&
               (g_city_gifted.count(c) || (g_fix_capture_keeps && g_city_captured.count(c)));
    };
    // E1 (BUG-1153): a claimed city's holder (its player, or the host while that player is away); -1: no claim
    auto claimed = [&](void* c) -> int {
        if (!g_store_fix)
            return -1;
        auto it = g_city_claim.find(c);
        if (it == g_city_claim.end() || it->second == "shared")
            return -1;
        return claims::holder(it->second, present_id, C.sess->my_id());
    };
    std::vector<uint8_t> first; // capture_keeps: the players without a city get the free ones first
    if (g_fix_capture_keeps) {
        std::unordered_set<uint8_t> has{0}; // the host: the capital
        size_t free = 0;
        for (size_t i = 1; i < cities.size(); i++)
            if (int cl = claimed(cities[i]); cl >= 0)
                has.insert((uint8_t)cl);
            else if (kept(cities[i]))
                has.insert(g_city_owner[cities[i]]);
            else
                free++;
        for (size_t j = 1; j <= players.size(); j++)
            if (!has.count(players[j % players.size()]))
                first.push_back(players[j % players.size()]);
        shared = first.size() > free; // the capital stays everybody's while a player would have no city
    }
    if (shared != (g_city_owner.count(cities[0]) && g_city_owner[cities[0]] == kShared))
        resplit = true;
    size_t k = 0;
    auto next = [&]() -> uint8_t {
        size_t j = k++;
        return j < first.size() ? first[j] : players[(1 + j - first.size()) % players.size()];
    };
    for (size_t i = 0; i < cities.size(); i++) {
        if (!resplit && g_city_owner.count(cities[i]))
            continue;
        if (int cl = i > 0 ? claimed(cities[i]) : -1; cl >= 0) { // E1: its player's, or the host's while it is away
            if (g_city_owner.count(cities[i]) && g_city_owner[cities[i]] != cl)
                (cl == C.sess->my_id() ? g_n.claims_held : g_n.claims_back)++;
            g_city_owner[cities[i]] = (uint8_t)cl;
            continue;
        }
        if (kept(cities[i]) && (i > 0 || g_city_gifted.count(cities[i])))
            continue; // a gift (or a capture: capture_keeps) stays with its owner while that player is here
        g_city_owner[cities[i]] = i == 0 ? (shared ? kShared : 0) : next();
    }
}

// Trade between the players: every pair of cities of two different players trades, the shared
// treasury earns kTradePerPair every kTradePeriod seconds (a player alone earns nothing extra)
constexpr double kTradePeriod = 30.0;
constexpr int kTradePerPair = 20;
static double g_last_trade;
int g_trade_total, g_trade_pairs, g_trade_payouts;

static void trade_tick(double now)
{
    if (now - g_last_trade < kTradePeriod)
        return;
    g_last_trade = now;
    std::vector<std::pair<void*, uint8_t>> owned;
    for (auto& [city, o] : g_city_owner)
        if (o != kShared && o != 0xFF)
            owned.push_back({city, o});
    int pairs = 0, gain = 0;
    auto economic = [](uint8_t p) { return role(p) == 2; };
    std::vector<std::array<int, 3>> paid; // BUG-1468: (owner a, owner b, gain) of every pair
    bool riv = civ_rivals::active();
    for (size_t i = 0; i < owned.size(); i++)
        for (size_t j = i + 1; j < owned.size() && pairs < 8; j++)
            if (owned[i].second != owned[j].second) {
                if (riv && !rivals::allied(owned[i].second, owned[j].second))
                    continue; // Rivals: only allied players earn cross-player trade payouts
                pairs++;
                // an economic governor at either end doubles the pair (both: triple)
                int g = kTradePerPair * (1 + economic(owned[i].second) + economic(owned[j].second));
                gain += g;
                paid.push_back({owned[i].second, owned[j].second, g});
            }
    g_trade_pairs = pairs;
    if (!pairs)
        return;
    if (riv) {
        for (auto& [a, b, g] : paid)
            civ_rivals::on_allied_trade((uint8_t)a, (uint8_t)b, g);
        g_trade_total += gain;
        g_trade_payouts++;
        return;
    }
    std::string err;
    if (!shared::debug_add("money", gain, err))
        return;
    double out = 0; // BUG-1468 (civ.own_money): each end's half goes to its owner's own money
    for (auto& [a, b, g] : paid)
        out += civ_wallet::host_trade_pair((uint8_t)a, (uint8_t)b, g);
    if (out > 0)
        shared::debug_add("money", -out, err);
    if (g_income_cause_fix) // BUG-1236: the trade between the players' cities has its own notice below
        shared::note_passive(2, gain - out);
    g_trade_total += gain;
    if (g_trade_payouts++ % 6 == 0 && C.notice) // now and then, not every payout
        C.notice("trade between the players' cities: +" + std::to_string(gain) + " every 30 s (" +
                 std::to_string(g_trade_total) + " so far)");
}

// spice nodes (cCommodityNode, noun 0x403DF5C): position and owner political id (-1 free)
static std::vector<std::pair<net::Vec3, uint32_t>> spice_nodes()
{
    std::vector<std::pair<net::Vec3, uint32_t>> r;
    game::for_each_object(
        [](void* o, uint32_t n, void* u) {
            net::Vec3 p;
            if (n == kNodeNoun && game::object_alive(o) && game::object_position(o, p))
                ((std::vector<std::pair<net::Vec3, uint32_t>>*)u)->push_back({p, pol_of(o)});
            return true;
        },
        &r);
    return r;
}


// BUG-0186 probe (coop.civ pushprobe, tests): who moves a mirror on the guest. The vehicles' spatial
// vtable slots SetPosition (0x38) and Teleport (0x44) are pointed here; a call on a mirror that moves
// it more than 2 m, from the game (not guest_tick), is logged with its return address.
using SpSetPos_t = void(__thiscall*)(void*, const float*);
using SpTeleport_t = void(__thiscall*)(void*, const float*, const float*);
static SpSetPos_t g_probe_setpos;
static SpTeleport_t g_probe_tp;
static uintptr_t* g_probe_vt;
static void probe_note(void* sp, const float* p, const char* what, void* ret)
{
    if (g_applying || !p || g_n.probe_hits >= 40)
        return;
    void* v = (uint8_t*)sp - kVSpatial;
    GvRef it = gv_of(v);
    if (!it)
        return;
    net::Vec3 cur = veh_pos(v), to{p[0], p[1], p[2]};
    if (net::dist(cur, to) < 2.f)
        return;
    g_n.probe_hits++;
    H->log(M, SK_WARN, "civ: probe: %s of mirror #%u by %p: (%.1f %.1f %.1f) -> (%.1f %.1f %.1f)", what,
           (unsigned)it.id, ret, cur.x, cur.y, cur.z, to.x, to.y, to.z);
}
static void __fastcall probe_setpos(void* sp, void*, const float* p)
{
    probe_note(sp, p, "SetPosition", __builtin_return_address(0));
    g_probe_setpos(sp, p);
}
static void __fastcall probe_tp(void* sp, void*, const float* p, const float* q)
{
    probe_note(sp, p, "Teleport", __builtin_return_address(0));
    g_probe_tp(sp, p, q);
}
std::string push_probe()
{
    if (g_probe_vt)
        return "probe on";
    void* v = nullptr;
    gv_each([&](const GvRef& x) {
        if (!v && gv_ok(x))
            v = x.v;
    });
    if (!v)
        return "no mirror";
    uintptr_t* vt = *(uintptr_t**)((uint8_t*)v + kVSpatial);
    DWORD old;
    if (!VirtualProtect(vt + 0x38 / 4, 0x10, PAGE_READWRITE, &old))
        return "no access";
    g_probe_setpos = (SpSetPos_t)vt[0x38 / 4];
    g_probe_tp = (SpTeleport_t)vt[0x44 / 4];
    vt[0x38 / 4] = (uintptr_t)&probe_setpos;
    vt[0x44 / 4] = (uintptr_t)&probe_tp;
    VirtualProtect(vt + 0x38 / 4, 0x10, old, &old);
    g_probe_vt = vt;
    char b[96];
    snprintf(b, sizeof b, "probe on vtable %p", (void*)vt);
    return b;
}
void boost_tick(double now);
// a vehicle given away (or taken by a capture) stays selected on the giver's screen and its orders
// went to the game directly on the host: only the owner keeps a vehicle selected
void drop_foreign_selection()
{
    uint8_t me = C.sess->my_id();
    for (void* v : all_vehicles())
        if (F<uint8_t>(v, kVSelected)) {
            uint8_t o = owner_of_vehicle(v);
            if (o != 0xFF && o != me) {
                thiscall<int>(a_SetSelected, (uint8_t*)v + kVSpatial, 0);
                g_n.unselected++;
            }
        }
}

// The host's world stands still while one of its cinematics plays (a captured city's caption waits
// for OK): everybody is told why the vehicles stopped
static bool g_movie;
bool movie_playing()
{
    void* cm = a_CinematicMgr ? skg::cdecl_call<void*>(a_CinematicMgr) : nullptr;
    return cm && F<uint32_t>(cm, 0x2C) != 0;
}
// BUG-0187: with guests in the session the host's caption closes itself after kCaptionWait seconds
// (the game's EndCinematic, as Enter does), so a host who is away does not stop everybody's game.
// `coop.civ fix caption 0`: the old wait for the host's OK (tests).
bool g_fix_caption = true;
COOP_FIX("civ.caption", "BUG-0187", g_fix_caption, "civ: the host's capture caption closes itself when guests wait");
double g_caption_wait = 8.0, g_movie_since;
// BUG-0224: only a cinematic that a capture started is ended. Before, every cinematic of the host
// in Civ (a city purchase, the camera when a city appears, superweapons) was cut after 8 s. A
// capture (h_Capture, host, any nation) arms; the 8 s count from the arm time while a cinematic
// plays (a purchase chain may never drop +0x2C between its cinematic and the caption). Disarmed
// when the cinematic ends after it was seen, when no cinematic came within kArmWindow, after an
// end, and at a session end or world change. Accepted: a capture during another long cinematic
// ends that one (the caption is queued behind it and everybody waits anyway).
constexpr double kArmWindow = 3.0;
double g_caption_armed;  // time of the last capture, 0 = not armed
static bool g_armed_seen;       // a cinematic played since the arm
void caption_arm()
{
    g_caption_armed = H->time_seconds();
    g_armed_seen = false;
    g_n.caption_arms++;
}
void caption_disarm()
{
    g_caption_armed = 0;
    g_armed_seen = false;
}
static void movie_tick()
{
    bool on = movie_playing();
    double now = H->time_seconds();
    if (g_caption_armed > 0) {
        if (on)
            g_armed_seen = true;
        else if (g_armed_seen || now - g_caption_armed > kArmWindow)
            caption_disarm(); // the caption ended, or the capture showed none
    }
    double since = g_caption_armed > 0 && g_movie_since > 0 ? g_caption_armed : -1;
    if (on && g_movie && g_fix_caption && since > 0 && now - since > g_caption_wait &&
        C.sess->players().size() >= 2) {
        g_movie_since = -1; // once per cinematic
        caption_disarm();
        H->run_outside_frame(
            [](void*) {
                uintptr_t end = H->sym("SporeCoop::cCinematicManager_EndCinematic");
                void* cm = a_CinematicMgr ? skg::cdecl_call<void*>(a_CinematicMgr) : nullptr;
                if (!end || !cm || !F<uint32_t>(cm, 0x2C) || game::mode_id() != modes::kCiv)
                    return;
                thiscall<void>(end, cm);
                g_n.captions_closed++;
                if (C.notice)
                    C.notice("the caption closed by itself: the world goes on");
            },
            nullptr);
    }
    if (on == g_movie)
        return;
    g_movie = on;
    g_movie_since = on ? now : 0;
    if (on && C.notice)
        C.notice(C.sess->name_of(C.sess->my_id()) + " watches a cinematic: the world waits for the OK" +
                 (g_fix_caption && g_caption_armed > 0 && C.sess->players().size() >= 2 ? " (or " + std::to_string((int)g_caption_wait) + " s)" : ""));
}

// BUG-0493: a guest's building for the city the host's own planner edits (a shared capital) is built
// when that planner closes
std::vector<HeldBuild> g_held_builds;
// BUG-0807: "the money is gone and the building disappears". A guest pays a building in its own planner (the shared
// money), and the host builds the same one (CMD_BUILD). When the host's game cannot build it (no free slot left: the
// host's own building took it in the same moment; the city is another player's now; its model is missing here), the
// host only counted a rejected command, the guest's reconcile then demolished the guest's building as an extra one and
// the price stayed paid. Now the guest sends what it paid (a float after the place; an older guest sends nothing) and
// the host gives that back to the shared money (at most kRefundMax) and tells the guest why.
// `coop.fix civ.build_refund off`: the old rejection (no refund, no notice).
static bool g_fix_build_refund = true;
COOP_FIX("civ.build_refund", "BUG-0807", g_fix_build_refund, "civ: a guest's building the host's game cannot build gives its price back to the shared money, with a notice");
static constexpr float kRefundMax = 100000.f;
float g_build_refunded;
static void refund_build(uint8_t from, float paid, const char* why);
const char* g_build_fail = ""; // why the last build_in_city failed
// BUG-0602 diagnostics: the layout slot a building is in (-1: none) and the distance from p to the nearest slot
int slot_of(void* city, void* b, const net::Vec3& p, float* nearest, void** occ)
{
    uint8_t* sb = F<uint8_t*>(city, kCitySlots);
    uint8_t* se = F<uint8_t*>(city, kCitySlots + 4);
    int in = -1;
    float best = 3.4e38f;
    for (int k = 0; sb && sb + (k + 1) * 0x18 <= se && k < 64; k++) {
        uint8_t* sl = sb + k * 0x18;
        if (F<void*>(sl, 4) == b)
            in = k;
        net::Vec3 q{F<float>(sl, 0xC), F<float>(sl, 0x10), F<float>(sl, 0x14)};
        float d = net::dist(p, q);
        if (d < best) {
            best = d;
            if (occ)
                *occ = F<void*>(sl, 4);
        }
    }
    if (nearest)
        *nearest = best;
    return in;
}
static std::unordered_set<void*> g_host_offslot_seen;
void* build_in_city(void* city, uint32_t noun, const net::Vec3& pos, bool pay);
// BUG-0602: a building the host's game has off the layout slot this game gives it (SnapBuildingToSlot takes the
// free slot nearest the host's spot, 8-9 m away in campaign_space): the reconciliation found no building within
// 4 m of the host's, destroyed this one and built it again, hundreds of times (buildings_fixed=+281/-281), and the
// building stood elsewhere in the guest's game. A guest's building made for the host's stands where the host's
// stands (its slot kept: the city's lists and the planner's slot stay the game's).
void to_host_spot(void* b, const net::Vec3& p)
{
    net::Vec3 bp{};
    if (!b || !game::object_position(b, bp) || net::dist(bp, p) <= 1.f)
        return;
    float q[3] = {p.x, p.y, p.z};
    vcall<void>((uint8_t*)b + 0x34, 0x38 / 4, (const float*)q); // spatial SetPosition, as build_in_city does
    // its layout slot goes with it (cLayoutSlot +0xC), so the slot nearest the building stays its own (the
    // planner picks a building by that, BUG-0491) and a later snap finds the host's spot
    for (void* c : g_host_cities) {
        uint8_t* sb = c ? F<uint8_t*>(c, kCitySlots) : nullptr;
        uint8_t* se = c ? F<uint8_t*>(c, kCitySlots + 4) : nullptr;
        for (int k = 0; sb && sb + (k + 1) * 0x18 <= se && k < 64; k++)
            if (F<void*>(sb + k * 0x18, 4) == b) {
                F<float>(sb + k * 0x18, 0xC) = p.x;
                F<float>(sb + k * 0x18, 0x10) = p.y;
                F<float>(sb + k * 0x18, 0x14) = p.z;
            }
    }
    g_n.bfix_moved++;
}

// ---- BUG-0529: the hit points of a city's buildings ----
// The snapshot's building rows (one city per cycle) carry noun, place and the ruined flag: a guest got the
// host's ruins, never the hit points of a building that still stands, so a city the host's vehicles hit stood
// whole on the guest until a building fell. The host now sends the hit points of the same city's buildings
// (MSG_CIV_STATE chunk 0xFD, chunks 0: an older guest drops it) when they changed and every 3 s; the guest
// lowers a building's hit points with the game's combatant damage (no attacker: its own damage look, no
// capture or alarm) and raises them (the host's repair) by setting them.
constexpr uint32_t kCityHallNoun = 0x18EA1EB;
std::unordered_map<void*, std::pair<uint32_t, double>> g_hp_sent; // host: city -> hash of the hit points, when
static bool hp_building(void* bl)
{
    uint32_t n = game::noun_id(bl);
    return type_for_noun(n) || n == kCityHallNoun;
}
static void host_send_building_hp(size_t ci, void* city, double now)
{
    std::vector<void*> blds;
    for (void* bl : vec_of((uint8_t*)city + kCityBuildings))
        if (hp_building(bl) && blds.size() < 40)
            blds.push_back(bl);
    uint32_t h = 2166136261u;
    for (void* bl : blds)
        h = (h ^ (uint32_t)(int)std::lround(F<float>(bl, kBuildingState + 4))) * 16777619u;
    auto& last = g_hp_sent[city];
    if (last.first == h && now - last.second < 3.0)
        return;
    last = {h, now};
    net::Writer w;
    w.u16(0); w.u8(kHpChunk); w.u8(0);
    w.u8((uint8_t)ci);
    w.u8((uint8_t)blds.size()); // 40 x 20 bytes + 6: within kMaxPacket
    for (void* bl : blds) {
        net::Vec3 bp;
        game::object_position(bl, bp);
        w.u32(game::noun_id(bl));
        w.f32(bp.x); w.f32(bp.y); w.f32(bp.z);
        w.f32(F<float>(bl, kBuildingState + 4));
    }
    for (auto& pl : C.sess->players())
        if (pl.id != 0)
            C.sess->send_unreliable_to(pl.id, net::MSG_CIV_STATE, w);
    g_n.hp_tx++;
}

static void refund_build(uint8_t from, float paid, const char* why)
{
    if (!g_fix_build_refund || !C.sess || from == C.sess->my_id())
        return;
    void* pc = player_civ();
    float back = civlogic::refund_amount(paid, kRefundMax);
    bool own = back > 0 && civ_wallet::host_refund(from, back); // BUG-1468: back into its own money (civ.own_money)
    if (own) {
        g_n.build_refunds++;
        g_build_refunded += back;
    } else if (pc && back > 0 && o_AddMoney) {
        o_AddMoney(pc, back, 0); // the shared money (no difficulty multiplier)
        g_n.build_refunds++;
        g_build_refunded += back;
    }
    char t[200];
    snprintf(t, sizeof t, "the host's game could not build your building (%s)%s", why && *why ? why : "refused",
             back > 0 ? (own ? ": its price is back in your money" : ": its price is back in the shared money") : "");
    net::Writer nw;
    nw.str8(t);
    C.sess->send_reliable_to(from, net::MSG_NOTICE, nw);
    H->log(M, SK_INFO, "civ: building of player %u not built here (%s): %.0f back to the money (BUG-0807)", (unsigned)from,
           why ? why : "", back);
}

// NET-3 (BUG-1499): the save opened the world's store itself: its claims are read and matched now and follow the
// owners of this moment (as the next ticks would), so this save writes them
static void claims_opened()
{
    if (!active() || !is_host() || game::mode_id() != modes::kCiv)
        return;
    claims_tick();
    if (g_fix_owners_kept)
        claims_refresh();
}

static double g_last_kin_send = 0.0;
static uint16_t g_kin_tx_seq = 0;

// BUG-0438: 20 Hz kinematics stream for moving vehicles (chunk kVehKinChunk 0xF8, chunks 0), sent only to civ 1.3+ peers.
static void host_send_veh_kin(double now)
{
    if (!g_veh_burst_fix || !C.sess)
        return;
    if (!net::state_due(now, g_last_kin_send, 0.f, 0.f, civlogic::kVehKinPeriod))
        return;
    std::vector<uint8_t> peers;
    for (auto& pl : C.sess->players()) {
        if (pl.id == 0)
            continue;
        bool has = false;
        std::vector<net::FamilyRev> fam;
        if (C.sess->peer_families(pl.id, has, fam) &&
            net::compat::has_minor(fam, has, (uint8_t)net::Family::civ, 1, 3))
            peers.push_back(pl.id);
    }
    if (peers.empty())
        return;
    struct KinRow {
        uint16_t id;
        net::Vec3 pos;
        net::Quat rot;
        net::Vec3 vel;
        float top;
    };
    std::vector<KinRow> rows;
    auto vehicles = all_vehicles();
    for (void* v : vehicles) {
        HvRef x = hv_find(v);
        if (!x || F<uint8_t>(v, kVDead))
            continue;
        HV& hv = *x.h;
        net::Vec3 p = veh_pos(v);
        float top = F<float>(v, 0x218);
        if (!std::isfinite(top) || top < 0.f || top > 10000.f)
            top = 0.f;
        float loco[3] = {F<float>(v, kVVel), F<float>(v, kVVel + 4), F<float>(v, kVVel + 8)};
        float pf[3] = {p.x, p.y, p.z};
        float prev[3] = {hv.prev_pos.x, hv.prev_pos.y, hv.prev_pos.z};
        float vf[3] = {};
        civlogic::veh_vel_pick(loco, pf, prev, hv.have_prev, now - hv.prev_t, 2.f * top + 10.f, vf);
        hv.prev_pos = p;
        hv.prev_t = now;
        hv.have_prev = true;
        float sp2 = vf[0] * vf[0] + vf[1] * vf[1] + vf[2] * vf[2];
        if (sp2 > 0.09f)
            hv.last_moving_at = now;
        float lp[3] = {hv.last_kin_pos.x, hv.last_kin_pos.y, hv.last_kin_pos.z};
        if (!civlogic::veh_kin_send(vf, pf, lp, hv.have_kin, now, hv.last_moving_at))
            continue;
        hv.last_kin_pos = p;
        hv.have_kin = true;
        rows.push_back({x.id, p, veh_rot(v), {vf[0], vf[1], vf[2]}, top});
    }
    if (rows.empty())
        return;
    const uint32_t ms = net::now_ms();
    constexpr size_t kMaxPerKin = 24; // 9 + 24 * 46 = 1113 <= kMaxPacket
    for (size_t off = 0; off < rows.size(); off += kMaxPerKin) {
        size_t cnt = std::min(kMaxPerKin, rows.size() - off);
        g_kin_tx_seq++;
        net::Writer w;
        w.u16(g_kin_tx_seq);
        w.u8(kVehKinChunk);
        w.u8(0);
        w.u32(ms);
        w.u8((uint8_t)cnt);
        for (size_t i = off; i < off + cnt; i++) {
            const KinRow& r = rows[i];
            w.u16(r.id);
            w.f32(r.pos.x); w.f32(r.pos.y); w.f32(r.pos.z);
            w.f32(r.rot.x); w.f32(r.rot.y); w.f32(r.rot.z); w.f32(r.rot.w);
            w.f32(r.vel.x); w.f32(r.vel.y); w.f32(r.vel.z);
            w.f32(r.top);
        }
        for (uint8_t pid : peers)
            C.sess->send_unreliable_to(pid, net::MSG_CIV_STATE, w);
        g_n.kin_tx++;
    }
}

void host_tick(double now)
{
    if (!g_held_builds.empty() && !planner_city()) {
        auto held = std::move(g_held_builds);
        g_held_builds.clear();
        auto cities = all_cities();
        for (auto& h : held) {
            bool listed = std::find(cities.begin(), cities.end(), h.city) != cities.end();
            void* b = nullptr;
            if (listed) {
                g_build_from = h.from;
                b = build_in_city(h.city, h.noun, h.pos, false);
                g_build_from = -1;
            }
            if (!b)
                refund_build(h.from, h.paid, listed ? g_build_fail : "the city is gone"); // BUG-0807
        }
    }
    trade_tick(now);
    boost_tick(now);
    movie_tick();
    host_send_shots(now); // BUG-0477
    host_send_tribes(now); // BUG-0539
    influence_host_tick(now); // BUG-1285, BUG-1287
    ff_host_tick(now); // BUG-0899
    host_send_veh_kin(now); // BUG-0438: 20 Hz moving-vehicle kinematics
    if (g_veh_burst_fix) {
        if (!net::state_due(now, g_last_send, 0.f, 0.f, 0.1))
            return;
    } else {
        if (now - g_last_send < 0.1)
            return;
        g_last_send = now;
    }
    drop_foreign_selection();
    std::vector<uint8_t> players;
    for (auto& p : C.sess->players())
        players.push_back(p.id);
    std::sort(players.begin(), players.end());
    bool roster_changed = players != g_split_for;
    claims_tick(); // E1 (BUG-1153)
    host_assign_cities(players);
    if (g_fix_owners_kept && now - g_kept_at > 2.0) { // BUG-0950: the owners of this world, by position and name
        g_kept_at = now;
        claims_refresh(); // E1 (BUG-1153)
        if (void* pc = player_civ()) {
            if (g_kept_key.empty())
                g_kept_key = world::current_game_key();
            std::vector<KeptCity> v;
            for (void* c : vec_of((uint8_t*)pc + kCivCities)) {
                auto it = g_city_owner.find(c);
                if (it == g_city_owner.end() || it->second == 0xFF)
                    continue;
                v.push_back({city_pos(c), it->second == kShared ? std::string() : C.sess->name_of(it->second),
                             g_city_gifted.count(c) > 0, g_city_captured.count(c) > 0});
                if (it->second != kShared && v.back().owner.empty())
                    v.pop_back(); // a player without a name: not kept
            }
            if (!g_kept_key.empty() && !v.empty()) {
                g_kept_cities[g_kept_key] = std::move(v);
                g_n.kept_saved++;
            }
        }
    }
    // BUG-0031: the vehicles of a player who left go to the owner of the nearest player city (the
    // cities were split anew just now); before, nobody could order them, they took slots of the
    // vehicle cap and the next joiner under the reused id inherited them
    // BUG-0223: the game's vehicle list first, then the table pruned against it (alive check before
    // the pol read), then the orphan loop over vehicles that still exist: before, both read the memory
    // of vehicles that may have been freed since the last tick
    void* pc = player_civ();
    uint32_t ppol = pc ? pol_of(pc) : 0;
    g_host_cities = all_cities();
    auto vehicles = all_vehicles();
    std::unordered_set<void*> alive(vehicles.begin(), vehicles.end());
    // C1 (BUG-1140): the table pass is the registry's sweep: an entry whose vehicle left the game's list, or whose
    // address holds a vehicle of another world or nation (BUG-0059) or another vehicle of the same nation
    // (BUG-0951: another serial), is not seen and goes (no game call: the host's game owns its vehicles)
    g_hvr.sweep(
        [&](uint16_t id) {
            auto* e = g_hvr.find(id);
            void* v = e ? e->ref.p : nullptr;
            if (!v || !alive.count(v))
                return false;
            if (pol_of(v) != e->data.pol)
                return false;
            if (g_veh_lifetime_fix && e->ref.serial && game::object_serial(v) != e->ref.serial) {
                g_n.veh_stale++; // BUG-0951: another vehicle of the same nation at the address: a new id and owner
                return false;
            }
            return true;
        },
        [](auto&, bool) { return mirror::Unseen::Remove; }, [](auto&, bool) {});
    if (roster_changed && g_store_fix) // E1 (BUG-1153): a claimed vehicle goes to its player, or the host holds it
        hv_each([&](const HvRef& x) {
            if (auto it = g_veh_claim.find(x.v); it != g_veh_claim.end())
                x.h->owner = claims::holder(it->second, present_id, C.sess->my_id());
        });
    if (roster_changed)
        hv_each([&](const HvRef& x) {
            HV& hv = *x.h;
            if (hv.owner >= civlogic::kNpc || civlogic::present(players, hv.owner))
                return;
            uint8_t nearest = 0, owner;
            nearest_player_city(veh_pos(x.v), &nearest);
            if (!civlogic::orphan_owner(hv.owner, players, nearest, owner))
                return;
            hv.owner = owner;
            g_n.orphans_given++;
        });
    for (void* v : vehicles) {
        if (civ_rivals::active()) {
            int rpid = civ_rivals::player_of_pol(pol_of(v));
            uint8_t owner = rpid >= 0 ? (uint8_t)rpid : civlogic::kNpc;
            if (HvRef ex = hv_find(v)) {
                ex.h->owner = owner;
            } else {
                hv_add(v, owner);
            }
            continue;
        }
        if (hv_find(v))
            continue;
        bool mine = pol_of(v) == ppol;
        uint8_t nearest = civlogic::kNpc; // (no player city: kept)
        if (mine && g_pending_owner < 0)
            nearest_player_city(veh_pos(v), &nearest); // built by that city
        uint8_t owner = civlogic::new_vehicle_owner(mine, g_pending_owner, nearest);
        hv_add(v, owner); // BUG-0154: never a live id, never 0
    }
    g_cycle++;
    host_send_roads(now); // BUG-0494
    // Chunks that fit one packet (kMaxPacket): a larger message is dropped by the connection for
    // good. Until 2026-10-23 chunk 0 carried every city and 24 vehicles (and later the spice nodes and
    // a city's buildings): ~1200 bytes with 9 cities and 24 vehicles, so with a grown planet the
    // guest got no cities, captures or the first 24 vehicles at all. The items are packed in order
    // (cities, spice nodes, one city's buildings, vehicles) into as many chunks as needed.
    // + u32 time (BUG-0122); a vehicle is 41 bytes + its f32 top speed in the tail after the time (BUG-0787)
    constexpr size_t kCityBytes = 25, kNodeBytes = 16, kVehBytes = 45, kHead = 14;
    const size_t kBudget = g_chunk_budget; // net::kMaxPacket - 48 (packet + message headers); tests: less
    std::vector<std::pair<net::Vec3, uint32_t>> nodes;
    if (g_cycle % 10 == 0)
        nodes = spice_nodes();
    // the buildings of one city per snapshot (all cities within a second): the guest reconciles
    // what the incremental messages missed (a capture clears the ruins, ...)
    size_t bci = g_host_cities.empty() ? 0xFF : g_cycle % g_host_cities.size();
    std::vector<void*> blds;
    if (bci != 0xFF)
        for (void* bl : vec_of((uint8_t*)g_host_cities[bci] + kCityBuildings))
            if (type_for_noun(game::noun_id(bl)) && blds.size() < 32) {
                blds.push_back(bl);
                // BUG-0602 diagnostics: a building of the host's city that stands off its layout slot
                net::Vec3 bp{};
                float nd = 0;
                if (game::object_position(bl, bp) && g_n.host_offslot_logged < 12 && !g_host_offslot_seen.count(bl)) {
                    int in = slot_of(g_host_cities[bci], bl, bp, &nd, nullptr);
                    if (in < 0 || nd > 2.f) {
                        g_host_offslot_seen.insert(bl);
                        g_n.host_offslot_logged++;
                        H->log(M, SK_INFO, "civ: BUG-0602: host building %08X of city %u at (%.1f %.1f %.1f): slot %d, nearest slot %.1f m",
                               game::noun_id(bl), (unsigned)bci, bp.x, bp.y, bp.z, in, nd);
                    }
                }
            }
    struct Chunk {
        size_t c0 = 0, c1 = 0, n0 = 0, n1 = 0, v0 = 0, v1 = 0, bytes = kHead;
        bool bld = false;
    };
    std::vector<Chunk> ch(1);
    auto place = [&](size_t sz) -> Chunk& {
        if (ch.back().bytes + sz > kBudget)
            ch.emplace_back();
        ch.back().bytes += sz;
        return ch.back();
    };
    size_t ncity = std::min<size_t>(g_host_cities.size(), 250);
    for (size_t i = 0; i < ncity; i++) {
        Chunk& c = place(kCityBytes);
        if (c.c1 == c.c0)
            c.c0 = i;
        c.c1 = i + 1;
    }
    for (size_t i = 0; i < nodes.size() && i < 250; i++) {
        Chunk& c = place(kNodeBytes);
        if (c.n1 == c.n0)
            c.n0 = i;
        c.n1 = i + 1;
    }
    if (bci != 0xFF)
        place(1 + 17 * blds.size()).bld = true;
    for (size_t i = 0; i < vehicles.size(); i++) {
        Chunk& c = place(kVehBytes);
        if (c.v1 == c.v0)
            c.v0 = i;
        c.v1 = i + 1;
    }
    g_n.last_chunks = (uint32_t)ch.size();
    for (size_t k = 0; k < ch.size() && k < 255; k++) {
        const Chunk& c = ch[k];
        net::Writer w;
        w.u16(g_cycle);
        w.u8((uint8_t)k);
        w.u8((uint8_t)ch.size());
        w.u8((uint8_t)ncity);
        w.u8((uint8_t)c.c0);
        w.u8((uint8_t)(c.c1 - c.c0));
        for (size_t i = c.c0; i < c.c1; i++) {
            void* city = g_host_cities[i];
            net::Vec3 p = city_pos(city);
            w.f32(p.x); w.f32(p.y); w.f32(p.z);
            w.u32(pol_of(city));
            w.u8(owner_of_city(city));
            w.f32(F<float>(city, kCityCapture)); // military capture progress (the meter over the city)
            w.u32(F<uint32_t>(city, kCityCapture + 4));
        }
        // spice nodes and who owns them (once a second): claims happen on the host only
        w.u8((uint8_t)(c.n1 - c.n0));
        for (size_t i = c.n0; i < c.n1; i++) {
            w.f32(nodes[i].first.x); w.f32(nodes[i].first.y); w.f32(nodes[i].first.z);
            w.u32(nodes[i].second);
        }
        w.u8(c.bld ? (uint8_t)bci : 0xFF);
        if (c.bld) {
            w.u8((uint8_t)blds.size());
            for (void* bl : blds) {
                net::Vec3 bp;
                game::object_position(bl, bp);
                w.u32(game::noun_id(bl));
                w.f32(bp.x); w.f32(bp.y); w.f32(bp.z);
                w.u8(F<int>(bl, kBuildingState) == 2 ? 1 : 0);
            }
        }
        w.u8((uint8_t)(c.v1 - c.v0));
        for (size_t i = c.v0; i < c.v1; i++) {
            void* v = vehicles[i];
            HvRef x = hv_find(v); // (every vehicle of the list is in the table after the pass above)
            const HV hv = x ? *x.h : HV{};
            const uint16_t vid = x ? x.id : 0;
            net::Vec3 p = veh_pos(v);
            net::Quat q = veh_rot(v);
            w.u16(vid); w.u8(hv.owner); w.u32(pol_of(v));
            w.u8((uint8_t)F<int>(v, kVPurpose)); w.u8((uint8_t)F<int>(v, kVLoco));
            w.f32(p.x); w.f32(p.y); w.f32(p.z);
            w.f32(q.x); w.f32(q.y); w.f32(q.z); w.f32(q.w);
            w.f32(F<float>(v, kVHp));
        }
        w.u32(net::now_ms()); // BUG-0122: the host's time of the snapshot (velocity on the guest)
        // BUG-0787: the vehicles' top speeds (+0x218, the alarm boost included) after the time, in the same order: a
        // guest before 0.7.13 finds more than the time behind the vehicles and only loses the sender time
        for (size_t i = c.v0; i < c.v1; i++)
            w.f32(F<float>(vehicles[i], 0x218));
        g_n.max_chunk = std::max<uint32_t>(g_n.max_chunk, (uint32_t)w.size());
        for (auto& pl : C.sess->players())
            if (pl.id != 0)
                C.sess->send_unreliable_to(pl.id, net::MSG_CIV_STATE, w);
    }
    if (bci != 0xFF) {
        host_send_building_hp(bci, g_host_cities[bci], now); // BUG-0529
        host_send_turrets(bci, g_host_cities[bci], now);     // BUG-0484
    }
}

static const char* g_buy_fail = "";
void* host_buy(void* city, int loco)
{
    bool riv = civ_rivals::active();
    void* civ = (riv && city) ? F<void*>(city, kCityCiv) : player_civ();
    g_buy_fail = "no such city";
    if (!civ || !city)
        return nullptr;
    int purpose = F<int>(city, kCitySpecialty);
    g_buy_fail = "no vehicle slot left";
    if (thiscall<int>(a_SlotsLeft, civ, purpose) <= 0)
        return nullptr;
    float cost = thiscall<float>(a_VehicleCost, civ, purpose, loco);
    civ_wallet::PayAs pay(riv ? -1 : g_pending_owner); // BUG-1468: Together own-money; Rivals uses the player's own cCivilization
    g_buy_fail = (riv || pay.paying()) ? "not enough money of your own" : "not enough money";
    if (!civlogic::can_pay(thiscall<float>(a_GetMoney, civ), cost))
        return nullptr;
    g_buy_fail = "the city cannot build it";
    uint32_t* key = thiscall<uint32_t*>(a_DesignKey, civ, skg::cdecl_call<int>(a_DesignId, purpose, loco));
    if (!key)
        return nullptr;
    void* v = thiscall<void*>(a_SpawnVehicle, city, purpose, loco, key[0], key[1], key[2], 0);
    if (!v)
        return nullptr;
    thiscall<int>(a_PushEvent, v, 10);
    thiscall<void>(a_SpendMoney, civ, cost);
    if (riv)
        civ_rivals::host_mark_assign_dirty();
    return v;
}

static bool g_fix_build_orphan = true;
COOP_FIX("civ.build_orphan", "BUG-0491", g_fix_build_orphan,
         "civ: a building the mod makes for a full city is destroyed, not left in the world outside every slot");
// BUG-0613: a building the mod pays for (`coop.civ build`, the tests' build on either game) was paid with
// cCivilization::SpendMoney, which takes the money below 0 (the game's planner only offers what the money
// covers): the host's game showed -800 while every guest floors the shared total at 0 (shared.cpp), so the
// divergence detector's `Civilization:state:money` row came in every civ_coop run. On: a paid build the money
// does not cover is not built ("not enough money"), as the planner and a purchase (host_buy) do. Off: built and paid.
bool g_fix_build_money = true;
COOP_FIX("civ.build_money", "BUG-0613", g_fix_build_money,
         "civ: a building the mod pays for is not built when the money does not cover it (money never below 0)");
// build `noun` in `city` at the free slot nearest to `pos` (the game's planner sequence)
void* build_in_city(void* city, uint32_t noun, const net::Vec3& pos, bool pay)
{
    g_build_fail = "no free slot";
    uint32_t type = type_for_noun(noun);
    if (!type || !city)
        return nullptr;
    if (!building_model_ok(city, type, noun)) // BUG-0469: the building's design model is not in this game
        return nullptr;
    void* o = thiscall<void*>(a_CreateInstance, nm(), noun);
    void* b = o ? game::cast(o, kBuildingType) : nullptr;
    if (!b)
        return nullptr;
    thiscall<void>(a_SetBuildingModel, city, b, type);
    if (pay && g_fix_build_money) { // BUG-0613
        void* civ = civ_rivals::active() ? F<void*>(city, kCityCiv) : player_civ();
        if (civ && !civlogic::can_pay(thiscall<float>(a_GetMoney, civ), vcall<float>(b, 0x5C / 4))) {
            g_build_fail = "not enough money";
            if (a_DestroyInstance)
                thiscall<void>(a_DestroyInstance, nm(), o);
            return nullptr;
        }
    }
    vcall<void>(b, 0x54 / 4, city); // SetOwnerCity
    float p[3] = {pos.x, pos.y, pos.z};
    vcall<void>((uint8_t*)b + 0x34, 0x38 / 4, (const float*)p);
    if (!thiscall<bool>(a_SnapToSlot, city, b, 1)) {
        // no free slot: the building was made for nothing. It stayed in the world (in no city list and
        // no slot, nobody collects it, the next save keeps it) at the host's spot, over the slot of
        // another building: a pick in the planner faulted on it (BUG-0491). Off: left as it was.
        g_n.build_orphans++;
        if (g_fix_build_orphan && a_DestroyInstance)
            thiscall<void>(a_DestroyInstance, nm(), o);
        return nullptr;
    }
    thiscall<int>(a_AddBuilding, city, b, 0);
    if (pay) {
        if (void* civ = civ_rivals::active() ? F<void*>(city, kCityCiv) : player_civ()) {
            thiscall<void>(a_SpendMoney, civ, vcall<float>(b, 0x5C / 4));
            if (civ_rivals::active())
                civ_rivals::host_mark_assign_dirty();
        }
    }
    return b;
}

static void* resolve_target(const Target& t)
{
    if (t.kind == 1) {
        HvRef x = hv_by_id(t.id);
        return x && hv_live(x) ? x.v : nullptr; // (BUG-1053)
    }
    if (t.kind == 2)
        return host_city(t.id); // (BUG-1053)
    if (t.kind == 3 && t.noun)
        return game::find_object(t.noun, t.pos, 10.f);
    return nullptr;
}

// BUG-0899: Host-side friendly fire attack orders between different players' vehicles and cities.
struct CivFfOrder {
    uint8_t target_kind = 0; // 1 = vehicle id, 2 = host city index
    uint16_t target_id = 0;
    double next_move = 0, next_fire = 0;
};
static std::unordered_map<uint16_t, CivFfOrder> g_ff_orders;
LIFECYCLE_STATE("civ.ff_orders", lifecycle::kSession | lifecycle::kWorld, nullptr, [] { g_ff_orders.clear(); },
                []() -> size_t { return g_ff_orders.size(); });

void ff_clear_orders() { g_ff_orders.clear(); }
void ff_cancel_vehicle(uint16_t id) { g_ff_orders.erase(id); }

std::string ff_status()
{
    char b[160];
    snprintf(b, sizeof b, "civ_ff: on=%d fix=%d orders=%u active=%u hits=%u blocked=%u\n",
             (int)ff_on(), (int)g_fix_civ_ff, g_n.ff_orders, (unsigned)g_ff_orders.size(), g_n.ff_hits, g_n.ff_blocked);
    return b;
}

bool ff_try_order(uint16_t vid, void* v, uint8_t owner, void* target, int type)
{
    if (!v || !target || (type != 1 && type != 2)) {
        ff_cancel_vehicle(vid);
        return false;
    }
    uint8_t tk = 0;
    uint16_t tid = 0;
    uint8_t vic_owner = 0xFF;
    if (HvRef tv = hv_find(target)) {
        tk = 1;
        tid = tv.id;
        vic_owner = tv.h->owner;
    } else {
        int ci = host_index_of_city(target);
        if (ci >= 0) {
            tk = 2;
            tid = (uint16_t)ci;
            vic_owner = owner_of_city(target);
        }
    }
    if (!tk || vic_owner >= net::kMaxPlayers || vic_owner == owner) {
        ff_cancel_vehicle(vid);
        return false;
    }
    if (!civlogic::can_ff_order(ff_on(), owner, vic_owner, F<int>(v, kVPurpose), (uint8_t)net::kMaxPlayers)) {
        ff_cancel_vehicle(vid);
        return true; // refuse cross-player attack while FF is off (or non-military vehicle)
    }
    double now = H->time_seconds();
    g_ff_orders[vid] = {tk, tid, 0.0, now + 0.25};
    g_n.ff_orders++;
    return true;
}

void ff_host_tick(double now)
{
    if (g_ff_orders.empty())
        return;
    if (!ff_on() || !is_host() || !active()) {
        g_ff_orders.clear();
        return;
    }
    for (auto it = g_ff_orders.begin(); it != g_ff_orders.end();) {
        HvRef av = hv_by_id(it->first);
        if (!av || !hv_live(av) || F<uint8_t>(av.v, kVDead) || F<float>(av.v, kVHp) <= 0 ||
            F<int>(av.v, kVPurpose) != 0) {
            it = g_ff_orders.erase(it);
            continue;
        }
        net::Vec3 ap{}, tp{};
        if (!game::object_position(av.v, ap)) {
            it = g_ff_orders.erase(it);
            continue;
        }
        void* tveh = nullptr;
        void* thall = nullptr;
        uint8_t vo = 0xFF;
        float range = 45.f;
        if (it->second.target_kind == 1) {
            HvRef tv = hv_by_id(it->second.target_id);
            if (tv && hv_live(tv) && !F<uint8_t>(tv.v, kVDead) && F<float>(tv.v, kVHp) > 0 &&
                game::object_position(tv.v, tp)) {
                tveh = tv.v;
                vo = tv.h->owner;
            }
        } else if (it->second.target_kind == 2) {
            void* city = host_city(it->second.target_id);
            void* hall = city ? F<void*>(city, 0x320) : nullptr;
            if (city && hall && F<int>(hall, kBuildingState) != 2 && F<float>(hall, kBuildingState + 4) > 0 &&
                game::object_position(city, tp)) {
                thall = hall;
                vo = owner_of_city(city);
                range = 65.f;
            }
        }
        if ((!tveh && !thall) || !civlogic::can_ff_order(true, av.h->owner, vo, 0, (uint8_t)net::kMaxPlayers)) {
            it = g_ff_orders.erase(it);
            continue;
        }
        float d = net::dist(ap, tp);
        auto step = civlogic::ff_combat_step(d, range, now, it->second.next_move, it->second.next_fire);
        if (step == civlogic::FfStep::Move) {
            o_SetMoveOrder(av.v, tp.x, tp.y, tp.z, 0);
            thiscall<void>(a_OrderStamp, av.v, 0);
            it->second.next_move = now + 0.6;
        } else if (step == civlogic::FfStep::Fire) {
            if (tveh)
                ff_deal_vehicle_hit(tveh, av.v, 35.f);
            else
                ff_deal_hall_hit(thall, av.v, 40.f);
            it->second.next_fire = now + 1.5;
        }
        ++it;
    }
}

void host_command(uint8_t from, net::Reader& r)
{
    uint8_t kind = r.u8();

    // BUG-0325 diagnostics: why a vehicle of a command is not the sender's (gone from the host's game, dead,
    // or another owner: a gift or a capture in between), logged with the rejection
    char why[160] = "";
    auto own_vehicle = [&](uint16_t id) -> void* {
        HvRef x = hv_by_id(id);
        if (!x) {
            snprintf(why, sizeof why, " (vehicle %u: not in the host's game)", (unsigned)id);
            return nullptr;
        }
        if (!hv_live(x)) { // BUG-1053: removed by the host's game since the last cycle
            snprintf(why, sizeof why, " (vehicle %u: gone from the host's game since the last cycle)", (unsigned)id);
            return nullptr;
        }
        if (!civlogic::owns(x.h->owner, from)) {
            snprintf(why, sizeof why, " (vehicle %u: owner %u%s)", (unsigned)id, (unsigned)x.h->owner,
                     F<uint8_t>(x.v, kVDead) ? ", dead" : "");
            return nullptr;
        }
        return x.v;
    };
    auto reject = [&] {
        if (from != C.sess->my_id()) {
            g_n.cmds_rejected++;
            if (g_n.cmds_rejected <= 20) // BUG-0325 diagnostics: which command, from whom, why
                H->log(M, SK_INFO, "civ: rejected command kind %u from player %u%s", (unsigned)kind, (unsigned)from, why);
        }
    };
    // BUG-1478 (CIV-5): a superweapon, a city purchase or a capture of a guest that plays another Civ world of its own is
    // not the host's world's (its target, its city index are of that world): dropped
    if ((kind == 5 || kind == 6 || kind == 7) && from != C.sess->my_id() &&
        !civlogic::host_takes_world_cmd(g_local_play_gate_fix, own_world::peer_elsewhere(from))) {
        snprintf(why, sizeof why, " (the player plays another Civ world)");
        g_n.world_cmds_dropped++;
        return reject();
    }
    if (kind == 5) { // a guest wants to fire a superweapon: everybody votes
        host_superweapon_request(from, r);
        return;
    }
    if (kind == CMD_GIVE) {
        uint8_t what = r.u8(), to = r.u8(), n = r.u8();
        if (!r.ok() || !net::is_player(*C.sess, to))
            return reject();
        int given = 0;
        for (int i = 0; i < n && r.ok(); i++) {
            uint16_t id = r.u16();
            if (what == 0) {
                if (void* v = own_vehicle(id)) {
                    hv_find(v).h->owner = to; // (own_vehicle found its entry)
                    if (g_store_fix && !store::key_of(to).empty())
                        g_veh_claim[v] = store::key_of(to); // E1: the receiver's from now on
                    given++;
                }
            } else if (void* city = host_city(id)) { // (BUG-1053)
                if (owner_of_city(city) == from) {
                    g_city_owner[city] = to;
                    g_city_gifted.insert(city);
                    if (g_store_fix && !store::key_of(to).empty())
                        g_city_claim[city] = store::key_of(to);
                    given++;
                }
            }
        }
        g_n.gifts += given;
        if (given && C.notice)
            C.notice(C.sess->name_of(from) + " gave " +
                     (what == 0 ? std::to_string(given) + (given == 1 ? " vehicle" : " vehicles") : std::string("a city")) +
                     " to " + C.sess->name_of(to));
        if (!given)
            return reject();
        if (from != C.sess->my_id()) // the host's own gifts are not guest commands
            g_n.cmds_done++;
        return;
    }
    if (kind == CMD_ORDER) {
        uint16_t id = r.u16();
        int type = (int)r.u32(), flag = (int)r.u32();
        Target t = read_target(r);
        void* v = own_vehicle(id);
        void* target = resolve_target(t);
        if (v && t.kind && !target) // BUG-0325 diagnostics: the order's target is not in the host's game
            snprintf(why, sizeof why, " (vehicle %u: target kind %u id %u noun %08X at (%.0f %.0f %.0f) not in the host's game)",
                     (unsigned)id, (unsigned)t.kind, (unsigned)t.id, t.noun, t.pos.x, t.pos.y, t.pos.z);
        if (r.ok() && v && t.kind == 3 && t.noun == kCreatureNoun && !target && g_fix_creature_target) {
            // BUG-0970: wild creatures are not synced in Civ (each game runs its own), so the guest's creature is
            // rarely within 10 m of one of the host's: attack the host's creature nearest to that place (within
            // kCreatureReach), else drive there (the order a right-click on the ground gives)
            target = game::find_object(t.noun, t.pos, kCreatureReach);
            if (!target) {
                g_n.creature_moves++;
                o_SetMoveOrder(v, t.pos.x, t.pos.y, t.pos.z, flag);
                thiscall<void>(a_OrderStamp, v, 0);
                g_n.cmds_done++;
                return;
            }
            g_n.creature_near++;
        }
        if (!r.ok() || !v || (t.kind && !target))
            return reject();
        static int s_obj_logged;
        net::Vec3 tp;
        if (t.kind == 3 && s_obj_logged++ < 20 && game::object_position(target, tp)) // BUG-0970 diagnostics
            H->log(M, SK_INFO, "civ: order %d of vehicle %u: noun %08X at (%.0f %.0f %.0f) is the host's at %.1f m", type,
                   (unsigned)id, t.noun, t.pos.x, t.pos.y, t.pos.z,
                   std::sqrt((tp.x - t.pos.x) * (tp.x - t.pos.x) + (tp.y - t.pos.y) * (tp.y - t.pos.y) +
                             (tp.z - t.pos.z) * (tp.z - t.pos.z)));
        if (civ_rivals::active() && (type == 1 || type == 2 || type == 3) &&
            civ_rivals::block_attack_order(from, target))
            return reject();
        if (civ_rivals::active() || !ff_try_order(id, v, from, target, type)) {
            o_SetOrder(v, target, type, flag);
            thiscall<void>(a_OrderStamp, v, 0);
        }
    } else if (kind == CMD_MOVE) {
        uint16_t id = r.u16();
        float x = r.f32(), y = r.f32(), z = r.f32();
        int flag = (int)r.u32();
        void* v = own_vehicle(id);
        if (!r.ok() || !v)
            return reject();
        ff_cancel_vehicle(id);
        o_SetMoveOrder(v, x, y, z, flag);
        thiscall<void>(a_OrderStamp, v, 0);
    } else if (kind == CMD_BUY) {
        uint8_t ci = r.u8(), loco = r.u8();
        void* city = host_city(ci); // (BUG-1053)
        uint8_t co = city ? owner_of_city(city) : 0xFF;
        if (!r.ok() || !city || (co != from && co != kShared) || loco > 2)
            return reject();
        g_pending_owner = from;
        void* v = host_buy(city, loco);
        if (v && !hv_find(v))
            hv_add(v, from); // BUG-0154 (this one could hand out 0)
        g_pending_owner = -1;
        if (v)
            own_designs_now(); // BUG-1274: the buyer's own design at once
        if (!v) {
            // the guest's button was enabled (its own count of vehicles may differ for a moment):
            // tell that player why nothing comes
            net::Writer nw;
            nw.str8(std::string("no vehicle: ") + g_buy_fail);
            C.sess->send_reliable_to(from, net::MSG_NOTICE, nw);
            g_n.buys_refused++;
            return reject();
        }
    } else if (kind == CMD_BUILD) {
        uint8_t ci = r.u8();
        uint32_t noun = r.u32();
        net::Vec3 p;
        p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
        void* city = host_city(ci); // (BUG-1053)
        uint8_t co = city ? owner_of_city(city) : 0xFF;
        float paid = r.ok() && r.left() >= 4 ? r.f32() : 0.f; // BUG-0807 (an older guest: nothing)
        if (!r.ok())
            return reject();
        if (!city || (co != from && co != kShared)) {
            refund_build(from, paid, !city ? "the city is not in the host's game" : "the city belongs to another player now");
            return reject();
        }
        if (planner_holds(city)) {
            g_held_builds.push_back({from, city, noun, p, paid});
            g_n.planner_held++;
            g_n.cmds_done++;
            return;
        }
        g_build_from = from; // the guest already has it
        void* b = build_in_city(city, noun, p, false); // the guest paid in its planner (shared money)
        g_build_from = -1;
        if (!b) {
            if (civ_rivals::active())
                civ_rivals::host_mark_assign_dirty(); // resync guest's uncharged wealth
            else
                refund_build(from, paid, g_build_fail);
            return reject();
        }
        if (civ_rivals::active() && paid > 0.0f)
            civ_rivals::host_charge_player(from, paid);
    } else if (kind == kCmdSpecialty) { // BUG-0982: a guest's answer to the specialty question of its city
        host_specialty_answer(from, r);
        return;
    } else if (kind == 6 /* CMD_BUYCITY, BUG-0877 */ || kind == 7 /* CMD_CAPTURE, BUG-0878 */) {
        uint8_t ci = r.u8();
        float price = kind == 6 ? r.f32() : 0.f;
        int type = kind == 7 ? r.u8() : 2;
        void* city = host_city(ci); // (BUG-1053)
        bool riv = civ_rivals::active();
        void* pc = riv ? civ_rivals::civ_of_player(from) : player_civ();
        bool on = kind == 6 ? g_fix_buy_city && o_CityBuy : g_fix_away_capture;
        if (!r.ok() || !on || !city || !pc || pol_of(city) == pol_of(pc) || !std::isfinite(price) || price < 0 ||
            type < 0 || type > 2 || (riv && civ_rivals::block_capture(city, pol_of(pc)))) {
            snprintf(why, sizeof why, " (city %u: %s)", (unsigned)ci, !city ? "not in the host's game" : "refused");
            return reject();
        }
        bool refused;
        {
            civ_wallet::PayAs pay((kind == 6 && !riv) ? from : -1); // BUG-1468: Together own-money; Rivals uses pc directly
            float money = thiscall<float>(a_GetMoney, pc);
            g_capture_for = from;
            if (kind == 6)
                o_CityBuy(city, pol_of(pc), price);
            else
                h_Capture(city, pol_of(pc), type, 0, 0, 0);
            g_capture_for = -1;
            // (the owner changes when the capture's cinematic ends: a purchase that took spent the money)
            refused = kind == 6 && pol_of(city) != pol_of(pc) && thiscall<float>(a_GetMoney, pc) >= money;
        }
        if (refused) {
            net::Writer nw;
            nw.str8("the city was not bought: not enough money or no trade route in the host's game");
            C.sess->send_reliable_to(from, net::MSG_NOTICE, nw);
            g_n.city_buys_refused++;
            return reject();
        }
        g_n.city_buys_done += kind == 6;
        H->log(M, SK_INFO, "civ: city %u %s for player %u in this game", (unsigned)ci, kind == 6 ? "bought" : "captured",
               (unsigned)from);
    } else
        return reject();
    g_n.cmds_done++;
}


} // namespace civ
