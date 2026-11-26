// The state the host keeps for a Space guest across its rejoin (C5 of docs/plan-0.7.15.md, audit NET-1): one store
// and one restore window for the four features that keep something (space_map: own colonies, BUG-0195; space_trade:
// the cargo hold, BUG-0678; space_badges: badges and rank, BUG-0786; space_rivalry: the Rivals budget, BUG-1029).
// Portable (no game, no session): tests/space_tests.cpp runs the state machine. docs/01-architecture.md#arch-space-kept-store.
//
// Every player flies in a copy of the host's save, so what a guest owns only in its own game (a colony it founded, its
// hold, its badges, its budget) is gone when it rejoins and loads the host's world again. The handshake, the same for
// every feature (the bytes on the wire stay each feature's own, see wire/space_*.h):
//   1. the guest reports its state to the host (K_ROSTER, K_INV, K_BADGES, K_MONEY); the host keeps the last report
//      per player key (net::Session::player_key: its rejoin token, BUG-1107) for the Space world it plays;
//   2. the first report after a (re)join of a player the host kept something for is the host's copy (a fresh hold, a
//      roster without the guest's colonies): the host sends the kept state back (K_KEPT, K_INV_KEPT, K_BADGES_KEPT;
//      begin()), takes no report of that player as its state until the guest answers (K_KEPT_DONE, K_INV_DONE,
//      K_BADGES_DONE; done()) and sends it again every 10 s without an answer (tick());
//   3. the guest writes it into its game in a Space world loaded from the host since it joined (RestoreWindow) and
//      answers; until then a gift or a sale that would go into the state about to be replaced waits (HeldQueue,
//      BUG-1034).
// Rivals asks instead (K_ASK -> K_SET: the guest repeats the ask every 2 s, the answer is idempotent): it uses the
// store's keyed values (find / put) and the window's known budget, not begin / done.
// Another Space world (world()) empties the store; a player who left is unbound (leave(), tick()); the session's end
// unbinds everybody (session_end()) and keeps the values for the next session in the same world.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace spacekept {

constexpr uint8_t kNoId = 0xFF;   // an entry whose player is not in the session
constexpr double kResend = 10.0;  // s without an answer before the kept state goes again

// BUG-1578: modes that tear down a guest's Space world in memory (Galaxy menu, Loading, another stage),
// unlike Space itself or an editor/adventure entered from Space (matches spaceext::world_epoch).
inline bool mode_tears_down_space(uint32_t mode)
{
    return mode != 0 && mode != 0x01654C05u && mode != 0x01654C06u && mode != 0x00DBDBA1u && mode != 0x01654C10u;
}

template <class T>
struct Entry {
    T value{};
    bool have = false;      // a report was kept (a first join has none)
    std::string name;       // the player's session name when last seen (status only; the key is the player key)
    uint8_t id = kNoId;     // its session id now
    bool restoring = false; // sent back, no answer yet
    uint32_t serial = 0;    // the restore's serial (answered with it, where the feature's message has one)
    double sent = 0;        // when it went
};

template <class T>
class KeptStore {
public:
    using Map = std::map<std::string, Entry<T>>;

    // the Space world the host plays (spaceext::world_epoch): a different non-zero one empties the store (the guests'
    // state of the old world is not this one's). true: emptied now
    bool world(uint32_t epoch)
    {
        if (!epoch || epoch == epoch_)
            return false;
        map_.clear();
        epoch_ = epoch;
        return true;
    }

    // a report of player `key` with session id `id`: its entry (made empty when new), bound to the id. *first: this is
    // its first report since it joined (the id changed, or it had left): then it is not restoring any more.
    // exclusive: an entry of another key bound to the same id lets go of it (space_map: a newcomer that got a departed
    // player's id must not see that player's entry as its own)
    Entry<T>& bind(const std::string& key, uint8_t id, const std::string& name, bool exclusive, bool* first = nullptr)
    {
        Entry<T>& e = map_[key];
        e.name = name;
        bool f = e.id != id;
        if (f) {
            if (exclusive)
                for (auto& [k, o] : map_)
                    if (o.id == id && &o != &e)
                        o.id = kNoId;
            e.id = id;
            e.restoring = false;
        }
        if (first)
            *first = f;
        return e;
    }

