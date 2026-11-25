// Cell warm reconnect receipt ledger, guest state machine, and MSG_WORLD_DONE wire extension
// (BUG-0869, 0.8.0 WP-C). Portable (no Windows, no SporeKit): unit-tested natively in tests/net_tests.cpp.
#pragma once

#include <array>
#include <cstdint>
#include <string>

#include "net/buffer.h"

namespace world_resume {

// Mode ids matching modes.h (world.cpp static_asserts they agree).
constexpr uint32_t kCellMode = 0x1654C00u;
constexpr uint32_t kEditMode = 0x1654C06u;
constexpr uint32_t kLoadMode = 0x1654C08u;
constexpr uint32_t kEditorMode = 0x00DBDBA1u;
constexpr uint32_t kMenuMode = 0x02CCD1D2u;

// Host ring buffer capacity for recent Cell world share receipts (BUG-0869).
constexpr size_t kMaxReceipts = 32;

// MSG_WORLD_DONE trailing flags (world family 1.2, BUG-0869).
// Older guests ignore trailing bytes on MSG_WORLD_DONE (and ignore standalone MSG_WORLD_DONE
// when no MSG_WORLD_OFFER is active).
enum DoneFlags : uint8_t {
    kFlagResumed = 1, // host accepted guest's resume_receipt; no MSG_WORLD_OFFER/FILE/CHUNK sent
};

struct WorldDone {
    uint32_t world_id = 0; // CRC32 of the transferred files (or original share's id on resume ack)
    bool has_receipt = false;
    uint64_t resume_receipt = 0;
    uint8_t flags = 0;

    bool resumed() const { return (flags & kFlagResumed) != 0; }

    void write(net::Writer& w) const
    {
        w.u32(world_id);
        if (has_receipt || flags != 0) {
            w.raw(&resume_receipt, 8);
            w.u8(flags);
        }
    }

    bool read(net::Reader& r)
    {
        world_id = r.u32();
        if (!r.ok())
            return false;
        has_receipt = false;
        resume_receipt = 0;
        flags = 0;
        if (r.left() >= 8) {
            r.raw(&resume_receipt, 8);
            has_receipt = true;
            if (r.left() >= 1)
                flags = r.u8();
        }
        return r.ok();
    }
};

// Unpredictable non-zero 64-bit receipt from counter (QueryPerformanceCounter), monotonic serial,
// process id, and optional salt.
inline uint64_t make_receipt(uint64_t counter, uint64_t serial, uint64_t pid, uint64_t salt = 0)
{
    uint64_t x = counter ^ (serial * 0x9E3779B97F4A7C15ull) ^ (pid << 17) ^ (pid >> 7) ^ salt ^ 0x5C0869CE11A1E5ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x ? x : 1ull;
}

struct HostEntry {
    uint64_t receipt = 0;
    std::string slot_key;           // canonical lowercase "<slot dir>\<planet>.spo" (or slot dir)
    uint32_t world_id = 0;          // planet name CRC32 (modes::current_world())
    uint32_t stage_epoch = 0;       // Cell stage epoch (game::stage_epoch(kCell))
    uint64_t slot_mtime = 0;        // newest file write time of the slot when shared
    uint64_t save_gen = 0;          // monotonic host save generation when shared
    int32_t food_total = 0;         // host shared Cell food total when shared
    uint32_t transfer_world_id = 0; // CRC32 of the shared files (carried in MSG_WORLD_DONE)
};

enum class RejectReason : uint8_t {
    None = 0,
    Disabled,
    NotInCell,
    EmptyReceipt,
    UnknownReceipt,
    WorldMismatch,
    EpochMismatch,
    SlotMismatch,
    SlotModified,
    SaveOccurred,
    SaveInFlight,
};

inline const char* reason_name(RejectReason r)
{
    switch (r) {
    case RejectReason::None: return "ok";
    case RejectReason::Disabled: return "disabled";
    case RejectReason::NotInCell: return "not_in_cell";
    case RejectReason::EmptyReceipt: return "empty_receipt";
    case RejectReason::UnknownReceipt: return "unknown_receipt";
    case RejectReason::WorldMismatch: return "world_mismatch";
    case RejectReason::EpochMismatch: return "epoch_mismatch";
    case RejectReason::SlotMismatch: return "slot_mismatch";
    case RejectReason::SlotModified: return "slot_modified";
    case RejectReason::SaveOccurred: return "save_occurred";
    case RejectReason::SaveInFlight: return "save_in_flight";
    }
    return "unknown";
}

struct HostQuery {
    bool enabled = true;
    uint32_t mode = 0;
    std::string slot_key;
    uint32_t world_id = 0;
    uint32_t stage_epoch = 0;
    uint64_t slot_mtime = 0;
    uint64_t save_gen = 0;
    bool save_in_flight = false;
};

struct ValidateResult {
    bool ok = false;
    RejectReason reason = RejectReason::None;
    const HostEntry* entry = nullptr;
};

// Host ring buffer of up to kMaxReceipts (32) recent Cell world share receipts.
class HostLedger {
public:
    void clear()
    {
        count_ = 0;
        next_ = 0;
    }

