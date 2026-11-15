// C1 of docs/plan-0.7.15.md (audit STG-1, CORE-4; classes BUG-0426 identity / lifetime and BUG-0427): one registry for
// the id <-> game object tables every stage keeps by hand today (npc g_host_npcs / g_host_by_id and g_proxies /
// g_proxy_of, tribe g_hm / g_hm_by_id and g_gm / g_gid_of, civ g_hv / g_hv_by_id and g_gv / g_gv_of). One entry per
// net id: the object bound to it (pointer + its cGameData::mID serial + noun, the fields of game::LiveRef) or none yet,
// whether the mod made the object, and the stage's own data. The reverse index (object -> id) is kept by the registry,
// so the two directions cannot drift apart (civ.cpp's `g_gv_of[v] == id` checks, BUG-0951).
//
//   liveness   injected (AliveFn): the game glue passes game::alive (the object list of the frame and the serial);
//              a stage with another rule passes its own (Civ guest vehicles: the serial in the object's memory,
//              because a vehicle the mod just made joins the list later). The registry never reads game memory.
//   bind       an object to an id; a pointer bound to another id is refused (counted), never silently stolen.
//   adopt      host side: the id of an object of the game's lists, a new id when the object is new or another
//              object was built at a known address (another serial: BUG-0951, BUG-0044's class).
//   sweep      after a complete host snapshot (net::SnapshotReceiver, C4) or a host table pass: every entry the
//              pass did not see is decided by the stage: Remove (the stage destroys the object), Keep (held: try
//              again next pass) or Defer (the entry goes now, its object waits in the deferred list until the
//              hold predicate lets it go: a cinematic, the planner, an editor; Civ's g_kill_later).
//   pump       the deferred list, once a frame: gone objects are dropped, held ones wait, the rest are destroyed.
//   ours       the save filter (CLAUDE.md 2: our objects never go into a save): an object bound and made by the
//              mod (or, with all_bound, any bound one) or waiting in the deferred list.
//   stats      for coop.status / the stage's debug line (status()).
// Rules: the registry holds no game call; every callback runs in the caller's frame (main thread); a stage keeps its
// game calls (despawn, RemoveMember, kill_local) in the callbacks. Portable (no Windows, no SporeKit):
// tests/core_tests.cpp checks it natively. docs/01-architecture.md#arch-mirror-registry.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <unordered_map>
#include <vector>

namespace mirror {

// the identity of a game object: its address, its serial (cGameData::mID when it was bound) and the noun it must be
// listed with (0: any); the same fields as game::LiveRef (game.h), so the glue converts with one aggregate copy
struct Ref {
    void* p = nullptr;
    uint32_t serial = 0;
    uint32_t noun = 0;
    explicit operator bool() const { return p != nullptr; }
};
using AliveFn = bool (*)(const Ref&);

// what a sweep does with an entry the pass did not see
enum class Unseen : uint8_t { Remove, Keep, Defer };

struct Stats {
    uint32_t bound = 0;     // binds made
    uint32_t made = 0;      // of them objects the mod made
    uint32_t refused = 0;   // binds refused: the pointer is bound to another id
    uint32_t stale = 0;     // objects found not alive (freed, another object at the address) and forgotten
    uint32_t reused = 0;    // adopt: another object at a known address (a new id)
    uint32_t removed = 0;   // entries a sweep removed (Remove)
    uint32_t kept = 0;      // unseen entries a sweep kept (Keep)
    uint32_t deferred = 0;  // objects handed to the deferred list (Defer)
    uint32_t deferred_done = 0; // deferred objects destroyed by pump
    uint32_t deferred_gone = 0; // deferred objects the game removed first (dropped without a call)
    uint32_t sweeps = 0;    // sweeps run
    uint32_t stripped = 0;  // objects the save filter answered "ours" for
};

template <class Id, class Data = char>
class Registry {
public:
    struct Entry {
        Id id{};
        Ref ref;           // the object bound (ref.p null: none yet, the stage makes or binds one later)
        bool made = false; // the mod made the object (spawned), not one of the game's own bound as a mirror
        Data data{};
    };

    Registry(const char* name, AliveFn alive) : name_(name), alive_(alive) {}

    // ---- the entry of an id ----
    // the entry of `id`, made empty when there is none (the host named a mirror that has no object yet)
    Entry& at(Id id)
    {
        auto it = by_id_.find(id);
        if (it == by_id_.end()) {
            it = by_id_.emplace(id, Entry{}).first;
            it->second.id = id;
        }
        return it->second;
    }
    Entry* find(Id id)
    {
        auto it = by_id_.find(id);
        return it == by_id_.end() ? nullptr : &it->second;
    }
    const Entry* find(Id id) const
    {
        auto it = by_id_.find(id);
        return it == by_id_.end() ? nullptr : &it->second;
    }
    bool has(Id id) const { return by_id_.count(id) != 0; }
    size_t size() const { return by_id_.size(); }
    bool empty() const { return by_id_.empty(); }

