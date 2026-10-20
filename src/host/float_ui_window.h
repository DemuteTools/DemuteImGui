// SPDX-License-Identifier: MIT
//
// A tool window of its own (13-15, Antho 2026-10-12: the Sound assignment window goes anywhere on the
// screen, not only inside RAV's panel). It is a top-level Win32 window, owned by RAV's own top window,
// with no system frame: the content's title row moves it, its edges resize it. It has its own Dear ImGui
// context, which shares the viewer's font atlas, style and OpenGL context (its DC gets the viewer's
// pixel format), so every ui:: widget draws in it unchanged. Main thread only.
//
// The viewer drives it once per render tick, after its own frame (FloatUiWindow::Frame); a tick that
// does not call Frame with show = true hides it. Its rect is kept in REAPER's ExtState, in screen pixels.
//
// demute-imgui (story 1.2): its rect goes through the host services (host_services.h: SetHostServices once at
// the plugin entry; without them it opens at its default place and saves nothing), its geometry through
// float_geometry.h. ImGui 1.92: the shared atlas's texture is the viewer backend's, kept alive by the atlas's
// RefCount (no more SetTexID). Every FloatUiWindow is known to RouteKey, the accelerator hook's decision.

#pragma once

#ifdef _WIN32

#include <windows.h>

#include <functional>

#include <imgui.h>

namespace rav {

class FloatUiWindow {
public:
    // `pref` names its ExtState keys ("<pref>.x" ...), `title` the OS window's title (Alt+Tab, taskbar
    // never: it is a tool window). Sizes in screen pixels.
    FloatUiWindow(const char* pref, const wchar_t* title, int default_w, int default_h, int min_w, int min_h);
    // Leaves the windows as they are (Destroy frees them); only forgets the object for RouteKey.
    ~FloatUiWindow();
    FloatUiWindow(const FloatUiWindow&) = delete;
    FloatUiWindow& operator=(const FloatUiWindow&) = delete;

    // The viewer's GL objects and its ImGui context (the style and font atlas to share). Call before Frame.
    void Bind(HWND viewer, HDC viewer_dc, HGLRC rc);

    // One frame: with `show`, the window is created when needed, shown, and `draw` fills it (an ImGui
    // window covering its whole client area is open around the call; `draw` must not Begin its own
    // top window). Without, it is hidden. `activate` brings it to the front with the keyboard focus.
    // Restores the viewer's ImGui context and GL context on return.
    void Frame(bool show, bool activate, const std::function<void()>& draw);

    // The content says which part of the client area moves the window (its title row, client pixels;
    // empty = none) and whether the mouse is over a widget there (then the click goes to the widget).
    void SetDragArea(float x0, float y0, float x1, float y1, bool over_widget);

    // True once after the user closed it with the system (Alt+F4): the content then closes too.
    bool TakeCloseRequest();
    // True while the window holds the keyboard focus (its keys are its own).
    bool HasFocus() const;
    // Its ImGui context wants the typed text (a text field is active).
    bool WantsText() const;
    HWND Hwnd() const { return hwnd_; }
    bool IsShown() const;
    // `hwnd` is this window or one of its children (a key message aimed at it).
    bool Owns(HWND hwnd) const;

    // REAPER's accelerator hook, for a message aimed at any FloatUiWindow (`*is_float` true; else false and
    // 0): typing in a field, and their own keys (Esc, the arrows, Enter, Tab, Delete / Backspace without
    // Ctrl / Alt) go to the window (-1); Alt+F4 too (-20); every other key stays REAPER's (0). A message that
    // is not a key: 0. The decision is FloatWindowKeyRoute (float_key_route.h).
    static int RouteKey(const MSG* msg, bool* is_float);

    // 13-68e: the next Frame puts the window at the mouse, wholly on that monitor's work area, its size kept. Antho
    // 2026-10-19: the pointer at the middle of its title row: x = mouse - width / 2, y = mouse - `header_mid` (the
    // title row's middle from the window's top, screen pixels). A window placed this way keeps only its size in
    // ExtState (it opens at the mouse each time).
    void PlaceAtMouse(int header_mid);

    // Hides it now (the viewer is parked: no frame will run).
    void Hide();
    // Frees everything (the viewer's GL context must be current, its ImGui context too).
    void Destroy();
    // The window class goes (the DLL unloads, or the viewer closes): call after every window is destroyed.
    static void UnregisterWindowClass();
    // 6-5-9: while the viewer is full screen (a top-level window of its own), every tool window is owned
    // by it, so a click on the viewer never hides them behind it; null = back to the full screen
    // window's owner (REAPER's main window). Call before the viewer stops being top-level.
    static void SetFullScreenOwner(HWND full_screen);

    // The window procedure's way in (one window per object).
    LRESULT Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

private:
    bool Create();
    void SaveRect();

    const char*    pref_;
    const wchar_t* title_;
    int            def_w_, def_h_, min_w_, min_h_;
    HWND           viewer_ = nullptr;
    HDC            viewer_dc_ = nullptr;
    HGLRC          rc_ = nullptr;
    HWND           hwnd_ = nullptr;
    HDC            dc_ = nullptr;
    ImGuiContext*  ctx_ = nullptr;
    ImFontAtlas*   atlas_ = nullptr;
    float          drag_[4] = {0, 0, 0, 0};
    bool           drag_over_widget_ = false;
    bool           rect_dirty_ = false;
    bool           failed_ = false;  // creation failed once: not retried every tick
    bool           close_requested_ = false;
    bool           place_mouse_ = false;  // 13-68e: PlaceAtMouse pending
    int            header_mid_ = 0;       // its title row's middle (screen px from the top)
    bool           at_mouse_ = false;     // 13-68e: placed at the mouse: its position is not saved
};

}  // namespace rav

#endif  // _WIN32
