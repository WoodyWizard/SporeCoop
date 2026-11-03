// Replicated state fields of mirrored objects (fields.h, docs/sync-architecture-plan.md#sync-fields).
#include "fields.h"

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#include "fields_wire.h"
#include "game.h"
#include "net/protocol.h"

namespace fields {

static const SkHost* H;
static SkMod* M;
static Ctx C;

float read_scale(void* obj) { return game::object_scale(obj); }
void apply_scale(void* obj, float v) { game::set_object_scale(obj, v); }
// 2 %: the detector's tolerance is 12 %; the game's own scales of one species differ by 10-40 %
const Field kScale{F_SCALE, "scale", read_scale, apply_scale, 0.02f};

constexpr double kSendEvery = 0.5;   // host: deltas
constexpr double kRefresh = 4.0;     // host: every value again (an unreliable packet lost, a new mirror)
constexpr double kApplyEvery = 0.25; // guest: mirrors checked against the wanted values
constexpr double kForget = 12.0;     // guest: a value the host stopped sending (its object is gone)

static std::vector<Kind> g_kinds;
static std::map<uint8_t, Stats> g_stats;

struct Sent {
    float v;
    double t;
};
// host: guest -> (kind << 24 | field << 16 | id) -> last sent
static std::map<uint8_t, std::map<uint32_t, Sent>> g_sent;
static double g_sent_at = 0;

struct Want {
    float v;
    double t;   // received
    bool applied = false;
    // the mirror's own value before the first apply: a switch turned off (a lab twin run, after the join
    // applied the host's) gives it back, so the off side is the mirror's own value as before the fix
    void* own_obj = nullptr;
    float own = 0;
};
// guest: (kind << 24 | field << 16 | id) -> the host's value
static std::map<uint32_t, Want> g_want;
static double g_apply_at = 0;

static uint32_t key(uint8_t kind, uint8_t field, uint16_t id) { return (uint32_t)kind << 24 | (uint32_t)field << 16 | id; }

static const Kind* kind_of(uint8_t id)
{
    for (auto& k : g_kinds)
        if (k.id == id)
            return &k;
    return nullptr;
}

static const Field* field_of(const Kind& k, uint8_t fid)
{
    for (int i = 0; i < k.nfields; i++)
        if (k.fields[i].id == fid)
            return &k.fields[i];
    return nullptr;
}

static bool kind_active(const Kind& k) { return !k.active || k.active(); }
// the switch acts on the apply only: the host sends with it off too, so a test can compare the guest's
// own values (`coop.fields list`) with the host's in both runs
static bool kind_on(const Kind& k) { return (!k.on || *k.on) && kind_active(k); }

void init(const SkHost* host, SkMod* mod, const Ctx& ctx)
{
    H = host;
    M = mod;
    C = ctx;
}

bool add_kind(const Kind& k)
{
    if (kind_of(k.id) || !k.fields || k.nfields <= 0 || k.nfields > fields_wire::kMaxFields || !k.each || !k.mirror)
        return false;
    for (int i = 0; i < k.nfields; i++)
        if (!k.fields[i].read || !k.fields[i].apply) // the rule: a field without an apply is not a field
            return false;
    g_kinds.push_back(k);
    return true;
}

static bool is_host() { return C.sess && C.sess->connected() && C.sess->role() == net::Role::Host; }
static bool is_guest() { return C.sess && C.sess->connected() && C.sess->role() == net::Role::Client; }

static void host_tick(double now)
{
    if (now - g_sent_at < kSendEvery)
        return;
    g_sent_at = now;
    for (const Kind& k : g_kinds) {
        if (!kind_active(k))
            continue;
        struct Obj {
            uint16_t id;
            std::vector<float> v;
        };
        struct Acc {
            const Kind* k;
            std::vector<Obj> objs;
        } acc{&k, {}};
        k.each(
            [](uint16_t id, void* obj, void* u) {
                auto* a = (Acc*)u;
                if (!obj || a->objs.size() >= 4000)
                    return;
                Obj o{id, {}};
                for (int i = 0; i < a->k->nfields; i++)
                    o.v.push_back(a->k->fields[i].read(obj));
                a->objs.push_back(std::move(o));
            },
            &acc);
        for (auto& p : C.sess->players()) {
            if (p.id == 0 || (C.in_place && !C.in_place(p.id)))
                continue;
            auto& sent = g_sent[p.id];
            fields_wire::Batch b;
            b.kind = k.id;
            for (int i = 0; i < k.nfields; i++)
                b.fids.push_back(k.fields[i].id);
            for (auto& o : acc.objs) {
                bool any = false;
                for (int i = 0; i < k.nfields && !any; i++) {
                    auto it = sent.find(key(k.id, k.fields[i].id, o.id));
                    any = it == sent.end() || now - it->second.t >= kRefresh ||
                          fields_wire::differs(it->second.v, o.v[i], k.fields[i].tol * 0.5f);
                }
                if (!any)
                    continue;
                b.ids.push_back(o.id);
                for (int i = 0; i < k.nfields; i++) {
                    b.vals.push_back(o.v[i]);
                    sent[key(k.id, k.fields[i].id, o.id)] = {o.v[i], now};
                }
            }
            if (b.ids.empty())
                continue;
            for (auto& w : fields_wire::encode(b))
                C.sess->send_unreliable_to(p.id, net::MSG_FIELDS, w);
            g_stats[k.id].sent += (uint32_t)b.ids.size();
        }
    }
    for (auto it = g_sent.begin(); it != g_sent.end();) { // guests that left
        bool present = false;
        for (auto& p : C.sess->players())
            present |= p.id == it->first;
        it = present ? std::next(it) : g_sent.erase(it);
    }
}

static void guest_tick(double now)
{
    if (now - g_apply_at < kApplyEvery)
        return;
    g_apply_at = now;
    for (auto& [k, s] : g_stats)
        s.pending = s.checked = s.differ = 0;
    for (auto it = g_want.begin(); it != g_want.end();) {
        if (now - it->second.t > kForget) {
            it = g_want.erase(it);
            continue;
        }
        uint8_t kid = it->first >> 24, fid = (it->first >> 16) & 0xFF;
        uint16_t id = it->first & 0xFFFF;
        const Kind* k = kind_of(kid);
        const Field* f = k ? field_of(*k, fid) : nullptr;
        Stats& st = g_stats[kid];
        if (!f || !kind_active(*k)) {
            ++it;
            continue;
        }
        void* obj = k->mirror(id);
        if (!kind_on(*k)) { // switched off: the mirror's own value again
            if (obj && obj == it->second.own_obj) {
                f->apply(obj, it->second.own);
                it->second.own_obj = nullptr;
                it->second.applied = false;
            }
            ++it;
            continue;
        }
        if (!obj) {
            st.pending++;
            ++it;
            continue;
        }
        st.checked++;
        float cur = f->read(obj);
        if (fields_wire::differs(cur, it->second.v, f->tol)) {
            if (obj != it->second.own_obj) { // a new mirror object: remember its own value
                it->second.own_obj = obj;
                it->second.own = cur;
            }
            f->apply(obj, it->second.v);
            if (it->second.applied)
                st.reapplied++; // the game (or a module) changed the mirror's value again
            st.applied++;
            it->second.applied = true;
            if (fields_wire::differs(f->read(obj), it->second.v, f->tol)) {
                if (!st.differ && !st.reapplied)
                    H->log(M, SK_WARN, "fields: %s %u %s: applied %.3f, the game reads %.3f", k->name, id, f->name,
                           it->second.v, f->read(obj));
                st.differ++;
            }
        }
        ++it;
    }
}

void tick(double now)
{
    if (is_host())
        host_tick(now);
    else if (is_guest())
        guest_tick(now);
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (type != net::MSG_FIELDS || !is_guest() || from != 0)
        return;
    fields_wire::Batch b;
    if (!fields_wire::decode(r, b))
        return;
    const Kind* k = kind_of(b.kind);
    if (!k)
        return;
    double now = H->time_seconds();
    size_t nf = b.fids.size();
    for (size_t j = 0; j < b.ids.size(); j++)
        for (size_t i = 0; i < nf; i++) {
            const Field* f = field_of(*k, b.fids[i]);
            float v = b.vals[j * nf + i];
            if (!f || !fields_wire::sane(v, 0.001f, 1000.f))
                continue;
            auto& w = g_want[key(k->id, f->id, b.ids[j])];
            if (fields_wire::differs(w.v, v, 1e-4f))
                w.applied = false;
            w.v = v;
            w.t = now;
        }
    g_stats[k->id].received += (uint32_t)b.ids.size();
}

void on_world_change()
{
    g_want.clear();
    g_sent.clear();
}

void on_session_end()
{
    g_want.clear();
    g_sent.clear();
}

Stats stats(uint8_t kind) { return g_stats[kind]; }

std::string list(const char* kind_name)
{
    const Kind* k = nullptr;
    for (auto& x : g_kinds)
        if (!strcmp(x.name, kind_name))
            k = &x;
    if (!k)
        return std::string("no kind ") + kind_name + "\n";
    std::string s;
    char b[160];
    if (is_host()) { // the owner's objects: the game's own values
        struct Acc {
            const Kind* k;
            std::string* s;
        } a{k, &s};
        k->each(
            [](uint16_t id, void* obj, void* u) {
                auto* a = (Acc*)u;
                if (a->s->size() > 60000)
                    return;
                char b[160];
                int n = snprintf(b, sizeof b, "obj %s %u", a->k->name, id);
                for (int i = 0; i < a->k->nfields && n > 0 && n < 120; i++)
                    n += snprintf(b + n, sizeof b - n, " %s=%.3f", a->k->fields[i].name, a->k->fields[i].read(obj));
                *a->s += std::string(b) + "\n";
            },
            &a);
        return s;
    }
    // a guest: every id it has a value of, its mirror's value read from the game and the host's
    for (auto& [key, w] : g_want) {
        if ((key >> 24) != k->id || s.size() > 60000)
            continue;
        const Field* f = field_of(*k, (key >> 16) & 0xFF);
        void* obj = k->mirror(key & 0xFFFF);
        if (!f)
            continue;
        snprintf(b, sizeof b, "obj %s %u %s=%.3f host=%.3f\n", k->name, key & 0xFFFF, f->name, obj ? f->read(obj) : -1.f, w.v);
        s += b;
    }
    return s;
}

std::string status()
{
    std::string s;
    char b[256];
    for (auto& k : g_kinds) {
        Stats st = g_stats[k.id];
        std::string names;
        for (int i = 0; i < k.nfields; i++)
            names += std::string(i ? "," : "") + k.fields[i].name;
        snprintf(b, sizeof b, "fields %s (kind %u, %s): on=%d active=%d sent=%u received=%u applied=%u reapplied=%u "
                 "pending=%u checked=%u differ=%u\n", k.name, k.id, names.c_str(), (!k.on || *k.on) ? 1 : 0,
                 (!k.active || k.active()) ? 1 : 0, st.sent, st.received, st.applied, st.reapplied, st.pending,
                 st.checked, st.differ);
        s += b;
    }
    snprintf(b, sizeof b, "fields wanted=%zu\n", g_want.size());
    return s + b;
}

} // namespace fields
