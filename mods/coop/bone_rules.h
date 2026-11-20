// BUG-1335: a bone index against a creature's posed bone array (pure, unit-tested in tests/tribe_tests.cpp).
//
// cCreatureBase::CreateEffect 0xC150D0 indexes the model's bone array (AnimatedCreature +0xB54 -> +0x17C, the array
// +0x2E4 .. +0x2E8, 700 bytes a bone) with `bone * 700` and has no check but `bone != -1` (docs/re/creature.md
// #creature-posed-top-portrait, docs/re/tribe.md#tribe-hand-bone). A citizen's hand bone (+0xFC4) comes from its
// species' limb table (+0xE40) when the citizen is made; the bone array is the model's and is built over the next
// frames: a hand item attached in between (the chief's staff of cTribe_SetChieftain) read past the array's end.
#pragma once
#include <cstdint>

namespace bone_rules {

constexpr uint32_t kBoneSize = 700;

enum class Bone {
    Ok,      // inside the array (or -1: the game's own "no bone", CreateEffect returns null for it)
    Out,     // the array is there and the bone is not in it (CreateEffect would read past its end)
    Unknown, // no array to judge by (no body, no model): the caller decides (the game's code checks the body)
};

// begin / end: the array's two pointers (+0x2E4 / +0x2E8); have: the model was readable
inline Bone check(bool have, uint32_t begin, uint32_t end, int bone)
{
    if (bone == -1)
        return Bone::Ok;
    if (!have)
        return Bone::Unknown;
    if (bone < 0)
        return Bone::Out;
    if (!begin || end < begin || (end - begin) % kBoneSize || (end - begin) / kBoneSize > 1000)
        return begin == 0 && end == 0 ? Bone::Out : Bone::Unknown; // (an empty array: nothing to index)
    return (uint32_t)bone < (end - begin) / kBoneSize ? Bone::Ok : Bone::Out;
}

} // namespace bone_rules
