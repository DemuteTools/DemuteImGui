// SPDX-License-Identifier: MIT
//
// The UI fonts from the Windows Fonts folder, and the Dear ImGui context of a GL window, as RAV brings
// them up (viewer_window.cpp LoadUiFontAtlas, StartRendering, StopRendering). Windows only.
//
// Fonts: --sans Segoe UI (body 12, captions 11, titles 13, big 15, tags 10, badge 9), Segoe UI Semibold
// (12 / 11 / 14 / 15), --mono Cascadia Mono / Consolas (12 / 11 / 10 / 9), Arial (Bold) when Segoe is
// missing, the Windows symbol fonts merged in for the blocks of ui_glyph_ranges.h; ImGui's own font as
// the last resort. One ImFont per role, added at its own size (the theme's contract, story 1.1).

#pragma once

#include <windows.h>

struct ImGuiContext;
struct ImGuiIO;

namespace rav {
namespace host {

// The screen's DPI over 96 (LOGPIXELSY of `dc`; 96 without one), clamped to 1..4.
float DpiScaleOf(HDC dc);

// Adds every role's font to `io.Fonts` at `dpi_scale` (1 at 100 %; outside 1..4 = 1) and sets ui::SetFonts
// and io.FontDefault. A missing Windows folder or no Segoe UI / Arial: ImGui's built-in font alone (logged).
void LoadUiFontAtlas(ImGuiIO& io, float dpi_scale);

// A new ImGui context for the GL window `hwnd` (its DC `dc`), left current: no ini or log file, the theme,
// the style and ui::SetUiScale at the DPI of `dc`, the UI fonts, the Win32 and OpenGL3 backends. The GL
// context must be current. Null when a backend fails (logged; nothing is left, the theme's
// fonts cleared: the window works without UI).
ImGuiContext* CreateWindowImGui(HWND hwnd, HDC dc);

// Its symmetric teardown: the OpenGL3 then Win32 backends, then the context (the GL context must be
// current). Every FloatUiWindow sharing its font atlas must be destroyed before. The previous current
// context is put back unless it was this one (then none is current).
void DestroyWindowImGui(ImGuiContext* ctx);

}  // namespace host
}  // namespace rav