    // the kept state goes back to the entry's player now: restoring until done(); the serial for the message
    uint32_t begin(Entry<T>& e, double now)
    {
        e.restoring = true;
        e.sent = now;
        e.serial = ++serial_;
        sent_back_++;
        return e.serial;
    }

    // the answer of player `id` (serial: the one it answers, when `check_serial`): the entry whose restore it ends, or
    // nullptr (an old answer, another player's, or no restore running)
    Entry<T>* done(uint8_t id, uint32_t serial, bool check_serial)
    {
        for (auto& [k, e] : map_)
            if (e.id == id && e.restoring && (!check_serial || e.serial == serial)) {
                e.restoring = false;
                done_++;
                return &e;
            }
        return nullptr;
    }

    // a report as the player's state: not while its restore runs (it is the host's copy, sent before the player got
    // its own back). true: kept
    bool keep(Entry<T>& e, T v)
    {
        if (e.restoring)
            return false;
        e.value = std::move(v);
        e.have = true;
        kept_++;
        return true;
    }

    // the session said player `id` left (a crash rejoin may give the newcomer the same id in the same message,
    // BUG-0810): its entry is unbound. The number of entries it unbound
    size_t leave(uint8_t id)
    {
        size_t n = 0;
        for (auto& [k, e] : map_)
            if (e.id == id) {
                e.id = kNoId;
                e.restoring = false;
                n++;
            }
        return n;
    }
    size_t bound(uint8_t id) const
    {
        size_t n = 0;
        for (auto& [k, e] : map_)
            n += e.id == id;
        return n;
    }

    // every frame on the host: a player that is not here any more is unbound; a restore without an answer for
    // kResend s goes again (resend(entry) sends the feature's message and calls begin())
    template <class Here, class Resend>
    void tick(double now, Here here, Resend resend)
    {
        for (auto& [k, e] : map_) {
            if (e.id != kNoId && !here(e.id))
                e.id = kNoId, e.restoring = false;
            else if (e.id != kNoId && e.restoring && now - e.sent > kResend)
                resend(e);
        }
    }

    // the session ended: nobody is bound; the values stay for the next session in the same Space world
    void session_end()
    {
        for (auto& [k, e] : map_)
            e.id = kNoId, e.restoring = false;
    }

    // keyed values (Rivals: K_MONEY / K_ASK)
    Entry<T>* find(const std::string& key)
    {
        auto it = map_.find(key);
        return it == map_.end() || !it->second.have ? nullptr : &it->second;
    }
    void put(const std::string& key, T v)
    {
        Entry<T>& e = map_[key];
        e.value = std::move(v);
        e.have = true;
        kept_++;
    }

    // E1 (player_store.h, BUG-1149): a value read from the host's disk when the world opened: kept, nobody bound. The
    // player's next join binds it, and its first report is then answered with it as after a rejoin. An entry that
    // already holds a value of this session is left as it is. true: taken
    bool restore(const std::string& key, const std::string& name, T v)
    {
        Entry<T>& e = map_[key];
        if (e.have)
            return false;
        e.value = std::move(v);
        e.have = true;
        if (e.name.empty())
            e.name = name;
        restored_++;
        return true;
    }
    // the host's word (`coop.store adopt`): the entry of key `from` becomes key `to`'s (to's own entry goes); the
    // binding stays to's. true: moved
    bool rekey(const std::string& from, const std::string& to)
    {
        auto it = map_.find(from);
        if (from == to || it == map_.end() || !it->second.have)
            return false;
        Entry<T> e = std::move(it->second);
        map_.erase(it);
        Entry<T>& d = map_[to];
        uint8_t id = d.id;
        std::string name = d.name.empty() ? e.name : d.name;
        d = std::move(e);
        d.id = id;
        d.name = name;
        d.restoring = false;
        return true;
    }
    uint32_t restored() const { return restored_; }

