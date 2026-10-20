// SPDX-License-Identifier: MIT
//
// Windows only: every public name of the `host` target, compiled and linked, so a signature that drifts
// fails the build. Run, it opens no window, makes no GL context and calls REAPER nothing: it checks what
// can be checked without a window (no services: defaults; RouteKey of a foreign message; the dialog
// code...) and returns 0.

#include <windows.h>

#include <cstdio>

#include <imgui.h>

#include "docked_panel.h"
#include "float_geometry.h"
#include "float_key_route.h"
#include "float_ui_window.h"
#include "gl_context.h"
#include "host_prefs.h"
#include "host_services.h"
#include "ui_fonts.h"

using namespace rav;

namespace {

int g_fail = 0;
#define CHECK(c)                                                     \
    do {                                                             \
        if (!(c)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            ++g_fail;                                                \
        }                                                            \
    } while (0)

// The services as a tool assigns them from REAPER's own pointers (same signatures).
const char* (*GetExtState)(const char* section, const char* key) = nullptr;
void (*SetExtState)(const char* section, const char* key, const char* value, bool persist) = nullptr;
void (*DockWindowAddEx)(HWND hwnd, const char* name, const char* identstr, bool allowShow) = nullptr;
void (*DockWindowActivate)(HWND hwnd) = nullptr;
void (*DockWindowRemove)(HWND hwnd) = nullptr;

// Static globals, as RAV builds its floating windows (before any service is set).
FloatUiWindow g_float("check.float", L"Check", 400, 300, 200, 150);

template <typename T>
void Use(T t)
{
    static const void* volatile sink;
    sink = reinterpret_cast<const void*>(&t);
}

}  // namespace

