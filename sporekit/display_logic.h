// Pure portable logic for BUG-0865: frame rate target (1-360 fps) pacing, game frame-ms override,
// and borderless windowed mode style/rect transitions. No Windows headers so native Linux unit tests
// can exercise every edge case under ASan/UBSan.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

namespace skdisp {

constexpr int kFpsAuto = 0;
constexpr int kFpsMin = 1;
constexpr int kFpsMax = 360;

// Presets cycled by the Settings UI button (0 = Auto, game default)
constexpr int kFpsPresets[] = {0, 30, 60, 90, 120, 144, 165, 240, 360};
constexpr int kFpsPresetCount = (int)(sizeof(kFpsPresets) / sizeof(kFpsPresets[0]));

inline int clamp_fps(int fps)
{
    if (fps <= 0)
        return kFpsAuto;
    return std::clamp(fps, kFpsMin, kFpsMax);
}

inline bool parse_fps_arg(const char* s, int& out_fps)
{
    if (!s || !*s)
        return false;
    if (!strcmp(s, "auto") || !strcmp(s, "off") || !strcmp(s, "default") || !strcmp(s, "0")) {
        out_fps = kFpsAuto;
        return true;
    }
    char* end = nullptr;
    long v = strtol(s, &end, 10);
    if (!end || *end != '\0')
        return false;
    if (v < kFpsMin || v > kFpsMax)
        return false;
    out_fps = (int)v;
    return true;
}

inline bool parse_bool_arg(const char* s, bool& out)
{
    if (!s || !*s)
        return false;
    if (!strcmp(s, "on") || !strcmp(s, "1") || !strcmp(s, "true") || !strcmp(s, "yes")) {
        out = true;
        return true;
    }
    if (!strcmp(s, "off") || !strcmp(s, "0") || !strcmp(s, "false") || !strcmp(s, "no")) {
        out = false;
        return true;
    }
    return false;
}

inline int next_fps_preset(int cur)
{
    if (cur < 0)
        cur = 0;
    for (int i = 0; i < kFpsPresetCount; i++) {
        if (kFpsPresets[i] > cur)
            return kFpsPresets[i];
    }
    return kFpsPresets[0];
}

inline std::string fps_label(int fps)
{
    fps = clamp_fps(fps);
    if (fps <= 0)
        return "Auto";
    return std::to_string(fps) + " fps";
}

// Game kFrameLimitMS override: when a live frame target (1..360 fps) is active and enabled,
// lift Spore's internal Sleep-based cap to 0 so SporeKit's waitable timer paces the frames.
inline int desired_frame_ms(int frame_target_fps, bool target_fix_on, int forced_ms, int menu_ms, bool in_menu,
                            int game_own_ms)
{
    if (target_fix_on && frame_target_fps > 0)
        return 0;
    if (forced_ms >= 0)
        return forced_ms;
    if (in_menu && menu_ms >= 0)
        return menu_ms;
    return game_own_ms;
}

// ---- High-resolution frame pacer math (nanoseconds) ----
inline int64_t frame_period_ns(int fps)
{
    fps = clamp_fps(fps);
    return fps > 0 ? (1000000000LL / fps) : 0;
}

struct PacerState {
    int64_t next_ns = 0;
};

// Advance pacer by one frame at timestamp `now_ns`. Returns how many nanoseconds to wait
// before presenting/proceeding (0 if already at or past deadline, or disabled).
inline int64_t pace_step(PacerState& st, int fps, int64_t now_ns)
{
    int64_t period = frame_period_ns(fps);
    if (period <= 0) {
        st.next_ns = 0;
        return 0;
    }
    if (st.next_ns <= 0) {
        st.next_ns = now_ns + period;
        return 0;
    }
    int64_t wait_ns = st.next_ns - now_ns;
    if (wait_ns > 0) {
        int64_t max_wait = std::min<int64_t>(period * 2, 1000000000LL);
        if (wait_ns > max_wait) {
            wait_ns = period;
            st.next_ns = now_ns + period;
        }
        st.next_ns += period;
        return wait_ns;
    }
    // Behind schedule: absorb small jitter, resync cleanly after a hitch (> 2 frames late)
    if (now_ns - st.next_ns > period * 2)
        st.next_ns = now_ns + period;
    else
        st.next_ns += period;
    return 0;
}

// Split a wait into a coarse OS timer sleep plus a short tail spin/yield for sub-ms accuracy.
// Returns the timer sleep portion in 100-nanosecond ticks (for SetWaitableTimer), or 0 for spin-only.
inline int64_t timer_sleep_100ns(int64_t wait_ns, bool high_res_timer)
{
    const int64_t spin_margin_ns = high_res_timer ? 500000LL : 1500000LL;
    if (wait_ns <= spin_margin_ns)
        return 0;
    return (wait_ns - spin_margin_ns) / 100LL;
}

// ---- Borderless windowed mode logic ----
// Matches Win32 WS_OVERLAPPEDWINDOW (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
constexpr uint32_t kStyleOverlappedWindow = 0x00CF0000u;
constexpr uint32_t kStylePopup = 0x80000000u;
// WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE
constexpr uint32_t kExBorderMask = 0x00020301u;

struct RectI {
    int32_t left = 0, top = 0, right = 0, bottom = 0;
    int32_t width() const { return right - left; }
    int32_t height() const { return bottom - top; }
    bool valid() const { return right > left && bottom > top; }
    bool operator==(const RectI& o) const
    {
        return left == o.left && top == o.top && right == o.right && bottom == o.bottom;
    }
    bool operator!=(const RectI& o) const { return !(*this == o); }
};

inline uint32_t borderless_style(uint32_t style) { return style & ~kStyleOverlappedWindow; }

inline uint32_t borderless_exstyle(uint32_t exstyle) { return exstyle & ~kExBorderMask; }

inline bool has_window_borders(uint32_t style) { return (style & kStyleOverlappedWindow) != 0; }

inline bool needs_borderless_apply(uint32_t cur_style, const RectI& cur_rect, const RectI& mon_rect)
{
    if (!mon_rect.valid())
        return false;
    return has_window_borders(cur_style) || cur_rect != mon_rect;
}

enum class BorderlessAction { None, Apply, Restore };

struct BorderlessState {
    bool enabled = false;
    bool video_windowed = true;
    bool applied = false;
    bool have_saved = false;
    uint32_t saved_style = 0;
    uint32_t saved_exstyle = 0;
    RectI saved_rect{};
};

inline BorderlessAction decide_borderless(const BorderlessState& st, bool fix_on, uint32_t cur_style,
                                          const RectI& cur_rect, const RectI& mon_rect)
{
    if (!st.video_windowed)
        return BorderlessAction::None;
    if (st.enabled && fix_on) {
        if (!st.applied || needs_borderless_apply(cur_style, cur_rect, mon_rect))
            return mon_rect.valid() ? BorderlessAction::Apply : BorderlessAction::None;
        return BorderlessAction::None;
    }
    if (st.applied && st.have_saved)
        return BorderlessAction::Restore;
    return BorderlessAction::None;
}

} // namespace skdisp
