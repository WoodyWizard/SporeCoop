// Helpers for calling into SporeApp.exe (MSVC 2008, x86) from mingw/clang code.
//
// Game classes are NOT mirrored as C++ classes (vtable/ABI differences). Instead:
//   * fields are read by offset:           field<float>(obj, 0x1C)
//   * non-virtual methods by address:      thiscall<int>(addr, obj, args...)
//   * virtual methods by vtable index:     vcall<int>(obj, index, args...)
// Methods returning structs by value use MSVC's hidden return pointer, passed
// right after `this`: declare them explicitly as thiscall<T*>(addr, obj, &ret, ...).
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <cstring>

#define SK_THISCALL __attribute__((thiscall))
#define SK_STDCALL __attribute__((stdcall))
#define SK_CDECL __attribute__((cdecl))

namespace skg {

template <typename T>
inline T& field(void* obj, uintptr_t off)
{
    return *reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(obj) + off);
}

template <typename T>
inline T* ptr_add(void* obj, intptr_t off)
{
    return reinterpret_cast<T*>(reinterpret_cast<uint8_t*>(obj) + off);
}

template <typename R, typename... A>
inline R thiscall(uintptr_t addr, void* self, A... a)
{
    using F = R(SK_THISCALL*)(void*, A...);
    return reinterpret_cast<F>(addr)(self, a...);
}

template <typename R, typename... A>
inline R cdecl_call(uintptr_t addr, A... a)
{
    using F = R(SK_CDECL*)(A...);
    return reinterpret_cast<F>(addr)(a...);
}

template <typename R, typename... A>
inline R stdcall_call(uintptr_t addr, A... a)
{
    using F = R(SK_STDCALL*)(A...);
    return reinterpret_cast<F>(addr)(a...);
}

inline void** vtable(void* obj) { return *reinterpret_cast<void***>(obj); }

template <typename R, typename... A>
inline R vcall(void* self, int index, A... a)
{
    using F = R(SK_THISCALL*)(void*, A...);
    return reinterpret_cast<F>(vtable(self)[index])(self, a...);
}

// ---- common math types (layout-compatible with Spore's Math::) ----
struct Vector3 {
    float x, y, z;
};
struct Quaternion {
    float x, y, z, w;
};
struct ResourceKey {
    uint32_t instance, type, group;
};

// BUG-0791 (measurement): readable() calls of this module (one VirtualQuery each, a syscall), and for a cost check
// readable_extra more VirtualQuery per call (tests compare the frame time with 1 and with 1 + extra queries).
inline volatile LONG readable_calls = 0;
inline int readable_extra = 0;

// Basic memory sanity check before dereferencing game pointers from tools.
inline bool readable(const void* p, size_t n)
{
    MEMORY_BASIC_INFORMATION mbi;
    InterlockedIncrement(&readable_calls);
    for (int i = 0; i < readable_extra; i++)
        VirtualQuery(p, &mbi, sizeof mbi);
    const uint8_t* a = static_cast<const uint8_t*>(p);
    const uint8_t* end = a + n;
    while (a < end) {
        if (!VirtualQuery(a, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT)
            return false;
        DWORD pr = mbi.Protect & 0xFF;
        if (pr == PAGE_NOACCESS || pr == PAGE_EXECUTE || (mbi.Protect & PAGE_GUARD))
            return false;
        a = static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
    }
    return true;
}

} // namespace skg
