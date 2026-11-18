// Incremental creation-library exchange. No game code or file paths on the wire.
#pragma once
#include "buffer.h"
#include <functional>
#include <map>
#include <set>

namespace library_sync {
constexpr uint32_t kMaxFile = 8u << 20;
constexpr size_t kChunk = 800;
struct Item {
    std::string hash, author, name, source, sharer;
    uint32_t size = 0;
};
bool valid_hash(const std::string &hash);
bool valid_png(const std::vector<uint8_t> &bytes);
void write_item(net::Writer &w, const Item &item);
bool read_item(net::Reader &r, Item &item);

class Exchange {
  public:
    // The host may offer current-session imports; clients offer only local work.
    // Append-only during a session. Reset the exchange before replacing it.
    std::function<const std::vector<Item> &()> inventory;
    std::function<bool(const Item &)> eligible;
    // BUG-9112: per receiving peer (the host offers by each peer's Foreign Creations choice)
    std::function<bool(uint8_t, const Item &)> eligible_to;
    std::function<bool(const Item &)> have;
    std::function<bool(const std::string &, std::vector<uint8_t> &)> load;
    std::function<bool(uint8_t, const Item &, const std::vector<uint8_t> &)> store;
    std::function<void(uint8_t, const Item &)> seen;
    std::function<bool(uint8_t, const net::Writer &)> send;
    void join(uint8_t peer);
    void leave(uint8_t peer);
    void reset();
    void receive(uint8_t peer, net::Reader &r);
    // At most one data chunk per peer per call; caller enforces time/backpressure.
    void tick(uint8_t peer);
    size_t incoming_bytes() const;

  private:
    struct Peer {
        std::set<std::string> offered;
        Item out, in;
        std::vector<uint8_t> sending, receiving;
        size_t offset = 0;
        size_t cursor = 0;
        bool waiting = false, sending_data = false;
    };
    std::map<uint8_t, Peer> peers;
};
} // namespace library_sync