int main()
{
    // Services: absent, then REAPER-shaped pointers (still null here).
    CHECK(host::LoadPrefFloat("check.float.x", 3.0f) == 3.0f);
    host::HostServices s;
    s.get_ext_state = GetExtState;
    s.set_ext_state = SetExtState;
    s.ext_section = "Check";
    s.dock_add_ex = DockWindowAddEx;
    s.dock_activate = DockWindowActivate;
    s.dock_remove = DockWindowRemove;
    host::SetHostServices(s);
    CHECK(host::LoadPrefFloat("check.float.x", 3.0f) == 3.0f);
    host::SavePrefFloat("check.float.x", 1.0f);
    host::DockAddEx(nullptr, "Check", "Check.Panel", true);
    host::DockActivate(nullptr);
    host::DockRemove(nullptr);
    host::Log(host::LogLevel::Info, "%d", 1);
    CHECK(host::GetHostServices().ext_section != nullptr);
    host::SetHostServices(host::HostServices{});

    // Pure parts, through the Windows build.
    CHECK(host::ParsePrefFloat("12.0", 0.0f) == 12.0f);
    CHECK(host::FormatPrefFloat(12.0f) == "12.0");
    CHECK(host::FloatBorderPx(1.0f) == 6);
    CHECK(host::FloatWindowKeyRoute(WM_KEYDOWN, VK_ESCAPE, false, false, false) == host::kFloatRouteWindow);
    CHECK(host::FloatWindowKeyRoute(WM_SYSKEYDOWN, VK_F4, false, true, false) == host::kFloatRouteWindowSys);

    // GL helpers without a context: nothing made current, nothing changed.
    {
        host::GlCurrentScope gl(nullptr, nullptr);
        CHECK(!gl.ok());
        host::ImGuiContextScope ctx(nullptr);
    }
    {  // A failed GL init (no window): false, nothing made, nothing to release.
        HDC   dc = reinterpret_cast<HDC>(1);
        HGLRC rc = reinterpret_cast<HGLRC>(1);
        CHECK(!host::CreateGlWindowContext(nullptr, &dc, &rc));
        CHECK(dc == nullptr && rc == nullptr);
        host::DestroyGlWindowContext(nullptr, &dc, &rc);  // idempotent on an empty context
        CHECK(dc == nullptr && rc == nullptr);
    }
    Use(&host::CreateGlWindowContext);
    Use(&host::DestroyGlWindowContext);
    Use(&host::SetSwapInterval);
    Use(&host::BindDefaultFramebuffer);

    // Fonts and the window's ImGui.
    CHECK(host::DpiScaleOf(nullptr) == 1.0f);
    Use(&host::LoadUiFontAtlas);
    Use(&host::CreateWindowImGui);
    Use(&host::DestroyWindowImGui);

    // The floating window: built as a static global, never created here.
    CHECK(g_float.Hwnd() == nullptr && !g_float.IsShown() && !g_float.HasFocus() && !g_float.WantsText());
    CHECK(!g_float.Owns(nullptr) && !g_float.TakeCloseRequest());
    MSG  msg = {};
    msg.message = WM_KEYDOWN;
    msg.wParam = VK_ESCAPE;
    bool is_float = true;
    CHECK(FloatUiWindow::RouteKey(&msg, &is_float) == 0 && !is_float);  // aimed at no floating window
    CHECK(FloatUiWindow::RouteKey(nullptr, nullptr) == 0);
    g_float.SetDragArea(0, 0, 10, 10, false);
    g_float.PlaceAtMouse(12);
    g_float.Hide();
    g_float.Frame(false, false, [] {});  // not shown: only hidden
    Use(&FloatUiWindow::Bind);
    Use(&FloatUiWindow::Frame);
    Use(&FloatUiWindow::Destroy);
    Use(&FloatUiWindow::Proc);
    Use(&FloatUiWindow::UnregisterWindowClass);
    Use(&FloatUiWindow::SetFullScreenOwner);

    // The docked panel: configured, never opened.
    host::DockedPanelConfig cfg;
    cfg.class_name = L"demute_imgui.HostCompileCheck";
    cfg.title = L"Check";
    cfg.dock_name = "Check";
    cfg.dock_ident = "Check.Panel";
    host::DockedPanelCallbacks cb;
    cb.on_gl_ready = [](host::DockedPanel&) { return true; };
    cb.on_imgui_ready = [](host::DockedPanel&) {};
    cb.on_tick = [](host::DockedPanel&) {};
    cb.on_hidden_tick = [](host::DockedPanel&) {};
    cb.on_parked = [](host::DockedPanel&) {};
    cb.on_resumed = [](host::DockedPanel&) {};
    cb.on_before_shutdown = [](host::DockedPanel&) {};
    cb.on_gl_shutdown = [](host::DockedPanel&) {};
    cb.proc_pre_imgui = [](host::DockedPanel&, HWND, UINT, WPARAM, LPARAM, LRESULT*) { return false; };
    cb.proc = [](host::DockedPanel&, HWND, UINT, WPARAM, LPARAM, LRESULT*) { return false; };
    host::DockedPanel panel(cfg, cb);
    CHECK(!panel.Exists() && !panel.IsVisible() && panel.Hwnd() == nullptr && panel.Dc() == nullptr);
    CHECK(panel.Rc() == nullptr && panel.ImGuiCtx() == nullptr && panel.Docked() && !panel.Paused());
    panel.SetDocked(false);
    CHECK(!panel.Docked());
    panel.SetDocked(true);
    CHECK(host::DockedPanel::DialogCode(false) == DLGC_WANTCHARS);
    CHECK(host::DockedPanel::DialogCode(true) == (DLGC_WANTCHARS | DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTTAB));
    CHECK(host::DockedPanel::IsMouseInputMessage(WM_MOUSEWHEEL) && !host::DockedPanel::IsMouseInputMessage(WM_KEYDOWN));
    host::DockedPanel::RefreshImGuiModifiers();  // no ImGui context: nothing
    panel.Close();      // never opened: nothing to undock, no window
    panel.TimerTick();  // no window: parked, nothing drawn
    CHECK(panel.Paused());
    Use(&host::DockedPanel::Open);
    Use(&host::DockedPanel::Toggle);
    Use(&host::DockedPanel::Proc);

    if (g_fail) {
        std::printf("host_compile_check: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("host_compile_check: ok\n");
    return 0;
}
