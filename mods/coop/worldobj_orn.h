// Portable sync rules for Creature interactive ornaments (bones / fossils, BUG-0459, BUG-1251, BUG-1261).
// Included by worldobj.cpp and by native unit tests (tests/core_tests.cpp).
// BUG-1261: the idea and the first version are fix/agy-open-bugs 576df657 (an outside agent), ported with the review's
// fixes (out/research/agy-review-misc.md findings 2, 6, 7): the old-peer gate (kFadeMinor) and the trust in the host's
// list before a copy the host no longer has goes (gone_trusted).
#pragma once

#include <cstdint>

namespace wobj_orn {

// Replicated uint32_t state bits for kind "interactive_ornament".
// An older peer reads and writes bit 0 (kUsed) only. A guest reports a fade to the host (acted) and takes the host's
// kGone only under a host of creature kFadeMinor or newer: a 0.7.16.1 host's orn_act marks any reported state used
// (a guest's own tier cleanup would spend the host's bones), and it may send an empty list while its reward handler
// is not there yet (every copy would go on the guest).
constexpr uint32_t kUsed = 1u;      // reward +0x24 != 0 (used up)
constexpr uint32_t kFading = 2u;    // cInteractiveOrnament +0x238: bit 1 set, bits 0 and 2 clear (fading to destroy)
constexpr uint32_t kGone = 0xFFFFFFFFu;
constexpr uint16_t kFadeMinor = 3;     // creature family minor (WP-0 of 0.7.17) from which the host sends and reads fades
constexpr double kGoneSettleS = 10.0;  // a copy the host's list lacks goes only this long after the host was away or
                                       // this game's world changed (the host's rewards are made at the first lookup)

// guest: may a copy the host's snapshot lacks (kGone) go? fix: wobj.ornament_fade; host_new: the host is of
// kFadeMinor; unsettled_at: the last time the host was away, the world changed or this game left the stage
inline bool gone_trusted(bool fix, bool host_new, double now, double unsettled_at)
{
    return fix && host_new && now - unsettled_at >= kGoneSettleS;
}

inline bool is_used(uint32_t st) { return st != kGone && (st & kUsed) != 0; }
inline bool is_fading(uint32_t st) { return st != kGone && (st & kFading) != 0; }

// cInteractiveOrnament::Update skips fading when +0x238 bit 0 is set or bit 1 is clear,
// and hides instead of destroying when bit 2 is set.
inline bool flags_fading_to_destroy(uint32_t orn_flags) { return (orn_flags & 0x7u) == 0x2u; }

// Encode local ornament state from its reward used flag and ornament fade flags (+0x238).
inline uint32_t encode_state(bool reward_used, uint32_t orn_flags, bool fade_fix)
{
    uint32_t st = reward_used ? kUsed : 0u;
    if (fade_fix && flags_fading_to_destroy(orn_flags))
        st |= kFading;
    return st;
}

struct Plan {
    bool use = false;  // run NextModel (if enabled), mark reward used, clear +0xA3
    bool fade = false; // start fade-out to destroy (once, never restarting an active fade)
};

// Local actions needed to catch `local` up to `remote` (host snapshot state, kGone, or guest act state).
// When fade_fix is false, reproduces the 0.7.15 behavior (only kUsed, ignoring kFading and kGone).
inline Plan apply_plan(uint32_t local, uint32_t remote, bool fade_fix)
{
    Plan p{};
    if (remote == kGone) {
        if (fade_fix && !is_fading(local)) {
            p.use = !is_used(local);
            p.fade = true;
        }
        return p;
    }
    if (is_used(remote) && !is_used(local))
        p.use = true;
    if (fade_fix && is_fading(remote) && !is_fading(local)) {
        p.use = !is_used(local);
        p.fade = true;
    }
    return p;
}

inline bool behind(uint32_t local, uint32_t host, bool fade_fix)
{
    Plan p = apply_plan(local, host, fade_fix);
    return p.use || p.fade;
}

// Guest -> host action detection:
// - A transition from unused to used (when the host is neither used nor already fading) requests use on the host.
// - With fade_fix on, a guest ornament that is fading to destroy (or finished fading to kGone after being seen fading)
//   while the host's copy is not yet fading requests fade-out on the host too.
inline bool acted(uint32_t was, uint32_t now, uint32_t host, bool fade_fix)
{
    if (host == kGone)
        return false;
    if (fade_fix && is_fading(host))
        return false;
    if (now == kGone)
        return fade_fix && is_fading(was);
    bool used_act = !is_used(was) && is_used(now) && !is_used(host);
    bool fade_act = fade_fix && is_fading(now);
    return used_act || fade_act;
}

} // namespace wobj_orn
