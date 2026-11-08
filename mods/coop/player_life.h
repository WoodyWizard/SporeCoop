// BUG-0422 (class: per-player state under a reused id): one registry of every module's state keyed by a player id.
// The host gives a newcomer the lowest free id (net/session.cpp alloc_id), so a player who joins after another left
// gets the departed player's id. Each module that keeps a map keyed by player id registers here (PLAYER_STATE in its
// .cpp, next to the map): `forget` drops what it holds for the id, `held` counts it. coop.cpp calls forget_all on
// every leave and on every join, so a module added later cannot miss the fan-out (0.7.10 called five modules by
// hand; spacecoop, space_rivalry, tribe, civ, ... kept the departed player's star, budget, chief, siege).
// `coop.players held <id>` lists what is still held (tests); `coop.fix core.player_forget off`: as 0.7.10.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace player_life {

struct Entry {
    const char* name;
    void (*forget)(uint8_t id);
    size_t (*held)(uint8_t id);
};

inline std::vector<Entry>& table()
{
    static std::vector<Entry> t;
    return t;
}

struct Reg {
    Reg(const char* name, void (*forget)(uint8_t), size_t (*held)(uint8_t)) { table().push_back({name, forget, held}); }
};

inline void forget_all(uint8_t id)
{
    for (auto& e : table())
        e.forget(id);
}

// "<module> <n>" for every module that still holds something of `id`, empty when none
inline std::string held(uint8_t id)
{
    std::string out;
    for (auto& e : table())
        if (size_t n = e.held(id)) {
            out += e.name;
            out += ' ';
            out += std::to_string(n);
            out += '\n';
        }
    return out;
}

} // namespace player_life

#define PLAYER_STATE_CAT2(a, b) a##b
#define PLAYER_STATE_CAT(a, b) PLAYER_STATE_CAT2(a, b)
// PLAYER_STATE("module", [](uint8_t id) { ... }, [](uint8_t id) -> size_t { ... });
#define PLAYER_STATE(name, forget, held) static player_life::Reg PLAYER_STATE_CAT(g_player_state_, __LINE__)(name, forget, held)
