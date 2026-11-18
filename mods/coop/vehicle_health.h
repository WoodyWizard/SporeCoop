// Native Civ vehicles and Space ships share the cCombatant HP fields.
#pragma once

#include <cstdint>

struct VehicleHealthScale {
    uint32_t serial;
    float factor, native_max, scaled_max;
    float before_save_hp = 0, before_save_max = 0;
    bool seen = true;

    void normalize(float& hp, float& max_hp) const
    {
        bool full = hp == max_hp;
        // Retain an outside modifier that changed max HP while scaled.
        max_hp = max_hp == scaled_max ? native_max : max_hp / factor;
        hp = full ? max_hp : hp / factor;
    }

    void before_save(float& hp, float& max_hp)
    {
        before_save_hp = hp;
        before_save_max = max_hp;
        normalize(hp,max_hp);
    }

    void after_save(float& hp, float& max_hp) const
    {
        hp = before_save_hp;
        max_hp = before_save_max;
    }
};
