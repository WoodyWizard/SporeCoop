// E1 (BUG-1149): the persistence backend of a Space kept store (space_kept.h KeptStore) in the per-player store on the
// host's disk (store.h, player_store.h). One record per player key of the feature (`feature` v`ver`): read into the
// KeptStore when the Space world opens (restore(): the player's next join gets it back as after a rejoin), written from
// it at every save of the game. docs/01-architecture.md#arch-player-store.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "space_kept.h"
#include "store.h"

namespace kept_disk {

template <class T>
struct Backend {
    const char* feature;
    uint16_t ver;
    std::function<std::vector<uint8_t>(const T&)> encode;
    std::function<bool(const std::vector<uint8_t>&, T&)> decode;
    uint32_t serial = 0; // the store (store::serial) the KeptStore holds the values of; 0 none
    uint32_t read = 0, skipped = 0, written = 0;

    // the host in Space, every frame after KeptStore::world(): `emptied` = it returned true (another Space world).
    // The values of the store opened now go into the KeptStore (once per store and per Space world)
    void sync(spacekept::KeptStore<T>& st, bool emptied, bool in_space)
    {
        pstore::Store* w = store::world();
        if (!in_space || !w || (!emptied && serial == store::serial()))
            return;
        serial = store::serial();
        for (auto& key : w->keys_with(feature)) {
            const pstore::Record* r = w->get(key, feature);
            T v{};
            if (!r || r->ver != ver || !decode(r->data, v)) {
                skipped++; // another version's record: left on disk as it is
                continue;
            }
            if (st.restore(key, w->name_of(key), std::move(v)))
                read++;
        }
    }
    // the game saves: the KeptStore's values into the store, if they are this store's world
    void save(spacekept::KeptStore<T>& st, pstore::Store& s, int64_t now, bool drop_empty)
    {
        if (!serial || serial != store::serial() || &s != store::world())
            return;
        for (auto& [key, e] : st.entries())
            if (e.have && pstore::key_ok(key) && s.put(key, e.name, feature, ver, encode(e.value), now, drop_empty))
                written++;
    }
    std::string status() const
    {
        return std::string(feature) + " v" + std::to_string(ver) + ": store " + std::to_string(serial) + " read " +
               std::to_string(read) + " skipped " + std::to_string(skipped) + " written " + std::to_string(written);
    }
};

} // namespace kept_disk
