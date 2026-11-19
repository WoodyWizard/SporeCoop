// The species line (WP-0 of docs/plan-0.7.16.md, BUG-1270): species_line.h, species_line_core.h,
// docs/01-architecture.md#arch-species-line.
//
// Who knows what:
// * the owner (a guest) keeps its own row (g_own: its creature, outfits and designs by the keys of its own game) and
//   the bundles of its outfits and designs; it registers them under their shared keys itself, sends the bundles and
//   the row to the host (after the host's kStored answer of this session: a stored row is taken first)
// * the host keeps the table (g_rows) of every guest, registers the bundles, relays them to the other guests, sends
//   every row (a Ref only with a shared key its game has, species_line_core.h resolved()) to everybody and both to a
//   newcomer; it keeps every row in its per-player store (store.h, record species.line v1) and offers it back at a
//   join (kStored)
// * every game resolves each row against what it has (key(), design(): the keys registered here) and tells the
//   packages of a change (on_change)
#include "species_line.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cstdio>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "fixes.h"
#include "game.h"
#include "lifecycle.h"
#include "modes.h"
#include "savestrip.h"
#include "net/protocol.h"
#include "net/validate.h"
#include "player_life.h"
#include "store.h"
#include "trace.h"

namespace species {

static const SkHost* H;
static SkMod* M;
static net::Session* S;

// `coop.fix species.line off`: no line (every key() {}, nothing sent, nothing stored): 0.7.15, every package's
// player then shows the world's own species (the packages' FAIL twins can use it)
static bool g_line_fix = true;
COOP_FIX("species.line", "BUG-1270", g_line_fix, "species: every player's own species line per stage (tribe outfit, Civ outfit, Civ designs) is kept and sent to every game");

// ---- the table (every game: as the host sent it; the host: its own) ----
static std::map<uint8_t, Row> g_rows;
// ---- the line creations registered here: shared instance -> key ----
static std::map<uint32_t, Key> g_reg;
static std::set<uint32_t> g_reg_failed; // refused here (parts this game lacks): not tried again
// ---- the owner (guest) ----
static Row g_own;                                       // by the keys of this game
static std::map<uint32_t, std::vector<uint8_t>> g_own_bundles; // shared -> our bundle
static bool g_stored_seen = false;                      // the host answered kStored in this session
static std::vector<uint8_t> g_own_sent;                 // the row last sent
static bool g_own_sent_any = false;
static std::set<uint32_t> g_own_bundles_sent;           // shared keys sent to the host in this session
static double g_own_next = 0;
// ---- the host ----
struct HostBundle {
    uint8_t owner = 0;
    std::vector<uint8_t> data;
};
static std::map<uint32_t, HostBundle> g_bundles;          // shared -> the owner's bundle (for newcomers)
static std::map<uint8_t, std::vector<uint8_t>> g_row_sent; // the resolved row last broadcast per player
static std::set<uint8_t> g_offered;                       // guests that got their kStored answer
// ---- receiving bundles ----
static std::map<uint8_t, net::ChunkRx> g_rx; // per sender: one bundle at a time (reliable, in order)
// the host: line creations a guest had registered here in this session (a cap: each is a package on the host's disk)
static std::map<uint8_t, unsigned> g_reg_by;
constexpr unsigned kMaxRegPerPlayer = 3 * (kMaxDesigns + 2);
static std::map<uint8_t, double> g_seen_at; // the host: when a guest was first seen without its kStored answer
// ---- what every game answers now, per player ----
struct Resolved {
    Key k[(int)Stage::Count];
    std::map<uint32_t, Key> designs;
    bool operator==(const Resolved& o) const
    {
        for (int i = 0; i < (int)Stage::Count; i++)
            if (!(k[i] == o.k[i]))
                return false;
        return designs == o.designs;
    }
    bool empty() const
    {
        for (auto& x : k)
            if (x.instance)
                return false;
        return designs.empty();
    }
};
static std::map<uint8_t, Resolved> g_res;
static std::map<uint8_t, uint32_t> g_serial;
static double g_res_next = 0;

struct Sub {
    const char* name;
    ChangeFn fn;
};
static std::vector<Sub>& subs()
{
    static std::vector<Sub> v;
    return v;
}

static unsigned g_rows_in, g_rows_out, g_rows_refused, g_bundles_in, g_bundles_out, g_bundles_dropped, g_stored_out,
    g_stored_taken, g_changes, g_store_puts, g_sets, g_sets_refused;

static bool is_host() { return S && S->role() == net::Role::Host; }
static bool is_guest() { return S && S->role() == net::Role::Client && S->connected(); }
static bool active() { return g_line_fix && S && S->connected(); }

bool own_mode() { return !creations::one_creature(); }

static std::string hx(uint32_t v)
{
    char b[12];
    snprintf(b, sizeof b, "%08X", v);
    return b;
}
static Key key_of_ref(const Ref& f) { return {f.own, f.type, f.group}; }

// ---- the keys this game has ----
static Key registered(uint32_t shared)
{
    auto it = g_reg.find(shared);
    return it != g_reg.end() && creations::key_known(it->second) ? it->second : Key{};
}
// a player's creature (cell) as creations has it here, when it is its own (species_line_core.h creation_counts: the
// shared copy 0x7C......; for the cell also a premade every game has), not the world's own key
static Key creation_here(uint8_t pid, creations::Kind k)
{
    Key c = creations::player_key(pid, k);
    if (!c.instance)
        return {};
    Key host = creations::player_key(0, k);
    bool hosts = c == host || c == creations::player_orig(0, k);
    return creation_counts(k == creations::kCell ? Stage::Cell : Stage::Creature, c.instance, creations::key_known(c), hosts)
               ? c
               : Key{};
}

static Resolved resolve(uint8_t pid)
{
    Resolved r;
    if (!active() || pid == 0 || !own_mode())
        return r;
    r.k[(int)Stage::Cell] = creation_here(pid, creations::kCell);
    auto it = g_rows.find(pid);
    if (it == g_rows.end())
        return r; // no row (a player of an older version, or not sent yet): the world's own from Creature on
    const Row& row = it->second;
    Key creature = creation_here(pid, creations::kCreature);
    auto have = [&](Source s) {
        return s == Source::Creature ? creature.instance != 0
               : s == Source::Tribe  ? registered(row.tribe.shared).instance != 0
                                     : registered(row.civ.shared).instance != 0;
    };
    auto key_for = [&](Source s) {
        return s == Source::Creature ? creature
               : s == Source::Tribe  ? registered(row.tribe.shared)
               : s == Source::Civ    ? registered(row.civ.shared)
                                     : Key{};
    };
    for (Stage s : {Stage::Creature, Stage::Tribe, Stage::Civ})
        r.k[(int)s] = key_for(pick(row, s, have));
    for (auto& [id, f] : row.designs)
        if (Key d = f.shared ? registered(f.shared) : Key{}; d.instance)
            r.designs[id] = d;
    return r;
}

Key key(uint8_t pid, Stage s)
{
    if ((int)s >= (int)Stage::Count)
        return {};
    auto it = g_res.find(pid);
    return it == g_res.end() ? Key{} : it->second.k[(int)s];
}
Key design(uint8_t pid, uint32_t design_id)
{
    auto it = g_res.find(pid);
    if (it == g_res.end())
        return {};
    auto d = it->second.designs.find(design_id);
    return d == it->second.designs.end() ? Key{} : d->second;
}
std::vector<uint32_t> design_ids(uint8_t pid)
{
    std::vector<uint32_t> v;
    auto it = g_res.find(pid);
    if (it != g_res.end())
        for (auto& [id, k] : it->second.designs)
            v.push_back(id);
    return v;
}
uint32_t serial(uint8_t pid)
{
    auto it = g_serial.find(pid);
    return it == g_serial.end() ? 0 : it->second;
}
std::vector<uint8_t> players()
{
    std::vector<uint8_t> v;
    for (auto& [pid, r] : g_res)
        if (!r.empty())
            v.push_back(pid);
    return v;
}
// every key that was a line's on this game in this process (BUG-1315: a herd or a member of a player who left, an outfit
// changed: the save still must not name it); shared instances, never a key of the host's own
static std::set<std::pair<uint32_t, uint32_t>> g_was; // {instance, group}

bool is_line_key(const Key& k)
{
    if (!k.instance)
        return false;
    if (is_shared(k.instance) && g_reg.count(k.instance))
        return true;
    if (g_was.count({k.instance, k.group}))
        return true;
    for (auto& [pid, r] : g_res) {
        for (auto& x : r.k)
            if (x == k)
                return true;
        for (auto& [id, d] : r.designs)
            if (d == k)
                return true;
    }
    return false;
}

// ---- BUG-1315: the save's key filter ----
static WorldFn g_world[(int)Stage::Count];
bool set_world_species(Stage s, WorldFn fn)
{
    if ((int)s < (int)Stage::Count)
        g_world[(int)s] = fn;
    return true;
}
constexpr uint32_t kCreatureType = 0x2B978C46u;
static uint32_t g_save_rekeyed, g_save_unmapped;
// a creature key of a line in a save stream: the world's species of the mode (the tribe's slot in Tribe, the
// civilization's in Civ, the host's creature elsewhere). A design key (a building, a vehicle) keeps its key here:
// civ_own_designs strips the objects that show one (the culture set's key per design id)
static bool save_key_filter(uint32_t k[3])
{
    if (!k[0] || k[1] != kCreatureType || !is_line_key({k[0], k[1], k[2]}))
        return false;
    uint32_t mode = game::mode_id();
    WorldFn fn = mode == modes::kTribe ? g_world[(int)Stage::Tribe] : mode == modes::kCiv ? g_world[(int)Stage::Civ] : nullptr;
    Key w = fn ? fn() : Key{};
    if (!w.instance || is_line_key(w)) {
        void* a = game::avatar();
        game::Key gk = a ? game::species_key(a) : game::Key{0, 0, 0};
        Key ak{gk.instance, gk.type, gk.group};
        w = ak.instance && !is_line_key(ak) ? ak : creations::player_key(0, creations::kCreature);
    }
    if (!w.instance || is_line_key(w)) { // (nothing of the host's known: the key of none, the game's default on load)
        g_save_unmapped++;
        w = Key{};
    }
    g_save_rekeyed++;
    k[0] = w.instance, k[1] = w.instance ? w.type : 0, k[2] = w.instance ? w.group : 0;
    return true;
}

bool on_change(const char* name, ChangeFn fn)
{
    subs().push_back({name, fn});
    return true;
}

// ---- the owner's own line ----
// this game's own creature on its own game: an own creation (My Creations), else 0 (the world's own)
static uint32_t own_creature()
{
    creations::Option o = creations::mine(creations::kCreature);
    if (!o.key.instance)
        return 0;
    for (auto& c : creations::options(creations::kCreature))
        if (c.key == o.key)
            return o.key.instance;
    return 0;
}

// BUG-1297 (WP-S2a): the own key as this game builds it. A guest's own creature under a key of the host's world
// (BUG-0278: "Save New" of an edited host creation keeps the host's key, common after BUG-1255's landfall editor made
// from the host's creature): the host's world package answers that key here at priority 2000, so an own editor
// started from the raw My Creations key opened on the host's body. On: creations' remapped key (own_key_here, the
// shared copy of our own records); an outfit under such a key: the line's registered copy (0x7B......).
// `coop.fix species.own_key_here off`: the raw keys (0.7.16 before WP-S2).
static bool g_own_here_fix = true;
COOP_FIX("species.own_key_here", "BUG-1297", g_own_here_fix,
         "species: a guest's own Tribe and Civ outfitters open on its own creature also when its key is one of the host's world");
static Key own_ref_here(const Ref& f)
{
    Key k = key_of_ref(f);
    if (g_own_here_fix && creations::host_shadows(creations::kCreature, k))
        if (Key r = registered(f.shared); r.instance)
            return r;
    return k;
}
Key own_key(Stage s)
{
    switch (s) {
    case Stage::Cell: return creations::mine(creations::kCell).key;
    case Stage::Creature:
        return !g_own.creature  ? Key{}
               : g_own_here_fix ? creations::own_here(creations::kCreature)
                                : creations::mine(creations::kCreature).key;
    case Stage::Tribe: return tribe_counts(g_own) ? own_ref_here(g_own.tribe) : own_key(Stage::Creature);
    case Stage::Civ: return civ_counts(g_own) ? own_ref_here(g_own.civ) : own_key(Stage::Tribe);
    default: return {};
    }
}
Key own_design(uint32_t design_id)
{
    auto it = g_own.designs.find(design_id);
    return it == g_own.designs.end() ? Key{} : key_of_ref(it->second);
}

bool own_ready() { return g_line_fix && is_guest() && own_mode() && g_stored_seen; }
bool own_waiting() { return g_line_fix && is_guest() && !g_stored_seen; }
bool host_took(const Key& own)
{
    if (!own.instance || !S || !is_guest())
        return false;
    auto it = g_rows.find(S->my_id());
    if (it == g_rows.end())
        return false;
    auto has = [&](const Ref& f) { return f.set() && f.own == own.instance && f.shared; };
    if (has(it->second.tribe) || has(it->second.civ))
        return true;
    for (auto& [id, f] : it->second.designs)
        if (has(f))
            return true;
    return false;
}

// the bundle of an own creation, registered here under its shared key; 0: no records here
static uint32_t own_pack(const Key& k)
{
    std::vector<uint8_t> data;
    uint32_t content = 0;
    if (!creations::line_pack(k, data, content) || data.size() > kMaxBundle)
        return 0;
    uint32_t shared = shared_instance(content);
    Key reg = creations::line_register(data, shared, "our own line creation " + hx(k.instance));
    if (!reg.instance)
        return 0;
    g_reg[shared] = reg;
    g_own_bundles[shared] = std::move(data);
    return shared;
}

static bool may_set()
{
    if (!g_line_fix || !is_guest() || !own_mode()) {
        g_sets_refused++;
        return false;
    }
    return true;
}
static bool own_ref(const Key& k, Ref& out, bool civ)
{
    if (!k.instance) {
        out = {};
        return true;
    }
    uint32_t shared = own_pack(k);
    if (!shared) {
        g_sets_refused++;
        H->log(M, SK_WARN, "species: %08X/%08X has no model on this game: not taken", k.instance, k.type);
        return false;
    }
    g_own.creature = own_creature();
    out = civ ? civ_ref(g_own, k.instance, k.type, k.group, shared) : tribe_ref(g_own, k.instance, k.type, k.group, shared);
    return true;
}
bool set_tribe_outfit(const Key& k)
{
    if (!may_set())
        return false;
    Ref f;
    if (!own_ref(k, f, false))
        return false;
    g_own.tribe = f;
    g_own_next = 0;
    g_sets++;
    H->log(M, SK_INFO, "species: our tribe outfit is %08X (shared %08X, made for %08X)", f.own, f.shared, f.made_for);
    return true;
}
bool set_civ_outfit(const Key& k)
{
    if (!may_set())
        return false;
    Ref f;
    if (!own_ref(k, f, true))
        return false;
    g_own.civ = f;
    g_own_next = 0;
    g_sets++;
    H->log(M, SK_INFO, "species: our Civ outfit is %08X (shared %08X, made for %08X)", f.own, f.shared, f.made_for);
    return true;
}
bool set_design(uint32_t design_id, const Key& k)
{
    if (!design_id || !may_set())
        return false;
    if (!k.instance) {
        g_own.designs.erase(design_id);
    } else {
        if (!g_own.designs.count(design_id) && g_own.designs.size() >= kMaxDesigns) {
            g_sets_refused++;
            return false;
        }
        Ref f;
        if (!own_ref(k, f, false))
            return false;
        f.made_for = 0;
        g_own.designs[design_id] = f;
    }
    g_own_next = 0;
    g_sets++;
    H->log(M, SK_INFO, "species: our design %08X is %08X", design_id, k.instance);
    return true;
}

// ---- sending ----
static void send_bundle(int to, uint8_t owner, uint32_t shared, const std::vector<uint8_t>& data)
{
    for (size_t off = 0; off < data.size() || off == 0; off += kChunk) {
        net::Writer w;
        w.u8(kBundle);
        w.u8(owner);
        w.u32(shared);
        w.u32((uint32_t)data.size());
        w.u32((uint32_t)off);
        size_t n = std::min(kChunk, data.size() - off);
        w.u16((uint16_t)n);
        w.raw(data.data() + off, n);
        if (to < 0)
            S->broadcast_reliable(net::MSG_SPECIES_LINE, w);
        else
            S->send_reliable_to((uint8_t)to, net::MSG_SPECIES_LINE, w);
        if (data.empty())
            break;
    }
    g_bundles_out++;
}
static void send_row(int to, uint8_t kind, uint8_t pid, const std::vector<uint8_t>& bytes)
{
    net::Writer w;
    w.u8(kind);
    w.u8(pid);
    w.u16((uint16_t)bytes.size());
    w.raw(bytes.data(), bytes.size());
    if (to < 0)
        S->broadcast_reliable(net::MSG_SPECIES_LINE, w);
    else
        S->send_reliable_to((uint8_t)to, net::MSG_SPECIES_LINE, w);
}

// the owner: its bundles, then its row (after the host's kStored answer)
static void own_tick(double now)
{
    if (!is_guest() || !g_stored_seen || now < g_own_next)
        return;
    g_own_next = now + 1.0;
    g_own.creature = own_creature();
    auto ensure = [&](Ref& f) {
        if (!f.set())
            return;
        if (!g_own_bundles.count(f.shared)) { // (a stored row: its creation packed again from this game)
            uint32_t sh = own_pack(key_of_ref(f));
            if (!sh) {
                H->log(M, SK_WARN, "species: our line creation %08X is not on this game any more: dropped", f.own);
                f = {};
                return;
            }
            f.shared = sh;
        }
        if (!g_own_bundles_sent.count(f.shared)) {
            send_bundle(0, S->my_id(), f.shared, g_own_bundles[f.shared]);
            g_own_bundles_sent.insert(f.shared);
        }
    };
    ensure(g_own.tribe);
    ensure(g_own.civ);
    for (auto it = g_own.designs.begin(); it != g_own.designs.end();) {
        ensure(it->second);
        it = it->second.set() ? std::next(it) : g_own.designs.erase(it);
    }
    std::vector<uint8_t> bytes = encode(g_own);
    if (g_own_sent_any && bytes == g_own_sent)
        return;
    send_row(0, kRow, S->my_id(), bytes);
    g_own_sent = bytes;
    g_own_sent_any = true;
    g_rows_out++;
}

// the host: a row changed: into the store (its memory; the file is written after the game's next save)
static void store_put(uint8_t pid)
{
    pstore::Store* s = store::world();
    std::string k = store::key_of(pid);
    if (!s || k.empty() || k == "host")
        return;
    auto it = g_rows.find(pid);
    std::vector<uint8_t> d = it == g_rows.end() || it->second.empty() ? std::vector<uint8_t>{} : encode(it->second);
    if (s->put(k, S->name_of(pid), kStoreFeature, kStoreVersion, std::move(d), store::now_unix(), true))
        g_store_puts++;
}
static void store_save(pstore::Store& s, int64_t now)
{
    if (!g_line_fix || !is_host() || &s != store::world())
        return;
    for (auto& [pid, row] : g_rows) {
        std::string k = store::key_of(pid);
        if (k.empty() || k == "host")
            continue;
        if (s.put(k, S->name_of(pid), kStoreFeature, kStoreVersion, row.empty() ? std::vector<uint8_t>{} : encode(row),
                  now, true))
            g_store_puts++;
    }
}
static bool g_store_reg = store::add({kStoreFeature, store_save, [](const std::string&, const std::string& to) {
                                          // the host's word: the stored row of `to` is offered to that player again
                                          if (S)
                                              for (auto& p : S->players())
                                                  if (p.id != 0 && store::key_of(p.id) == to)
                                                      g_offered.erase(p.id);
                                      }});

static bool host_known(uint32_t shared) { return registered(shared).instance != 0; }

// BUG-1312: a guest that plays the world's own species again (its world record forgotten, "the world's own" chosen)
// sends an empty row; the host erased it and sent nothing, so the guest's own game and the other guests kept the old
// row and drew its members in its old creature while the host drew the tribe's species. On: the host sends the empty
// row to everybody once. `coop.fix species.row_cleared off`: not sent (0.7.16 before the fix).
static bool g_row_cleared_fix = true;
COOP_FIX("species.row_cleared", "BUG-1312", g_row_cleared_fix,
         "species: a guest that plays the world's own species again is drawn so on every game, its own included");
static unsigned g_rows_cleared_out;

static void host_tick()
{
    if (!is_host())
        return;
    std::set<uint8_t> here;
    for (auto& p : S->players())
        if (p.id != 0)
            here.insert(p.id);
    // a guest's stored row, once per session, as soon as the host's world (its store) is open
    // (no store after 10 s: a world without one, or no key for the player: the answer is "nothing kept")
    double now = H->time_seconds();
    pstore::Store* st = store::world();
    for (uint8_t pid : here) {
        if (g_offered.count(pid))
            continue;
        double& seen = g_seen_at.try_emplace(pid, now).first->second;
        std::string k = store::key_of(pid);
        if ((!st || k.empty()) && now - seen < 10.0)
            continue;
        std::vector<uint8_t> bytes;
        Row row;
        if (const pstore::Record* rec = st && !k.empty() ? st->get(k, kStoreFeature) : nullptr)
            if (rec->ver == kStoreVersion && decode(rec->data, row))
                bytes = encode(row);
        send_row(pid, kStored, pid, bytes);
        g_offered.insert(pid);
        g_stored_out++;
        H->log(M, SK_INFO, "species: player %u's stored line offered (%u bytes)", pid, (unsigned)bytes.size());
    }
    // every row as the host's game has it, to everybody, when it changed
    for (auto& [pid, row] : g_rows) {
        if (!here.count(pid))
            continue;
        std::vector<uint8_t> bytes = encode(resolved(row, host_known));
        auto it = g_row_sent.find(pid);
        if (it != g_row_sent.end() && it->second == bytes)
            continue;
        send_row(-1, kRow, pid, bytes);
        g_row_sent[pid] = bytes;
        g_rows_out++;
    }
    // BUG-1312: a row the guest emptied (it plays the world's own species again) was erased here and never sent, so
    // every other game (the owner's own included) kept the old row and drew its members in the old line
    if (g_row_cleared_fix)
        for (auto& [pid, sent] : g_row_sent) {
            if (sent.empty() || g_rows.count(pid) || !here.count(pid))
                continue;
            send_row(-1, kRow, pid, {});
            sent.clear();
            g_rows_out++;
            g_rows_cleared_out++;
        }
}

// lifecycle-exempt: sends messages (the rows and their creations to a newcomer), called by session_glue's player_join
void on_player_join(uint8_t id)
{
    if (!g_line_fix || !is_host() || id == 0)
        return;
    // the bundles of every row first (reliable, in order), then the rows: the newcomer can resolve each key it gets
    std::set<uint32_t> sent;
    for (auto& [pid, row] : g_rows) {
        if (pid == id)
            continue;
        auto out = [&](const Ref& f) {
            auto b = f.shared ? g_bundles.find(f.shared) : g_bundles.end();
            if (b != g_bundles.end() && sent.insert(f.shared).second)
                send_bundle(id, b->second.owner, f.shared, b->second.data);
        };
        out(row.tribe);
        out(row.civ);
        for (auto& [d, f] : row.designs)
            out(f);
        send_row(id, kRow, pid, encode(resolved(row, host_known)));
    }
}

// the host keeps only the bundles some row names
static void host_prune()
{
    std::set<uint32_t> used;
    for (auto& [pid, row] : g_rows) {
        used.insert(row.tribe.shared);
        used.insert(row.civ.shared);
        for (auto& [d, f] : row.designs)
            used.insert(f.shared);
    }
    for (auto it = g_bundles.begin(); it != g_bundles.end();)
        it = used.count(it->first) ? std::next(it) : g_bundles.erase(it);
}

// ---- receiving ----
static void take_bundle(uint8_t from, net::Reader& r)
{
    uint8_t owner = r.u8();
    uint32_t shared = r.u32(), total = r.u32(), off = r.u32();
    uint16_t n = r.u16();
    if (!r.ok() || n > r.left() || !is_shared(shared))
        return;
    if (is_host() && owner != from) { // a guest sends only its own
        g_bundles_dropped++;
        return;
    }
    if (!is_host() && from != 0)
        return;
    auto& rx = g_rx[from];
    net::Chunk got = net::chunk_add(rx, shared, total, off, r.cur(), n, kChunk, kMaxBundle);
    if (got == net::Chunk::Drop) {
        g_bundles_dropped++;
        g_rx.erase(from);
        return;
    }
    if (got == net::Chunk::Partial)
        return;
    std::vector<uint8_t> data = std::move(rx.data);
    g_rx.erase(from);
    g_bundles_in++;
    if (is_host() && !g_reg.count(shared) && g_reg_by[from] >= kMaxRegPerPlayer) {
        g_bundles_dropped++; // (a guest that sends more line creations than a line holds)
        return;
    }
    if (!g_reg.count(shared) && !g_reg_failed.count(shared)) {
        if (is_host())
            g_reg_by[from]++;
        Key k = creations::line_register(data, shared, "player " + std::to_string(owner) + "'s line creation");
        if (k.instance)
            g_reg[shared] = k;
        else
            g_reg_failed.insert(shared);
    }
    if (is_host() && g_reg.count(shared)) { // (one the host cannot build goes nowhere: its rows go out without it)
        for (auto& p : S->players())
            if (p.id != 0 && p.id != owner)
                send_bundle(p.id, owner, shared, data);
        g_bundles[shared] = {owner, std::move(data)};
        unsigned mine = 0;
        for (auto& [sh, b] : g_bundles)
            mine += b.owner == owner;
        if (mine > kMaxDesigns + 2)
            host_prune(); // (more than a row can name: only the named ones stay)
    }
}

static void take_row(uint8_t kind, uint8_t from, net::Reader& r)
{
    uint8_t pid = r.u8();
    uint16_t n = r.u16();
    if (!r.ok() || n > r.left())
        return;
    std::vector<uint8_t> bytes(r.cur(), r.cur() + n);
    Row row;
    if (n && !decode(bytes, row)) {
        g_rows_refused++;
        return;
    }
    if (kind == kStored) { // the host's store: ours
        if (is_host() || from != 0 || pid != S->my_id())
            return;
        g_stored_seen = true;
        g_own_next = 0;
        bool refs = g_own.tribe.set() || g_own.civ.set() || !g_own.designs.empty();
        if (!refs && n) { // nothing set in this session: the stored line is ours again (packed again in own_tick)
            g_own.tribe = row.tribe;
            g_own.civ = row.civ;
            g_own.designs = row.designs;
            g_own_bundles.clear();
            g_stored_taken++;
            H->log(M, SK_INFO, "species: the host kept our line: taken (tribe %08X, civ %08X, %u designs)", row.tribe.own,
                   row.civ.own, (unsigned)row.designs.size());
        }
        return;
    }
    if (is_host()) {
        if (pid != from || pid == 0) { // a guest names only its own row
            g_rows_refused++;
            return;
        }
        g_rows_in++;
        if (row.empty())
            g_rows.erase(pid);
        else
            g_rows[pid] = row;
        store_put(pid);
        host_prune();
        g_res_next = 0;
        return;
    }
    if (from != 0 || pid == 0)
        return;
    g_rows_in++;
    if (row.empty())
        g_rows.erase(pid);
    else
        g_rows[pid] = row;
    g_res_next = 0;
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (type != net::MSG_SPECIES_LINE || !g_line_fix || !S)
        return;
    uint8_t kind = r.u8();
    if (!r.ok())
        return;
    if (kind == kBundle)
        take_bundle(from, r);
    else if (kind == kRow || kind == kStored)
        take_row(kind, from, r);
}

// ---- the per-frame part ----
static void resolve_tick(double now, bool force)
{
    if (!force && now < g_res_next)
        return;
    g_res_next = now + 0.25;
    std::set<uint8_t> pids;
    for (auto& [pid, r] : g_res)
        pids.insert(pid);
    for (auto& [pid, r] : g_rows)
        pids.insert(pid);
    if (S && S->connected())
        for (auto& p : S->players())
            if (p.id != 0)
                pids.insert(p.id);
    for (uint8_t pid : pids) {
        Resolved now_r = resolve(pid);
        auto it = g_res.find(pid);
        bool had = it != g_res.end();
        if (had && it->second == now_r)
            continue;
        if (!had && now_r.empty())
            continue;
        if (now_r.empty())
            g_res.erase(pid);
        else
            g_res[pid] = now_r;
        for (auto& x : now_r.k)
            if (x.instance)
                g_was.insert({x.instance, x.group});
        for (auto& [id, d] : now_r.designs)
            g_was.insert({d.instance, d.group});
        g_serial[pid]++;
        g_changes++;
        trace::line("species", "player %u's line here: creature %08X tribe %08X civ %08X, %u designs", pid,
                    now_r.k[(int)Stage::Creature].instance, now_r.k[(int)Stage::Tribe].instance,
                    now_r.k[(int)Stage::Civ].instance, (unsigned)now_r.designs.size());
        for (auto& s : subs())
            s.fn(pid);
    }
}

void tick(double now)
{
    if (!S)
        return;
    if (g_line_fix && S->connected()) {
        own_tick(now);
        host_tick();
    }
    resolve_tick(now, false);
}

// ---- state ----
static void session_reset()
{
    g_rows.clear();
    g_own = Row{};
    g_own_bundles.clear();
    g_stored_seen = false;
    g_own_sent.clear();
    g_own_sent_any = false;
    g_own_bundles_sent.clear();
    g_own_next = 0;
    g_bundles.clear();
    g_row_sent.clear();
    g_offered.clear();
    g_rx.clear();
    g_reg_by.clear();
    g_seen_at.clear();
    g_reg_failed.clear();
    g_res_next = 0; // (g_res stays: the next tick sees every line go and tells the packages)
}
SESSION_STATE("species.line", nullptr, session_reset, []() -> size_t {
    return g_rows.size() + g_own_bundles.size() + g_bundles.size() + g_offered.size() + g_rx.size() +
           (g_own.empty() ? 0 : 1);
});
PLAYER_STATE("species.line", [](uint8_t id) {
        g_rows.erase(id);
        g_row_sent.erase(id);
        g_offered.erase(id);
        g_rx.erase(id);
        g_reg_by.erase(id);
        g_seen_at.erase(id);
        host_prune();
        g_res_next = 0;
    },
    [](uint8_t id) -> size_t { return g_rows.count(id) + g_row_sent.count(id) + g_offered.count(id); });

// ---- the console ----
static std::string row_text(const Row& r)
{
    std::string s = "creature=" + hx(r.creature);
    auto ref = [&](const char* n, const Ref& f) {
        if (f.set())
            s += std::string(" ") + n + "=" + hx(f.own) + "/" + hx(f.type) + " shared=" + hx(f.shared) +
                 " made_for=" + hx(f.made_for);
    };
    ref("tribe", r.tribe);
    ref("civ", r.civ);
    for (auto& [id, f] : r.designs)
        s += " design " + hx(id) + "=" + hx(f.own) + " shared=" + hx(f.shared);
    return s;
}

std::string status()
{
    std::string s;
    char b[400];
    snprintf(b, sizeof b, "species line: fix=%d mode=%s role=%s rows=%u registered=%u failed=%u cleared_out=%u "
             "row_cleared=%d\n", (int)g_line_fix,
             own_mode() ? "own" : "host", !S ? "none" : is_host() ? "host" : is_guest() ? "guest" : "none",
             (unsigned)g_rows.size(), (unsigned)g_reg.size(), (unsigned)g_reg_failed.size(), g_rows_cleared_out,
             (int)g_row_cleared_fix);
    s += b;
    std::set<uint8_t> pids;
    for (auto& [pid, r] : g_rows)
        pids.insert(pid);
    for (auto& [pid, r] : g_res)
        pids.insert(pid);
    if (S)
        for (auto& p : S->players())
            if (p.id != 0)
                pids.insert(p.id);
    for (uint8_t pid : pids) {
        snprintf(b, sizeof b, "player %u '%s': serial=%u", pid, S ? S->name_of(pid).c_str() : "?", serial(pid));
        s += b;
        for (int st = 0; st < (int)Stage::Count; st++) {
            Key k = key(pid, (Stage)st);
            snprintf(b, sizeof b, " %s=%08X%s", stage_name((Stage)st), k.instance,
                     k.instance ? (creations::key_known(k) ? "" : "(unknown!)") : "");
            s += b;
        }
        for (uint32_t id : design_ids(pid)) {
            snprintf(b, sizeof b, " design %08X=%08X", id, design(pid, id).instance);
            s += b;
        }
        s += "\n";
        auto it = g_rows.find(pid);
        if (it != g_rows.end())
            s += "  row: " + row_text(it->second) + "\n";
    }
    if (is_guest()) {
        s += "own: " + row_text(g_own) + (g_stored_seen ? "" : " (waiting for the host's answer)") + "\n";
        snprintf(b, sizeof b, "own keys: creature=%08X tribe=%08X civ=%08X\n", own_key(Stage::Creature).instance,
                 own_key(Stage::Tribe).instance, own_key(Stage::Civ).instance);
        s += b;
    }
    snprintf(b, sizeof b,
             "counters: rows in=%u out=%u refused=%u bundles in=%u out=%u dropped=%u stored out=%u taken=%u "
             "changes=%u store_puts=%u sets=%u refused=%u\n",
             g_rows_in, g_rows_out, g_rows_refused, g_bundles_in, g_bundles_out, g_bundles_dropped, g_stored_out,
             g_stored_taken, g_changes, g_store_puts, g_sets, g_sets_refused);
    s += b;
    snprintf(b, sizeof b, "saves: line keys known=%u rekeyed=%u unmapped=%u (BUG-1315)\n", (unsigned)g_was.size(),
             g_save_rekeyed, g_save_unmapped);
    s += b;
    s += creations::line_status();
    return s;
}

std::string trace_line()
{
    std::string s;
    char b[160];
    snprintf(b, sizeof b, "mode=%s rows=%u registered=%u", own_mode() ? "own" : "host", (unsigned)g_rows.size(),
             (unsigned)g_reg.size());
    s = b;
    for (auto& [pid, r] : g_res) {
        snprintf(b, sizeof b, "; p%u creature %08X tribe %08X civ %08X designs %u", pid, r.k[1].instance,
                 r.k[2].instance, r.k[3].instance, (unsigned)r.designs.size());
        s += b;
    }
    return s;
}

static bool parse_stage(const char* a, Stage& s)
{
    for (int i = 0; i < (int)Stage::Count; i++)
        if (!strcmp(a, stage_name((Stage)i))) {
            s = (Stage)i;
            return true;
        }
    return false;
}

// coop.species test tribe|civ next|none|<instance>: one of our own creatures (My Creations): "next" the first that is
// not our creature (the lab's outfit stand-in); coop.species test design <id> <instance>|none
static Key own_creation(const char* arg, bool& none)
{
    none = !strcmp(arg, "none");
    if (none)
        return {};
    auto opts = creations::options(creations::kCreature);
    creations::Key cur = creations::mine(creations::kCreature).key;
    if (!strcmp(arg, "next")) {
        for (auto& o : opts)
            if (!(o.key == cur))
                return o.key;
        for (auto& o : opts) // one own creature only: itself (its copy under the line's shared key)
            return o.key;
        return {};
    }
    uint32_t inst = (uint32_t)strtoul(arg, nullptr, 16);
    for (auto& o : opts)
        if (o.key.instance == inst)
            return o.key;
    return {};
}

static int command(void*, int argc, const char* const* argv, SkOut* out)
{
    std::vector<std::string> a(argv + 1, argv + argc);
    if (a.empty() || a[0] == "status") {
        H->out_printf(out, "%s", status().c_str());
        return 0;
    }
    if (a[0] == "key" && a.size() >= 3) { // coop.species key <player> <stage>: the key and the model the game has for it
        Stage st;
        if (!parse_stage(a[2].c_str(), st)) {
            H->out_printf(out, "stages: cell creature tribe civ\n");
            return 1;
        }
        Key k = key((uint8_t)atoi(a[1].c_str()), st);
        H->out_printf(out, "key=%08X/%08X/%08X known=%d model=%s\n", k.instance, k.type, k.group,
                      k.instance ? (int)creations::key_known(k) : 0,
                      k.instance ? creations::model_hash(k).c_str() : "none");
        return 0;
    }
    if (a[0] == "own" && a.size() >= 2) { // coop.species own <stage>: our own key and its model (the owner's side)
        Stage st;
        if (!parse_stage(a[1].c_str(), st)) {
            H->out_printf(out, "stages: cell creature tribe civ\n");
            return 1;
        }
        Key k = own_key(st);
        H->out_printf(out, "own=%08X/%08X/%08X model=%s\n", k.instance, k.type, k.group,
                      k.instance ? creations::model_hash(k).c_str() : "none");
        return 0;
    }
    if (a[0] == "test" && a.size() >= 3 && (a[1] == "tribe" || a[1] == "civ")) {
        bool none = false;
        Key k = own_creation(a[2].c_str(), none);
        if (!k.instance && !none) {
            H->out_printf(out, "no such own creature (next: needs two own creatures in My Creations)\n");
            return 1;
        }
        bool ok = a[1] == "tribe" ? set_tribe_outfit(k) : set_civ_outfit(k);
        H->out_printf(out, "%s %s outfit %08X\n", ok ? "set" : "refused", a[1].c_str(), k.instance);
        return ok ? 0 : 1;
    }
    if (a[0] == "test" && a.size() >= 4 && a[1] == "design") {
        uint32_t id = (uint32_t)strtoul(a[2].c_str(), nullptr, 16);
        bool none = false;
        Key k = own_creation(a[3].c_str(), none); // (the lab's stand-in: an own creature under a design id)
        if (!k.instance && !none) {
            H->out_printf(out, "no such own creature\n");
            return 1;
        }
        bool ok = set_design(id, k);
        H->out_printf(out, "%s design %08X = %08X\n", ok ? "set" : "refused", id, k.instance);
        return ok ? 0 : 1;
    }
    H->out_printf(out, "usage: coop.species [status | key <player> <stage> | own <stage> | test tribe|civ "
                       "next|none|<instance> | test design <id> next|none|<instance>]\n");
    return 1;
}

void init(const SkHost* host, SkMod* mod, net::Session* sess)
{
    H = host;
    M = mod;
    S = sess;
    H->command_add(M, "coop.species",
                   "coop.species [status | key <player> <stage> | own <stage> | test ...] - every player's own species "
                   "line per stage (WP-0, BUG-1270)",
                   command, nullptr, SK_CMD_MAIN_THREAD);
    savestrip::add_key_filter(save_key_filter); // BUG-1315: no key of a line in a save
}

} // namespace species
