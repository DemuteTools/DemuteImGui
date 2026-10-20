// SPDX-License-Identifier: MIT
//
// A panel in REAPER's docker, as RAV's viewer is (viewer_window.cpp): a WS_CHILD window of REAPER's main
// window (class CS_OWNDC), its own OpenGL context and Dear ImGui context, driven by a thread timer
// (15 ms: ~64 fps, above REAPER's ~30 Hz extension timer) on the main thread, handed to the docker with
// DockWindowAddEx (the docker services, host_services.h). Windows only. Main thread only.
//
// HIDE HANDLING (RAV, proven by a message trace): when the docker's tab X closes the panel, REAPER hides an
// ancestor and sends the window no message at all, and a close cannot be told from a transient hide
// (minimize, another tab of the docker). So the timer never destroys on a hide: it renders while the window
// is visible and PARKS while hidden (on_parked once, then on_hidden_tick each tick: nothing is drawn). The
// window is destroyed only by Close / Toggle (a path the tool owns) or the unload.
//
// The panel holds no logic of the tool: the tool plugs its own steps in, in RAV's order.
//   Open (WM_CREATE): GL context -> made current -> on_gl_ready (false = the open fails) -> vsync off ->
//   timer -> ImGui (CreateWindowImGui; a failure is not fatal) -> on_imgui_ready -> one tick.
//   Tick (visible): nested tick skipped (logged once) -> GL context re-asserted, ImGui context current ->
//   on_tick (the tool's frame).
//   Teardown (WM_DESTROY, or a failed WM_CREATE): timer killed -> GL made current -> on_before_shutdown
//   (ImGui still up: destroy the FloatUiWindows here) -> ImGui torn down -> on_gl_shutdown (the tool's GL
//   objects; GL still current when it exists) -> GL context dropped.
//   Both shutdown callbacks also run after a failed open (no GL context, on_gl_ready false, no ImGui yet),
//   as RAV's StopRendering does: they must tolerate a half-made state (Rc() / ImGuiCtx() may be null).
// A callback that throws inside WM_CREATE is caught there (WM_CREATE returns -1, nothing escapes USER32).

#pragma once

#include <windows.h>

#include <functional>

struct ImGuiContext;

namespace rav {
namespace host {

class DockedPanel;

struct DockedPanelConfig {
    const wchar_t* class_name = nullptr;  // the window class (registered by Open, unregistered by Close)
    const wchar_t* title = nullptr;       // the window's title
    const char*    dock_name = nullptr;   // the docker tab's caption
    const char*    dock_ident = nullptr;  // REAPER's key for the dock position: must never change between builds
    int            initial_w = 800;       // before the docker lays it out
    int            initial_h = 600;
    UINT           timer_ms = 15;
};

// A window procedure step: true when it handled the message (`*result` is then returned).
using PanelProc = std::function<bool(DockedPanel& panel, HWND hwnd, UINT msg, WPARAM wp, LPARAM lp, LRESULT* result)>;

struct DockedPanelCallbacks {
    std::function<bool(DockedPanel&)> on_gl_ready;        // GL current, before ImGui; false = the open fails
    std::function<void(DockedPanel&)> on_imgui_ready;     // ImGui context made (or not: ImGuiCtx() is null)
    std::function<void(DockedPanel&)> on_tick;            // a frame (visible); GL current, ImGui current when ImGuiCtx() is non-null
    std::function<void(DockedPanel&)> on_hidden_tick;     // a tick while hidden (nothing current is assured)
    std::function<void(DockedPanel&)> on_parked;          // the window just became hidden (once)
    std::function<void(DockedPanel&)> on_resumed;         // shown again (once, before the next on_tick)
    std::function<void(DockedPanel&)> on_before_shutdown; // GL current (if any), ImGui still up (if made); also after a failed open
    std::function<void(DockedPanel&)> on_gl_shutdown;     // ImGui gone, GL still current (if any); also after a failed open
    PanelProc proc_pre_imgui;  // before Dear ImGui sees the message (after the modifiers' refresh)
    PanelProc proc;            // after Dear ImGui (unless it took the message); WM_CREATE excluded
};

class DockedPanel {
public:
    DockedPanel(const DockedPanelConfig& config, DockedPanelCallbacks callbacks);
    ~DockedPanel();
    DockedPanel(const DockedPanel&) = delete;
    DockedPanel& operator=(const DockedPanel&) = delete;

    // Creates the window and docks it (DockWindowAddEx then DockWindowActivate); already there: its tab is
    // brought forward (out of the docker, see SetDocked: the window comes to the front). False when it could
    // not be made (logged). Never throws.
    bool Open(HINSTANCE hinst, HWND reaper_main);
    // Undocks (DockWindowRemove, unless out of the docker) and destroys it, then unregisters the class. The
    // next Open is docked. Never throws.
    void Close();
    // Visible: Close; else Open. Never throws.
    void Toggle(HINSTANCE hinst, HWND reaper_main);

    bool IsVisible() const;  // exists, and the whole ancestor chain (the docker) is visible
    bool Exists() const;
    HWND          Hwnd() const { return hwnd_; }  // null inside WM_CREATE (as RAV)
    HDC           Dc() const { return dc_; }
    HGLRC         Rc() const { return rc_; }
    ImGuiContext* ImGuiCtx() const { return imgui_; }
    bool          Paused() const { return paused_; }

    // The window is out of the docker (RAV's full screen: a top-level window of its own) or back in it.
    // Out of it, Open brings it to the front and Close does not call DockWindowRemove.
    void SetDocked(bool docked) { docked_ = docked; }
    bool Docked() const { return docked_; }

    // The answer to WM_GETDLGCODE for a child of REAPER's docker (a dialog: its IsDialogMessage would take a
    // character as a mnemonic and beep): characters always; Enter / Esc / Tab / the arrows only while the
    // panel takes keys (a text field, a key being recorded...), else the docker keeps them.
    static LRESULT DialogCode(bool takes_keys);

    // The modifiers as they are now into the current ImGui context (the backend reads them only on key
    // messages, and a modifier's own key may have gone to REAPER or another window).
    static void RefreshImGuiModifiers();
    // A mouse message (moves, buttons, wheels) before which the modifiers are refreshed.
    static bool IsMouseInputMessage(UINT msg);

    // The window procedure's way in (one window per object).
    LRESULT Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    // One timer tick (the timer calls it).
    void TimerTick();

private:
    bool OpenImpl(HINSTANCE hinst, HWND reaper_main);
    void CloseImpl();
    bool StartRendering(HWND hwnd);
    void StopRendering();
    void RenderTick();
    bool EnsureClassRegistered(HINSTANCE hinst);
    void UnregisterWindowClass();

    DockedPanelConfig    config_;
    DockedPanelCallbacks cb_;
    HWND                 hwnd_ = nullptr;
    HDC                  dc_ = nullptr;
    HGLRC                rc_ = nullptr;
    ImGuiContext*        imgui_ = nullptr;
    UINT_PTR             timer_id_ = 0;
    bool                 rendering_ = false;  // StartRendering ran: StopRendering has callbacks to call
    bool                 paused_ = false;
    bool                 in_tick_ = false;
    bool                 said_nested_ = false;
    bool                 docked_ = true;
    bool                 class_registered_ = false;
    HINSTANCE            class_hinst_ = nullptr;
};

}  // namespace host
}  // namespace rav
