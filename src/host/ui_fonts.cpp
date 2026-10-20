// SPDX-License-Identifier: MIT
//
// See ui_fonts.h. Taken from RAV (viewer_window.cpp WindowsFontPath, UiFontConfig, AddUiFont,
// LoadUiFontAtlas, the ImGui part of StartRendering and StopRendering). ImGui 1.92: the atlas is no longer
// built by hand (ImFontAtlas::Build before the renderer backend says it handles textures asserts); a font
// file that does not load is refused by AddFontFromFileTTF itself.

#include "ui_fonts.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <string>
#include <type_traits>

#include <imgui.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_win32.h>

#include "host_services.h"
#include "ui_glyph_ranges.h"
#include "ui_theme.h"

namespace rav {
namespace host {

// Sizes: CSS px are em sizes, but ImGui's size is the font's ascent + descent height
// (stbtt_ScaleForPixelHeight), and Segoe UI's ascent + descent is 1.33 em (hhea
// 2210 + 514 over 2048). So 12 CSS px = 16 ImGui px (a 16 px line, as a browser lays
// 12 px Segoe UI out), not 12: 12 would draw the letters a quarter smaller than the
// mock-up. Each file carries its own ratio below; all are scaled by the screen's DPI.
//
// Every font gets the Windows symbol fonts merged in for the blocks of
// ui_glyph_ranges.h (Segoe UI punctuation and arrows, then Segoe UI Symbol shapes,
// stars and dingbats, then Arial): the first font holding a code point wins, a font
// missing a glyph adds nothing, and a missing or unreadable file is skipped.
namespace {

struct UiFontFile {
    const wchar_t* name;
    float          height_per_em;   // (hhea ascent + descent) / units per em
};
constexpr UiFontFile kSegoeUi     = {L"segoeui.ttf", 1.330f};
constexpr UiFontFile kSegoeSymbol = {L"seguisym.ttf", 1.330f};
constexpr UiFontFile kArial       = {L"arial.ttf", 1.117f};
// The 600 weight of the mockup's titles (Segoe UI Semibold shares Segoe UI's vertical
// metrics), Arial Bold when it is missing.
constexpr UiFontFile kSegoeSemibold = {L"seguisb.ttf", 1.330f};
constexpr UiFontFile kArialBold     = {L"arialbd.ttf", 1.117f};
constexpr UiFontFile kCascadia    = {L"CascadiaMono.ttf", 1.172f};
constexpr UiFontFile kConsolas    = {L"consola.ttf", 1.171f};

// The UTF-8 path of a file of the Windows Fonts folder, or "" when it is not there.
std::string WindowsFontPath(const std::wstring& fonts_dir, const wchar_t* file)
{
    const std::wstring path = fonts_dir + file;
    const DWORD attr = GetFileAttributesW(path.c_str());
    if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_DIRECTORY)) return {};
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) return {};
    std::string utf8(static_cast<size_t>(bytes), '\0');   // ImGui opens it as UTF-8 (_wfopen)
    if (WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &utf8[0], bytes, nullptr, nullptr) != bytes) return {};
    utf8.resize(static_cast<size_t>(bytes - 1));
    return utf8;
}

// Crisp small text: 2x horizontal oversampling for smooth subpixel placement, glyph
// advances snapped to whole pixels at UI sizes, and a touch more ink, as Segoe UI's
// thin stems at 11-13 px wash out grey under stb_truetype's plain coverage.
ImFontConfig UiFontConfig(float em_px)
{
    ImFontConfig cfg;
    cfg.OversampleH        = em_px >= 24.0f ? 1 : 2;   // large (high-DPI) sizes are smooth already; keeps the atlas small
    cfg.OversampleV        = 1;
    cfg.PixelSnapH         = em_px <= 16.0f;
    cfg.RasterizerMultiply = 1.15f;
    return cfg;
}

// Adds the first of `files` that loads at `em_px` (CSS px, DPI applied), the symbol
// fonts merged in. nullptr when none loaded.
ImFont* AddUiFont(ImGuiIO& io, const std::wstring& fonts_dir, std::initializer_list<UiFontFile> files, float em_px)
{
    ImFont* font = nullptr;
    for (const UiFontFile& f : files) {
        const std::string path = WindowsFontPath(fonts_dir, f.name);
        if (path.empty()) continue;
        ImFontConfig cfg = UiFontConfig(em_px);
        font = io.Fonts->AddFontFromFileTTF(path.c_str(), std::round(em_px * f.height_per_em), &cfg, kUiGlyphRanges);
        if (font) break;
    }
    if (!font) return nullptr;
    for (const UiFontFile& f : {kSegoeUi, kSegoeSymbol, kArial}) {
        const std::string path = WindowsFontPath(fonts_dir, f.name);
        if (path.empty()) continue;
        ImFontConfig cfg = UiFontConfig(em_px);
        cfg.MergeMode = true;
        io.Fonts->AddFontFromFileTTF(path.c_str(), std::round(em_px * f.height_per_em), &cfg, kUiGlyphRanges);
    }
    return font;
}

}  // namespace

float DpiScaleOf(HDC dc)
{
    const int dpi_y = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
    // One scale for the style, the theme and the fonts, clamped to what the fonts accept.
    return std::clamp(dpi_y > 96 ? static_cast<float>(dpi_y) / 96.0f : 1.0f, 1.0f, 4.0f);
}

