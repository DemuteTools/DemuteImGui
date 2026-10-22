// SPDX-License-Identifier: MIT
//
// See docked_panel.h. The mechanisms are RAV's (viewer_window.cpp StartRendering, StopRendering,
// RenderTick's guards, FrameTimerProc, WindowProc, EnsureClassRegistered, UnregisterViewerClass,
// Open / Toggle / CloseViewerWindow), without the viewer itself.

#include "docked_panel.h"

#include <exception>
#include <utility>

#include <imgui.h>
#include <backends/imgui_impl_win32.h>

#include "gl_context.h"
#include "gl_switch.h"
#include "host_services.h"
#include "ui_fonts.h"

// imgui_impl_win32.h keeps this declaration inside an `#if 0` (so it does not need windows.h).
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace rav {
namespace host {

namespace {

// The live panels, for the thread timer (SetTimer with no window: its callback gets only the timer id).
constexpr int kMaxPanels = 16;
DockedPanel*  g_panels[kMaxPanels] = {};
UINT_PTR      g_panel_timers[kMaxPanels] = {};

void RegisterPanel(DockedPanel* p)
{
    for (int i = 0; i < kMaxPanels; ++i)
        if (!g_panels[i]) {
            g_panels[i] = p;
            return;
        }
}

void UnregisterPanel(DockedPanel* p)
{
    for (int i = 0; i < kMaxPanels; ++i)
        if (g_panels[i] == p) {
            g_panels[i] = nullptr;
            g_panel_timers[i] = 0;
        }
}

void SetPanelTimer(DockedPanel* p, UINT_PTR id)
{
    for (int i = 0; i < kMaxPanels; ++i)
        if (g_panels[i] == p) g_panel_timers[i] = id;
}

// ~66 Hz: SetTimer's ~15.6 ms floor yields ~64 fps, above REAPER's ~30 Hz extension timer. A thread timer
// dispatched by REAPER's main pump keeps rendering on the main thread. Rendering pauses during the host's
// modal loops (an open menu, a modal dialog) and resumes when they exit.
void CALLBACK PanelTimerProc(HWND, UINT, UINT_PTR id, DWORD)
{
    for (int i = 0; i < kMaxPanels; ++i)
        if (g_panels[i] && g_panel_timers[i] == id) {
            g_panels[i]->TimerTick();
            return;
        }
}

LRESULT CALLBACK PanelWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE) {
        const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    if (DockedPanel* p = reinterpret_cast<DockedPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
        const LRESULT r = p->Proc(hwnd, msg, wp, lp);
        if (msg == WM_NCDESTROY) SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        return r;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

const char* NameOf(const DockedPanelConfig& c)
{
    return c.dock_name ? c.dock_name : "panel";
}

}  // namespace

DockedPanel::DockedPanel(const DockedPanelConfig& config, DockedPanelCallbacks callbacks)
    : config_(config), cb_(std::move(callbacks))
{
    RegisterPanel(this);
}

DockedPanel::~DockedPanel()
{
    // A window that outlives the object must not reach it any more, nor its timer.
    if (hwnd_ && IsWindow(hwnd_)) SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
    if (timer_id_) {
        KillTimer(nullptr, timer_id_);
        timer_id_ = 0;
    }
    UnregisterPanel(this);
}

bool DockedPanel::IsVisible() const
{
    return hwnd_ && IsWindow(hwnd_) && IsWindowVisible(hwnd_);
}

bool DockedPanel::Exists() const
{
    return hwnd_ && IsWindow(hwnd_);
}

LRESULT DockedPanel::DialogCode(bool takes_keys)
{
    return DLGC_WANTCHARS | (takes_keys ? (DLGC_WANTALLKEYS | DLGC_WANTARROWS | DLGC_WANTTAB) : 0);
}

void DockedPanel::RefreshImGuiModifiers()
{
    if (!ImGui::GetCurrentContext()) return;
    ImGuiIO& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiMod_Ctrl, GetKeyState(VK_CONTROL) < 0);
    io.AddKeyEvent(ImGuiMod_Shift, GetKeyState(VK_SHIFT) < 0);
    io.AddKeyEvent(ImGuiMod_Alt, GetKeyState(VK_MENU) < 0);
}

bool DockedPanel::IsMouseInputMessage(UINT msg)
{
    return msg == WM_MOUSEWHEEL || msg == WM_MOUSEHWHEEL || msg == WM_MOUSEMOVE ||
           (msg >= WM_LBUTTONDOWN && msg <= WM_MBUTTONDBLCLK) || msg == WM_XBUTTONDOWN || msg == WM_XBUTTONUP ||
           msg == WM_XBUTTONDBLCLK;
}

bool DockedPanel::StartRendering(HWND hwnd)
{
    rendering_ = true;
    if (!CreateGlWindowContext(hwnd, &dc_, &rc_)) {
        Log(LogLevel::Error, "%s: failed to create an OpenGL context — your GPU/driver may not support OpenGL or no "
                             "display is available; the panel did not open (REAPER is unaffected)",
            NameOf(config_));
        return false;
    }
    if (!GlMakeCurrent(dc_, rc_)) {
        Log(LogLevel::Error, "%s: failed to activate the OpenGL context (wglMakeCurrent); the panel did not open "
                             "(REAPER is unaffected)",
            NameOf(config_));
        return false;
    }
    if (cb_.on_gl_ready && !cb_.on_gl_ready(*this)) return false;  // the tool logged why

    SetSwapInterval(0);  // the timer paces presentation
    paused_ = false;

    timer_id_ = SetTimer(nullptr, 0, config_.timer_ms, &PanelTimerProc);
    if (!timer_id_) {
        Log(LogLevel::Error, "%s: failed to start the render timer (SetTimer); the panel did not open (REAPER is "
                             "unaffected)",
            NameOf(config_));
        return false;
    }
    SetPanelTimer(this, timer_id_);

    // Dear ImGui (the GL context is current, as ImGui_ImplOpenGL3_Init needs). Not fatal: without it the
    // window still renders.
    imgui_ = CreateWindowImGui(hwnd, dc_);
    if (cb_.on_imgui_ready) cb_.on_imgui_ready(*this);

    // One frame now (the context is current), so the window presents real content the instant the docker
    // reveals it instead of a tick of undefined framebuffer.
    RenderTick();
    return true;
}

void DockedPanel::StopRendering()
{
    // Kill the timer, then release GL objects while the context is still current, then drop the context.
    // Idempotent (safe after a StartRendering that bailed mid-way).
    if (timer_id_) {
        KillTimer(nullptr, timer_id_);
        timer_id_ = 0;
        SetPanelTimer(this, 0);
    }
    if (rendering_) {
        rendering_ = false;
        if (rc_) GlMakeCurrent(dc_, rc_);
        {
            ImGuiContextScope scope(imgui_);
            if (cb_.on_before_shutdown) cb_.on_before_shutdown(*this);
        }
        if (imgui_ && rc_) {
            DestroyWindowImGui(imgui_);
            imgui_ = nullptr;
        }
        if (cb_.on_gl_shutdown) cb_.on_gl_shutdown(*this);
    }
    DestroyGlWindowContext(hwnd_, &dc_, &rc_);
    paused_ = false;
}

void DockedPanel::RenderTick()
{
    if (!dc_ || !rc_) return;
    // A gesture run from this frame may make REAPER pump messages (a dialog REAPER opens); the timer then
    // calls us again inside the frame, and a second ImGui frame inside the first breaks both. That nested
    // tick is skipped (said once in the log); the outer frame finishes as usual.
    if (in_tick_) {
        if (!said_nested_)
            Log(LogLevel::Info, "%s: a frame was asked for inside a frame (a REAPER dialog during a gesture); skipped",
                NameOf(config_));
        said_nested_ = true;
        return;
    }
    in_tick_ = true;
    struct LeaveTick {
        bool& flag;
        ~LeaveTick() { flag = false; }
    } leave_tick{in_tick_};

    // Re-assert OUR context every frame: REAPER's main thread is shared with other GL plugins that make
    // THEIR context current; every GL call would then fail silently. The previous one is given back.
    GlCurrentScope    gl(dc_, rc_);
    if (!gl.ok()) return;
    ImGuiContextScope ctx(imgui_);
    if (cb_.on_tick) cb_.on_tick(*this);
}

void DockedPanel::TimerTick()
{
    // REAPER sends a docked window no message when the panel is closed or hidden; a close cannot be told
    // from a transient hide. So nothing is destroyed here: it renders while visible and PARKS while hidden
    // (no frame, no SwapBuffers). IsWindowVisible is true only when the whole ancestor chain is visible.
    if (!hwnd_ || !IsWindowVisible(hwnd_)) {
        if (!paused_) {
            paused_ = true;
            if (cb_.on_parked) cb_.on_parked(*this);
            Log(LogLevel::Info, "panel hidden — render paused");
        }
        if (cb_.on_hidden_tick) cb_.on_hidden_tick(*this);
        return;
    }
    if (paused_) {
        paused_ = false;
        if (cb_.on_resumed) cb_.on_resumed(*this);
        Log(LogLevel::Info, "panel shown — render resumed");
    }
    RenderTick();
}

LRESULT DockedPanel::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    // Our ImGui context, only when another one is current (then put back): WM_CREATE makes it, WM_DESTROY frees it.
    ImGuiContextScope scope(imgui_);
    // The backend reads the modifiers only on key messages, and a modifier's own key may have gone to
    // REAPER or another window: refreshed before every mouse message (Alt + wheel; a modifier released
    // elsewhere is not left down).
    if (imgui_ && IsMouseInputMessage(msg)) RefreshImGuiModifiers();
    LRESULT result = 0;
    if (msg != WM_CREATE && cb_.proc_pre_imgui && cb_.proc_pre_imgui(*this, hwnd, msg, wp, lp, &result)) return result;
    // Dear ImGui first, once it is up (it comes up inside WM_CREATE, synchronously).
    if (imgui_ && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return true;

    switch (msg) {
    case WM_CREATE:
        // The no-throw boundary holds here too: StartRendering runs inside CreateWindowExW (a USER32
        // callback); an exception unwinding across that frame is a host crash. Caught, turned into -1.
        try {
            if (!StartRendering(hwnd)) {
                // Windows does NOT send WM_DESTROY when WM_CREATE returns -1 (only WM_NCDESTROY): torn down
                // here, explicitly (StopRendering is idempotent).
                StopRendering();
                return -1;
            }
        } catch (const std::exception& e) {
            Log(LogLevel::Error, "%s: init failed: %s (REAPER is unaffected)", NameOf(config_), e.what());
            StopRendering();
            return -1;
        } catch (...) {
            Log(LogLevel::Error, "%s: init failed: unknown error (REAPER is unaffected)", NameOf(config_));
            StopRendering();
            return -1;
        }
        return 0;

    case WM_DESTROY:
        // Reached only on the paths the tool initiates (Close, the unload), not the docker's tab X. The tool
        // sees it first, then the render loop, ImGui and GL go while the context is still current.
        if (cb_.proc) cb_.proc(*this, hwnd, msg, wp, lp, &result);
        StopRendering();
        hwnd_ = nullptr;
        return 0;

    default:
        break;
    }
    if (cb_.proc && cb_.proc(*this, hwnd, msg, wp, lp, &result)) return result;

    switch (msg) {
    case WM_PAINT: {
        // The timer renders; paint just validates the update region.
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;  // GL owns the surface; no GDI background fill (no flicker)
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

bool DockedPanel::EnsureClassRegistered(HINSTANCE hinst)
{
    if (class_registered_) return true;

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_OWNDC;  // each window owns its DC; required for stable WGL
    wc.lpfnWndProc   = PanelWndProc;
    wc.hInstance     = hinst;
    wc.hCursor       = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
    wc.lpszClassName = config_.class_name;

    if (!RegisterClassExW(&wc)) {
        const DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) return false;
    }
    class_registered_ = true;
    class_hinst_      = hinst;
    return true;
}

// The module the class was registered under, so a later load of the DLL does not inherit a window
// procedure pointing into the unloaded image. Idempotent.
void DockedPanel::UnregisterWindowClass()
{
    if (class_registered_) {
        UnregisterClassW(config_.class_name, class_hinst_);
        class_registered_ = false;
        class_hinst_      = nullptr;
    }
}

bool DockedPanel::OpenImpl(HINSTANCE hinst, HWND reaper_main)
{
    if (hwnd_ && IsWindow(hwnd_)) {
        // Out of the docker (full screen) it just comes to the front.
        if (!docked_) {
            SetForegroundWindow(hwnd_);
            return true;
        }
        // Already created (possibly hidden by the docker's X): its tab forward; the paused loop resumes alone.
        DockActivate(hwnd_);
        return true;
    }
    if (!config_.class_name) return false;
    if (!EnsureClassRegistered(hinst)) {
        Log(LogLevel::Error, "%s: failed to register window class", NameOf(config_));
        return false;
    }
    // A WS_CHILD of REAPER's main window: the docker reparents it into its own host and drives its size and
    // visibility. Created without WS_VISIBLE: DockWindowActivate reveals it.
    hwnd_ = CreateWindowExW(0, config_.class_name, config_.title ? config_.title : L"", WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                            0, 0, config_.initial_w, config_.initial_h, reaper_main, nullptr, hinst, this);
    if (!hwnd_) {
        // WM_CREATE returned -1 (already logged with its cause) or the create failed outright: the class goes
        // too, so a failed open leaves nothing behind.
        Log(LogLevel::Error, "%s: the window could not be created (CreateWindowExW); the panel did not open "
                             "(REAPER is unaffected)",
            NameOf(config_));
        UnregisterWindowClass();
        return false;
    }
    // The docker (allow_show), its tab forward. The ident keeps the dock position across sessions.
    DockAddEx(hwnd_, config_.dock_name, config_.dock_ident, true);
    DockActivate(hwnd_);
    return true;
}

void DockedPanel::CloseImpl()
{
    if (hwnd_ && IsWindow(hwnd_)) {
        // A path the tool owns: out of the docker first (none when out of it already), then destroyed
        // (WM_DESTROY runs StopRendering).
        if (docked_) DockRemove(hwnd_);
        DestroyWindow(hwnd_);
    } else {
        StopRendering();  // the window is already gone: the timer and the context go too
    }
    hwnd_ = nullptr;
    docked_ = true;  // the next open is docked
    UnregisterWindowClass();
}

bool DockedPanel::Open(HINSTANCE hinst, HWND reaper_main)
{
    try {
        return OpenImpl(hinst, reaper_main);
    } catch (const std::exception& e) {
        Log(LogLevel::Error, "%s: open failed: %s (REAPER is unaffected)", NameOf(config_), e.what());
    } catch (...) {
        Log(LogLevel::Error, "%s: open failed: unknown error (REAPER is unaffected)", NameOf(config_));
    }
    Close();
    return false;
}

void DockedPanel::Close()
{
    try {
        CloseImpl();
    } catch (const std::exception& e) {
        Log(LogLevel::Error, "%s: close failed: %s (REAPER is unaffected)", NameOf(config_), e.what());
    } catch (...) {
        Log(LogLevel::Error, "%s: close failed: unknown error (REAPER is unaffected)", NameOf(config_));
    }
}

void DockedPanel::Toggle(HINSTANCE hinst, HWND reaper_main)
{
    // A visible panel toggles off (a full close); a missing or docker-hidden one toggles on.
    try {
        if (IsVisible()) CloseImpl();
        else OpenImpl(hinst, reaper_main);
    } catch (const std::exception& e) {
        Log(LogLevel::Error, "%s: open/close failed: %s (REAPER is unaffected)", NameOf(config_), e.what());
        Close();
    } catch (...) {
        Log(LogLevel::Error, "%s: open/close failed: unknown error (REAPER is unaffected)", NameOf(config_));
        Close();
    }
}

}  // namespace host
}  // namespace rav