    void clear() { map_.clear(); }
    Map& entries() { return map_; }
    const Map& entries() const { return map_; }
    size_t size() const { return map_.size(); }
    uint32_t sent_back() const { return sent_back_; }
    uint32_t answered() const { return done_; }
    uint32_t reports() const { return kept_; }

private:
    Map map_;
    uint32_t epoch_ = 0, serial_ = 0, sent_back_ = 0, done_ = 0, kept_ = 0, restored_ = 0;
};

// The guest's side: which Space world's state is its own and whether the host's kept copy may still replace it.
// Worlds are spaceext::world_epoch values (0: none).
struct RestoreWindow {
    bool join_seen = false;
    uint32_t join = 0;    // the Space world when it joined (its own world, before the host's arrived); 0: not in Space
    uint32_t fresh = 0;   // the world whose first report went to the host (marked fresh)
    double fresh_t = 0;   // when it went
    uint32_t applied = 0; // the world the kept copy (or the guest's own record) was written into
    uint32_t none = 0;    // the world the host said it keeps nothing for
    bool known = false;   // Rivals: the budget of `applied` is known (the host's answer, or kept from before)

    // the first frame after the join (every frame: only the first one counts)
    void joined(bool in_space, uint32_t world)
    {
        if (join_seen)
            return;
        join_seen = true;
        join = in_space ? world : 0;
    }
    // a world loaded from the host since the join: the one whose state the host's copy may replace
    bool from_host(uint32_t world) const { return world && world != join; }
    // its first report goes as fresh
    bool is_fresh(uint32_t world) const { return world != fresh; }
    void sent(uint32_t world, bool was_fresh, double now)
    {
        if (was_fresh)
            fresh_t = now;
        fresh = world;
    }
    // the kept copy may be written into this world (not twice, not into the guest's own world of the join)
    bool may_apply(uint32_t world) const { return world != applied && world != join; }
    // a world loaded from the host whose state is not settled: neither restored nor answered with "nothing kept"
    bool open(uint32_t world) const { return world && world != join && world != applied && world != none; }
    // ... and may still be replaced by the kept copy: no answer yet to its fresh report (and at most `max_wait` s
    // after it went: a host without the store never answers)
    bool pending(uint32_t world, double now, double max_wait) const
    {
        return open(world) && (world != fresh || now - fresh_t < max_wait);
    }
    // the session ended: the next join is a new one (what was applied stays: it was this game's own state)
    void session_end()
    {
        join_seen = false;
        fresh = 0;
        none = 0;
    }
};

// What arrives inside the window and would go into the state about to be replaced (BUG-1034): kept here and handled
// once the restore is done (or the session ended). At most kMax: beyond that a message is handled at once (as before
// the queue existed).
struct HeldMsg {
    uint8_t kind = 0, origin = 0;
    std::vector<uint8_t> payload;
};
class HeldQueue {
public:
    static constexpr size_t kMax = 64;
    bool hold(uint8_t kind, uint8_t origin, const uint8_t* p, size_t n)
    {
        if (q_.size() >= kMax)
            return false;
        q_.push_back({kind, origin, std::vector<uint8_t>(p, p + n)});
        held_++;
        return true;
    }
    std::vector<HeldMsg> take()
    {
        std::vector<HeldMsg> v = std::move(q_);
        q_.clear();
        return v;
    }
    bool empty() const { return q_.empty(); }
    size_t size() const { return q_.size(); }
    uint32_t held() const { return held_; }

private:
    std::vector<HeldMsg> q_;
    uint32_t held_ = 0;
};

} // namespace spacekept