    size_t size() const { return count_; }

    void record(const HostEntry& e)
    {
        if (e.receipt == 0)
            return;
        for (size_t i = 0; i < count_; i++) {
            if (ring_[i].receipt == e.receipt) {
                ring_[i] = e;
                return;
            }
        }
        ring_[next_] = e;
        next_ = (next_ + 1) % kMaxReceipts;
        if (count_ < kMaxReceipts)
            count_++;
    }

    const HostEntry* find(uint64_t receipt) const
    {
        if (receipt == 0)
            return nullptr;
        for (size_t i = 0; i < count_; i++) {
            if (ring_[i].receipt == receipt)
                return &ring_[i];
        }
        return nullptr;
    }

    ValidateResult validate(uint64_t receipt, const HostQuery& q) const
    {
        if (!q.enabled)
            return {false, RejectReason::Disabled, nullptr};
        if (q.mode != kCellMode)
            return {false, RejectReason::NotInCell, nullptr};
        if (receipt == 0)
            return {false, RejectReason::EmptyReceipt, nullptr};
        const HostEntry* e = find(receipt);
        if (!e)
            return {false, RejectReason::UnknownReceipt, nullptr};
        if (q.save_in_flight)
            return {false, RejectReason::SaveInFlight, e};
        if (q.world_id == 0 || q.world_id != e->world_id)
            return {false, RejectReason::WorldMismatch, e};
        if (q.stage_epoch == 0 || q.stage_epoch != e->stage_epoch)
            return {false, RejectReason::EpochMismatch, e};
        if (!e->slot_key.empty() && !q.slot_key.empty() && q.slot_key != e->slot_key)
            return {false, RejectReason::SlotMismatch, e};
        if (q.save_gen != e->save_gen)
            return {false, RejectReason::SaveOccurred, e};
        if (e->slot_mtime == 0 || q.slot_mtime != e->slot_mtime)
            return {false, RejectReason::SlotModified, e};
        return {true, RejectReason::None, e};
    }

private:
    std::array<HostEntry, kMaxReceipts> ring_{};
    size_t count_ = 0;
    size_t next_ = 0;
};

// Guest-side Cell warm reconnect receipt state machine (BUG-0869).
// Lifecycle:
//   Empty -> PendingLoad (on full Cell MSG_WORLD_DONE with non-zero receipt + queued load)
//   PendingLoad -> Active (once the queued load finishes and guest enters Cell in a newer epoch)
//   Active stays alive across disconnects/leaves while guest stays in that exact Cell world.
//   Cleared when guest leaves Cell, loads another world, starts hosting, receives a new
//   MSG_WORLD_OFFER, or joins a different host.
class GuestState {
public:
    enum class Phase : uint8_t { Empty, PendingLoad, Active };

    Phase phase() const { return phase_; }
    const char* state_name() const
    {
        switch (phase_) {
        case Phase::Empty: return "empty";
        case Phase::PendingLoad: return "pending";
        case Phase::Active: return "active";
        }
        return "?";
    }

    uint64_t active_receipt() const { return phase_ == Phase::Active ? receipt_ : 0; }
    uint64_t any_receipt() const { return phase_ != Phase::Empty ? receipt_ : 0; }
    uint32_t world_id() const { return world_id_; }
    uint32_t stage_epoch() const { return stage_epoch_; }

    void clear()
    {
        phase_ = Phase::Empty;
        receipt_ = 0;
        host_addr_.clear();
        host_raw_.clear();
        slot_key_.clear();
        world_id_ = 0;
        stage_epoch_ = 0;
        armed_epoch_ = 0;
        armed_ms_ = 0;
    }