void LoadUiFontAtlas(ImGuiIO& io, float dpi_scale)
{
    static_assert(std::is_same<ImWchar, std::remove_cv_t<std::remove_extent_t<decltype(kUiGlyphRanges)>>>::value,
                  "kUiGlyphRanges must match ImWchar (16-bit)");
    if (!(dpi_scale >= 1.0f && dpi_scale <= 4.0f)) dpi_scale = 1.0f;
    ui::SetFonts(ui::FontSet{});
    io.Fonts->Flags |= ImFontAtlasFlags_NoPowerOfTwoHeight;   // high DPI: stay under GL_MAX_TEXTURE_SIZE

    wchar_t win_dir[MAX_PATH] = {};
    const UINT dir_len = GetWindowsDirectoryW(win_dir, MAX_PATH);
    if (dir_len == 0 || dir_len >= MAX_PATH) {
        io.Fonts->AddFontDefault();
        Log(LogLevel::Info, "UI fonts: the Windows folder was not found; the built-in font is used");
        return;
    }
    const std::wstring fonts_dir = std::wstring(win_dir) + L"\\Fonts\\";

    // The body font first: the atlas's first font is ImGui's default.
    // The mockup's whole type scale, one font per role, each added at its own size (the theme reads
    // ImFont::LegacySize): Segoe UI 12 (body, controls) / 11 (captions) / 13 (rows, section
    // titles) / 15 / 10 (units, tags) / 9 (badge); Segoe UI Semibold (600) 12 / 11 / 14 / 15
    // for bold names and titles; Cascadia Mono / Consolas 12 / 11 (number fields) / 10 / 9. A
    // role whose file is missing falls back to the nearest loaded one (ui_theme.cpp).
    ui::FontSet set;
    const auto sans = [&](float css) { return AddUiFont(io, fonts_dir, {kSegoeUi, kArial}, css * dpi_scale); };
    const auto bold = [&](float css) {
        return AddUiFont(io, fonts_dir, {kSegoeSemibold, kArialBold}, css * dpi_scale);
    };
    const auto mono = [&](float css) { return AddUiFont(io, fonts_dir, {kCascadia, kConsolas}, css * dpi_scale); };
    set.body = sans(12.0f);
    if (set.body) {
        set.small        = sans(11.0f);
        set.title        = sans(13.0f);
        set.big          = sans(15.0f);
        set.tiny         = sans(10.0f);
        set.badge        = sans(9.0f);
        set.semibold     = bold(12.0f);
        set.caption_bold = bold(11.0f);
        set.heading      = bold(14.0f);
        set.pane_title   = bold(15.0f);
        set.mono         = mono(12.0f);
        set.mono_small   = mono(11.0f);
        set.mono_tiny    = mono(10.0f);
        set.mono_micro   = mono(9.0f);
    }
    // ImGui 1.92 reads and checks each file when it is added (no Build step): without the body font,
    // the built-in font alone rather than lose every label.
    if (!set.body) {
        io.Fonts->Clear();
        io.Fonts->AddFontDefault();
        Log(LogLevel::Info, "UI fonts: Segoe UI / Arial could not be loaded; the built-in font is used");
        return;
    }
    io.FontDefault = set.body;
    ui::SetFonts(set);
}

ImGuiContext* CreateWindowImGui(HWND hwnd, HDC dc)
{
    IMGUI_CHECKVERSION();
    ImGuiContext* const prev = ImGui::GetCurrentContext();
    ImGuiContext* created = ImGui::CreateContext();
    if (!created) return nullptr;
    ImGui::SetCurrentContext(created);  // CreateContext keeps a context that was current before
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;   // do not write imgui.ini next to REAPER
    io.LogFilename = nullptr;
    ui::ApplyRavTheme();   // the theme (ui_theme.h), over StyleColorsDark
    // The mock-up's fonts (Segoe UI, Cascadia Mono / Consolas) at the screen's DPI; the style's paddings
    // and sizes follow the same scale.
    const float dpi_scale = DpiScaleOf(dc);
    if (dpi_scale > 1.0f) ImGui::GetStyle().ScaleAllSizes(dpi_scale);
    ui::SetUiScale(dpi_scale);   // the theme's widgets size themselves in mockup px x this (ui::Dp)
    LoadUiFontAtlas(io, dpi_scale);
    // The two backends in nested steps so teardown stays symmetric: if the GL backend fails AFTER the
    // Win32 backend initialized, the Win32 backend is still shut down (DestroyContext alone leaks its
    // platform data).
    if (ImGui_ImplWin32_Init(hwnd)) {
        if (ImGui_ImplOpenGL3_Init("#version 130")) return created;
        ImGui_ImplWin32_Shutdown();
    }
    Log(LogLevel::Info, "tool UI (Dear ImGui) could not initialize — the window still works");
    ui::SetFonts(ui::FontSet{});  // no role font may point into the atlas that goes
    ImGui::DestroyContext(created);
    ImGui::SetCurrentContext(prev);
    return nullptr;
}

void DestroyWindowImGui(ImGuiContext* ctx)
{
    if (!ctx) return;
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(ctx);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ui::SetFonts(ui::FontSet{});  // no role font may point into the atlas that goes
    ImGui::DestroyContext(ctx);
    ImGui::SetCurrentContext(prev == ctx ? nullptr : prev);
}

}  // namespace host
}  // namespace rav
