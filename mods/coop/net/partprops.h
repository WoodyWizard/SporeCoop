// BUG-0559 (class BUG-0424): a part's description from its property list (a rigblock prop, type
// 0x00B1B104: the part's role, its ability levels, cost, editor ranks, bounding box) and the choice of
// the most similar part a game has for a part it lacks. Plain C++ (no game code) so the host build
// can test it (tests/net_tests.cpp) and fuzz it (tests/fuzz_decoders.cpp): the property list of a
// part the receiving game lacks comes from the other player. docs/re/creations.md#creations-lookalike
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace partprops {

constexpr int kNone = -0x7FFFFFFF; // an int property the part does not have

struct Part {
    uint32_t id = 0;
    std::vector<uint32_t> roles;                  // the 0xB00F0FXX role properties (eye 0FE9, mouth 0FEC, ...), sorted
    std::vector<std::pair<uint32_t, int>> abil;   // the ability levels (int properties 0x??C3E5XX), sorted by id
    int cost = kNone;                             // 0x02166464
    int rank[3] = {kNone, kNone, kNone};          // 0x3C652302, 0x51FA9DFA, 0x03A289AC (editor ranks)
    bool has_box = false;
    float box[6] = {};                            // 0x1CBB2F62: min xyz, max xyz (little endian floats)
    std::vector<uint32_t> props;                  // every property id, sorted
    bool detail() const { return roles.empty(); }
    float size() const;                           // the box's diagonal (0: no box)
};

// parses a part prop (big endian count; each property: id u32, type u16, flags u16, then the value or an
// array: count u32, item size u32, items). False when the bytes are not a property list this code reads
// (an unknown value type, a size past the end, more than 4096 properties).
bool parse(const uint8_t* d, size_t n, uint32_t id, Part& out);

// how different b is from a (0: the same description); +inf when b cannot stand for a (another role set)
double distance(const Part& a, const Part& b);

// the 0xFE bytes of an id (a model writes 0xFE as two bytes: a substitute must have as many to fit in place)
int fe_count(uint32_t id);

// the part of `have` most similar to `want` with the same role set and as many 0xFE bytes as `want.id`
// (the id is rewritten in place); 0 when none. Ties: the lower id (the same answer on every machine).
uint32_t nearest(const Part& want, const std::vector<Part>& have);

} // namespace partprops