    // ---- binding ----
    // binds `r` to `id` (the entry is made when missing). False, and nothing changes, when r.p is bound to another
    // id. A previous object of `id` is released from the reverse index.
    bool bind(Id id, const Ref& r, bool made)
    {
        if (!r.p)
            return false;
        auto o = of_.find(r.p);
        if (o != of_.end() && !(o->second == id)) {
            stats_.refused++;
            return false;
        }
        Entry& e = at(id);
        if (e.ref.p && e.ref.p != r.p)
            of_.erase(e.ref.p);
        e.ref = r;
        e.made = made;
        of_[r.p] = id;
        stats_.bound++;
        if (made)
            stats_.made++;
        return true;
    }
    // the entry keeps its data, its object is forgotten (no game call; the stage makes or binds one again)
    void detach(Id id)
    {
        Entry* e = find(id);
        if (!e || !e->ref.p)
            return;
        auto o = of_.find(e->ref.p);
        if (o != of_.end() && o->second == id)
            of_.erase(o);
        e->ref = Ref{};
        e->made = false;
    }
    // the entry goes (no game call)
    void erase(Id id)
    {
        detach(id);
        by_id_.erase(id);
    }

    // ---- lookups ----
    // the id bound to `p` (a pointer compare: safe on a freed one), without a liveness check
    bool id_of(const void* p, Id& out) const
    {
        auto o = of_.find(const_cast<void*>(p));
        if (o == of_.end())
            return false;
        out = o->second;
        return true;
    }
    bool bound(const void* p) const { return of_.count(const_cast<void*>(p)) != 0; }
    // the entry's object is alive (the injected rule); a dead one is detached here (stale)
    bool live(Entry& e)
    {
        if (!e.ref.p)
            return false;
        if (alive_(e.ref))
            return true;
        stats_.stale++;
        detach(e.id);
        return false;
    }
    // the object of `id` while it is the object bound and alive, else null (a dead one is detached)
    void* get_live(Id id)
    {
        Entry* e = find(id);
        return e && live(*e) ? e->ref.p : nullptr;
    }
    // the id of `p` while p is the live object bound to it
    bool id_of_live(const void* p, Id& out)
    {
        Id id;
        if (!id_of(p, id))
            return false;
        Entry* e = find(id);
        if (!e || !live(*e))
            return false;
        out = id;
        return true;
    }

    // ---- walks ----
    template <class F>
    void for_each(F&& f)
    {
        for (auto& [id, e] : by_id_)
            f(e);
    }
    // every entry whose object is alive (the dead ones are detached on the way)
    template <class F>
    void for_each_live(F&& f)
    {
        for (auto& [id, e] : by_id_)
            if (live(e))
                f(e);
    }
    // every entry with an object that is not alive is detached now (BUG-0842's check_identity, the snapshot's
    // "its object is gone"); returns how many
    size_t check_live()
    {
        size_t n = 0;
        for (auto& [id, e] : by_id_)
            if (e.ref.p && !live(e))
                n++;
        return n;
    }

    // ---- host side: the id of an object of the game's lists ----
    // the id bound to r.p when it is the same object (serial), else a new one from `alloc()` (0 / Id{}: none free,
    // false). Another object at a known address (another serial) loses the old id first: `on_reused(old id)` runs
    // (the stage retires the old id at its guests) and the entry goes.
    template <class Alloc, class OnReused>
    bool adopt(const Ref& r, Alloc&& alloc, OnReused&& on_reused, Id& out, bool* fresh = nullptr)
    {
        if (fresh)
            *fresh = false;
        Id id;
        if (id_of(r.p, id)) {
            Entry* e = find(id);
            if (e && (!r.serial || !e->ref.serial || e->ref.serial == r.serial)) {
                out = id;
                return true;
            }
            stats_.reused++;
            on_reused(id);
            erase(id);
        }
        Id n = alloc();
        if (n == Id{})
            return false;
        if (!bind(n, r, false))
            return false;
        if (fresh)
            *fresh = true;
        out = n;
        return true;
    }

