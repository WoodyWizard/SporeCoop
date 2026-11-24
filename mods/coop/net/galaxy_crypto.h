// Decision 8Q A (owner 2026-11-23: "8QA"): the galaxy traffic is encrypted. Portable: the galaxy server (server/galaxy)
// and the game share this file. Crypto: Monocypher 4.0.2 (third_party/monocypher, BSD-2 / CC0, vendored).
//
// The key exchange, in clear (before HELLO):
//   game -> server  'K' 1, u8 version, 32 the game's ephemeral X25519 public key, zero padding to kKeyxHelloBytes
//                   (the server's answer is never bigger than the question: no amplification)
//   server -> game  'K' 2, u8 version, 32 the server's static public key, 32 the server's ephemeral public key
// Both take BLAKE2b-512 of kKeyxContext, the three public keys and the two X25519 secrets (the game's ephemeral key
// with the server's static key, and with its ephemeral key): the first 32 bytes are the game -> server key, the next
// 32 the server -> game key. The game keeps each server's static key (trust on first use) and refuses a server whose
// key changed: nobody between them can stand in for a server the game knew.
// Then every datagram both ways is sealed: 'E', u64 counter (little-endian, the nonce's first 8 bytes; one key per
// direction), 16 the MAC, then the datagram as it was ('G', 'R' or 'F' and its body) encrypted with XChaCha20-Poly1305
// (the 9 header bytes are its additional data). A counter seen before, or 64 below the highest seen, is dropped.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace net::galaxy::crypto {

constexpr uint8_t kKeyx = 'K', kSealed = 'E';
constexpr uint8_t kKeyxAsk = 1, kKeyxAnswer = 2;
// server -> game, 2 bytes: a sealed datagram from an address it has no keys for (it restarted): exchange keys again
constexpr uint8_t kKeyxNone = 3;
constexpr uint8_t kKeyxVersion = 1;
constexpr size_t kKeyxHelloBytes = 96;         // the question, padded
constexpr size_t kKeyxAnswerBytes = 1 + 1 + 1 + 64; // 'K', kind, version, two keys
constexpr size_t kHeader = 1 + 8, kMac = 16, kOverhead = kHeader + kMac;
constexpr const char* kKeyxContext = "SporeCoop galaxy key exchange v1";

// the system's random generator (BCryptGenRandom on Windows, getrandom / /dev/urandom elsewhere)
bool random_bytes(void* out, size_t n);

struct KeyPair {
    uint8_t secret[32]{};
    uint8_t pub[32]{};
    bool make();              // a new random pair
    void from_secret();       // pub from secret
    void wipe();
};

struct Channel {
    bool on = false;
    uint8_t tx[32]{}, rx[32]{};
    uint64_t tx_counter = 0;
    uint64_t rx_top = 0;  // the highest counter opened
    uint64_t rx_bits = 0; // bit i: rx_top - i was opened
    uint64_t sealed = 0, opened = 0, refused = 0, replays = 0;
    void wipe();
    // `in` (a whole datagram as it would go in clear) -> 'E' ...; false: the channel is off
    bool seal(const uint8_t* in, size_t n, std::vector<uint8_t>& out);
    // an 'E' datagram -> the datagram it holds; false: damaged, forged, a replay, or the channel is off
    bool open(const uint8_t* in, size_t n, std::vector<uint8_t>& out);
};

// the game: its question for this exchange (eph: a new pair per exchange)
std::vector<uint8_t> keyx_question(const KeyPair& eph);
// the game: the server's answer -> the channel (game side) and the server's static key; false: not an answer
bool keyx_game(const KeyPair& eph, const uint8_t* answer, size_t n, uint8_t server_static[32], Channel& out);
// the server: a game's question -> its answer and the channel (server side); false: not a question
bool keyx_server(const KeyPair& server_static, const uint8_t* question, size_t n, std::vector<uint8_t>& answer,
                 Channel& out);

std::string hex(const uint8_t* p, size_t n);
bool unhex(const std::string& s, uint8_t* out, size_t n);
// a short fingerprint of a public key for logs and the status (the first 8 bytes of its BLAKE2b, hex)
std::string fingerprint(const uint8_t pub[32]);

} // namespace net::galaxy::crypto
