// Portable sync rules for city decorations (kind "city_ornament", BUG-0541, BUG-1456).
// Included by worldobj.cpp and by native unit tests (tests/core_tests.cpp).
//
// Each city decoration (cOrnament, noun 0x018C88E4, model tag +0x228 == 0x02AE5BA7) carries its
// model variant id at +0x22C (street lamp, tree planter, fountain, brazier, etc.). The graphics
// factory (SporeApp.exe 0x00E8F740) resolves (+0x22C, 0, 0x9430ADD7) as the ResourceKey, and
// cSpatialObject::SetModelKey (spatial +0x70 vt +0x94 -> 0x00C889A0) updates the live 3D model
// on the next frame (cModelGfx::Update 0x00E92CE1).
//
// Wire compatibility: the existing u32 state word of MSG_WOBJ snapshots carried 0 before 0.7.99.
// An older guest ignores non-kGone states (corn_behind only checked host == kGone) and ignores
// city_ornament in Civ; a newer guest treats host_state == 0 from an older host as "keep model".
#pragma once

#include <cstdint>

namespace wobj_corn {

constexpr uint32_t kOrnamentNoun = 0x018C88E4u;
constexpr uint32_t kCityOrnModel = 0x02AE5BA7u; // cOrnament +0x228: city decoration marker
constexpr uint32_t kCityOrnGroup = 0x9430ADD7u; // ResourceKey.group for city decoration models (0x00E8F797)
constexpr uint32_t kGone = 0xFFFFFFFFu;

// Effective stage bitmask for a world-object kind with an optional fix-gated stage extension.
inline uint8_t effective_stages(uint8_t base_stages, uint8_t extra_stages, bool extra_on)
{
    return (uint8_t)(base_stages | (extra_on ? extra_stages : 0u));
}

inline bool stage_active(uint8_t base_stages, uint8_t extra_stages, bool extra_on, uint8_t sb)
{
    return (effective_stages(base_stages, extra_stages, extra_on) & sb) != 0;
}

// Replicated u32 state word sent in MSG_WOBJ snapshots: the decoration's model variant (+0x22C)
// when variant sync is enabled, else 0 (pre-0.7.99 behavior). Never collides with kGone.
inline uint32_t encode_state(uint32_t variant, bool model_fix)
{
    if (!model_fix || variant == kGone)
        return 0u;
    return variant;
}

// Whether a bound guest decoration should update its model variant (+0x22C and SetModelKey).
inline bool should_set_model(uint32_t local_variant, uint32_t host_state, bool model_fix)
{
    return model_fix && host_state != 0u && host_state != kGone && local_variant != host_state;
}

// Whether a bound guest decoration lags the host (either removed or different model variant).
inline bool behind(uint32_t local_variant, uint32_t host_state, bool model_fix)
{
    return host_state == kGone || should_set_model(local_variant, host_state, model_fix);
}

// Fill the ResourceKey {instance, type, group} expected by cSpatialObject::SetModelKey (0x00C889A0).
inline void model_key(uint32_t variant, uint32_t out[3])
{
    out[0] = variant;
    out[1] = 0u;
    out[2] = kCityOrnGroup;
}

} // namespace wobj_corn