    // ---- delete-unseen ----
    // After a complete snapshot / table pass: every entry with !seen(id) goes to decide(entry) (the stage's hold
    // predicate: Keep while its object is in a cinematic, held, in an editor, ...). Remove: remove(entry) runs with
    // `alive` (the stage destroys the object only when it is alive; a dead one is only forgotten) and the entry goes.
    // Defer: the object (when alive) joins the deferred list (pump destroys it later) and the entry goes. Keep: it
    // stays. Returns the entries removed or deferred.
    template <class Seen, class Decide, class Remove>
    size_t sweep(Seen&& seen, Decide&& decide, Remove&& remove)
    {
        stats_.sweeps++;
        std::vector<Id> gone;
        for (auto& [id, e] : by_id_)
            if (!seen(id))
                gone.push_back(id);
        size_t n = 0;
        for (const Id& id : gone) {
            Entry* e = find(id);
            if (!e)
                continue;
            bool alive = e->ref.p && alive_(e->ref);
            Unseen d = decide(*e, alive);
            if (d == Unseen::Keep) {
                stats_.kept++;
                continue;
            }
            if (d == Unseen::Defer && alive) {
                deferred_.push_back({e->ref, e->made});
                stats_.deferred++;
            } else {
                remove(*e, alive);
                stats_.removed++;
            }
            erase(id);
            n++;
        }
        return n;
    }
    // the deferred list, once a frame: an object no longer alive is dropped (the game removed it), one that
    // hold(ref) still holds waits, the others go to destroy(ref)
    template <class Hold, class Destroy>
    void pump(Hold&& hold, Destroy&& destroy)
    {
        for (size_t i = 0; i < deferred_.size();) {
            Deferred& d = deferred_[i];
            if (!alive_(d.ref) || bound(d.ref.p)) { // gone, or a mirror again (civ guest_tick's rule)
                stats_.deferred_gone++;
            } else if (hold(d.ref)) {
                i++;
                continue;
            } else {
                destroy(d.ref);
                stats_.deferred_done++;
            }
            deferred_.erase(deferred_.begin() + (long)i);
        }
    }
    // an object of the game's own that the host does not have (the orphan sweep: tribe's unknown local members, civ's
    // local vehicles) goes through the same deferred path
    void defer(const Ref& r, bool made)
    {
        if (!r.p || bound(r.p) || deferred(r.p))
            return;
        deferred_.push_back({r, made});
        stats_.deferred++;
    }
    bool deferred(const void* p) const
    {
        for (auto& d : deferred_)
            if (d.ref.p == p)
                return true;
        return false;
    }
    size_t deferred_count() const { return deferred_.size(); }

    // ---- the save filter ----
    // `p` (a live noun `noun` the save writes) is ours: bound to an entry and made by the mod (all_bound: any bound
    // object, Civ's rule), or waiting in the deferred list. `noun` 0 in an entry matches any noun.
    bool ours(const void* p, uint32_t noun, bool all_bound = false)
    {
        Id id;
        bool hit = false;
        if (id_of(p, id)) {
            const Entry* e = find(id);
            hit = e && (all_bound || e->made) && (!e->ref.noun || !noun || e->ref.noun == noun);
        }
        if (!hit)
            for (auto& d : deferred_)
                if (d.ref.p == p && (all_bound || d.made) && (!d.ref.noun || !noun || d.ref.noun == noun))
                    hit = true;
        if (hit)
            stats_.stripped++;
        return hit;
    }

    // ---- lifecycle and diagnostics ----
    // everything forgotten (no game call: a world that is gone, a session end); the counters stay
    void clear()
    {
        by_id_.clear();
        of_.clear();
        deferred_.clear();
    }
    // what is held (lifecycle.h `held`)
    size_t held() const { return by_id_.size() + deferred_.size(); }
    // the reverse index agrees with the entries (tests, coop.lifecycle): every object -> its id -> that object
    bool consistent() const
    {
        size_t with = 0;
        for (auto& [id, e] : by_id_)
            if (e.ref.p) {
                with++;
                auto o = of_.find(e.ref.p);
                if (o == of_.end() || !(o->second == id))
                    return false;
            }
        return with == of_.size();
    }
    const Stats& stats() const { return stats_; }
    // "<name>: n=.. bound=.. made=.. refused=.. stale=.. reused=.. removed=.. kept=.. deferred=../.. done=.. gone=..
    // sweeps=.. stripped=.."
    std::string status() const
    {
        size_t with = 0, made = 0;
        for (auto& [id, e] : by_id_) {
            with += e.ref.p != nullptr;
            made += e.ref.p && e.made;
        }
        char b[320];
        snprintf(b, sizeof b,
                 "%s: n=%zu objects=%zu made_now=%zu bound=%u made=%u refused=%u stale=%u reused=%u removed=%u kept=%u "
                 "deferred=%zu/%u done=%u gone=%u sweeps=%u stripped=%u",
                 name_, by_id_.size(), with, made, stats_.bound, stats_.made, stats_.refused, stats_.stale, stats_.reused,
                 stats_.removed, stats_.kept, deferred_.size(), stats_.deferred, stats_.deferred_done,
                 stats_.deferred_gone, stats_.sweeps, stats_.stripped);
        return b;
    }

private:
    struct Deferred {
        Ref ref;
        bool made;
    };
    const char* name_;
    AliveFn alive_;
    std::unordered_map<Id, Entry> by_id_;
    std::unordered_map<void*, Id> of_;
    std::vector<Deferred> deferred_;
    Stats stats_;
};

} // namespace mirror