    // Called when a full world transfer completed with a non-zero receipt and queued a load.
    void arm_pending(uint64_t receipt, const std::string& host_addr, const std::string& host_raw,
                     const std::string& slot_key, uint32_t world_id, uint32_t cur_cell_epoch, uint64_t now_ms)
    {
        if (receipt == 0) {
            clear();
            return;
        }
        phase_ = Phase::PendingLoad;
        receipt_ = receipt;
        host_addr_ = host_addr;
        host_raw_ = host_raw;
        slot_key_ = slot_key;
        world_id_ = world_id;
        stage_epoch_ = 0;
        armed_epoch_ = cur_cell_epoch;
        armed_ms_ = now_ms;
    }

    // Called on session end (disconnect / leave):
    // PendingLoad is cleared if the queued load was dropped; Active is preserved for reconnect.
    void on_session_end(bool load_dropped)
    {
        if (phase_ == Phase::PendingLoad && load_dropped)
            clear();
    }

    // Returns true on the transition PendingLoad -> Active. Sets `cleared_why` if cleared.
    bool tick(uint32_t mode, uint32_t cell_epoch, uint32_t cur_world_id, const std::string& cur_game_key,
              bool load_queued, uint64_t now_ms, const char** cleared_why = nullptr)
    {
        if (cleared_why)
            *cleared_why = nullptr;
        if (phase_ == Phase::Empty)
            return false;
        if (phase_ == Phase::PendingLoad) {
            if (mode == kCellMode && !load_queued && cell_epoch > armed_epoch_) {
                bool slot_ok = !slot_key_.empty() && !cur_game_key.empty() && cur_game_key == slot_key_;
                bool world_ok = world_id_ != 0 && cur_world_id == world_id_;
                if (slot_ok || (cur_game_key.empty() && world_ok)) {
                    phase_ = Phase::Active;
                    stage_epoch_ = cell_epoch;
                    if (cur_world_id != 0)
                        world_id_ = cur_world_id;
                    if (!cur_game_key.empty())
                        slot_key_ = cur_game_key;
                    return true;
                }
            }
            if (!load_queued && mode != kMenuMode && mode != kLoadMode && mode != kCellMode &&
                mode != kEditMode && mode != kEditorMode) {
                clear();
                if (cleared_why)
                    *cleared_why = "loaded non-Cell stage";
                return false;
            }
            if (now_ms - armed_ms_ > 120000ull) {
                clear();
                if (cleared_why)
                    *cleared_why = "pending load timed out";
                return false;
            }
            return false;
        }
        // Phase::Active
        if (mode != kCellMode && mode != kEditMode && mode != kEditorMode) {
            clear();
            if (cleared_why)
                *cleared_why = "left Cell stage";
            return false;
        }
        if (cell_epoch != stage_epoch_) {
            clear();
            if (cleared_why)
                *cleared_why = "Cell stage epoch changed";
            return false;
        }
        if (mode == kCellMode && cur_world_id != 0 && world_id_ != 0 && cur_world_id != world_id_) {
            clear();
            if (cleared_why)
                *cleared_why = "world changed";
            return false;
        }
        if (mode == kCellMode && !cur_game_key.empty() && !slot_key_.empty() && cur_game_key != slot_key_) {
            clear();
            if (cleared_why)
                *cleared_why = "save slot changed";
            return false;
        }
        return false;
    }

    // Called when starting a join to target host: returns receipt to send in HELLO if active,
    // enabled, and still in the same Cell world for the same host. Clears if switching hosts or
    // if the guest is no longer in that Cell world.
    uint64_t receipt_for_join(bool enabled, const std::string& target_addr, const std::string& target_raw,
                              uint32_t mode, uint32_t cell_epoch, uint32_t cur_world_id)
    {
        if (phase_ != Phase::Active)
            return 0;
        bool same_host = (!target_addr.empty() && !host_addr_.empty() && target_addr == host_addr_) ||
                         (!target_raw.empty() && !host_raw_.empty() && target_raw == host_raw_);
        if (!same_host) {
            clear();
            return 0;
        }
        if (mode != kCellMode || cell_epoch != stage_epoch_ ||
            (cur_world_id != 0 && world_id_ != 0 && cur_world_id != world_id_)) {
            clear();
            return 0;
        }
        if (!enabled)
            return 0;
        return receipt_;
    }

    bool accept_ack(uint64_t ack_receipt) const
    {
        return phase_ == Phase::Active && receipt_ != 0 && ack_receipt == receipt_;
    }

private:
    Phase phase_ = Phase::Empty;
    uint64_t receipt_ = 0;
    std::string host_addr_;
    std::string host_raw_;
    std::string slot_key_;
    uint32_t world_id_ = 0;
    uint32_t stage_epoch_ = 0;
    uint32_t armed_epoch_ = 0;
    uint64_t armed_ms_ = 0;
};

} // namespace world_resume
