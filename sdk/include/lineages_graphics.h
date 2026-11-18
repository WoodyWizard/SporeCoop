#pragma once

#include <windows.h>
#include <cwchar>
#include <string>

namespace lineages_graphics {

struct Settings {
    int fps = 0; // zero selects the game's default
    bool borderless = false;
};

inline constexpr wchar_t file_name[] = L"lineages-graphics.ini";

inline Settings read(const std::wstring& file)
{
    wchar_t text[24]{};
    GetPrivateProfileStringW(L"graphics", L"display", L"0,0", text, 24, file.c_str());
    Settings settings;
    int borderless = 0;
    if (swscanf(text, L"%d,%d", &settings.fps, &borderless) != 2 ||
        settings.fps < 0 || settings.fps > 360 || borderless < 0 || borderless > 1)
        return {};
    settings.borderless = borderless != 0;
    return settings;
}

inline bool write(const std::wstring& file, Settings settings)
{
    if (settings.fps < 0 || settings.fps > 360)
        return false;
    const auto value = std::to_wstring(settings.fps) + (settings.borderless ? L",1" : L",0");
    return WritePrivateProfileStringW(L"graphics", L"display", value.c_str(), file.c_str()) != FALSE;
}

// the mod's own HUD size in percent (the Party panel, the partner arrows; BUG-9191), 100 = the mockups' size;
// its own key, so readers of `display` (SporeKit, the installer) are not touched
inline int read_ui_scale(const std::wstring& file)
{
    const int v = (int)GetPrivateProfileIntW(L"graphics", L"ui_scale", 100, file.c_str());
    return v < 50 || v > 200 ? 100 : v;
}

inline bool write_ui_scale(const std::wstring& file, int percent)
{
    if (percent < 50 || percent > 200)
        return false;
    return WritePrivateProfileStringW(L"graphics", L"ui_scale", std::to_wstring(percent).c_str(), file.c_str()) != FALSE;
}

} // namespace lineages_graphics
