// SPDX-License-Identifier: MIT
//
// See float_ui_window.h. Taken from RAV (float_ui_window.cpp, 47558f9): the prefs go through the host
// services, the geometry through float_geometry.h, the key routing of RAV's accelerator hook is RouteKey.

#include "float_ui_window.h"

#ifdef _WIN32

#include <windowsx.h>  // GET_X_LPARAM / GET_Y_LPARAM

#include <algorithm>
#include <string>

#include <GL/gl.h>  // glViewport / glClearColor / glClear (GL 1.1, opengl32)

#include <imgui_internal.h>  // ImGuiContext::IO (WantsText without making the context current)
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_win32.h>

#include "float_geometry.h"
#include "float_key_route.h"
#include "gl_context.h"     // BindDefaultFramebuffer
#include "gl_switch.h"      // GlMakeCurrent (RAV freeze 2026-10-21)
#include "host_prefs.h"     // PrefKey
#include "host_services.h"  // LoadPrefFloat / SavePrefFloat
#include "ui_theme.h"

// The pure key routing names the codes itself: they are windows.h's.
static_assert(rav::host::kMsgKeyDown == WM_KEYDOWN && rav::host::kMsgKeyUp == WM_KEYUP && rav::host::kMsgChar == WM_CHAR &&
                  rav::host::kMsgSysKeyDown == WM_SYSKEYDOWN && rav::host::kMsgSysKeyUp == WM_SYSKEYUP &&
                  rav::host::kMsgSysChar == WM_SYSCHAR,
              "float_key_route.h: message codes");
static_assert(rav::host::kVkBack == VK_BACK && rav::host::kVkTab == VK_TAB && rav::host::kVkReturn == VK_RETURN &&
                  rav::host::kVkEscape == VK_ESCAPE && rav::host::kVkLeft == VK_LEFT && rav::host::kVkUp == VK_UP &&
                  rav::host::kVkRight == VK_RIGHT && rav::host::kVkDown == VK_DOWN && rav::host::kVkDelete == VK_DELETE &&
                  rav::host::kVkF4 == VK_F4,
              "float_key_route.h: virtual-key codes");

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace rav {

namespace {

constexpr wchar_t kClass[] = L"RAV_FloatUiWindow";
HINSTANCE         g_class_inst = nullptr;  // the class is registered (by this DLL) while set
HWND              g_full_screen_owner = nullptr;  // 6-5-9: the viewer while it is full screen

// Every FloatUiWindow object (RouteKey). Constant-initialized: safe for objects built as static globals.
constexpr int  kMaxFloats = 32;
FloatUiWindow* g_floats[kMaxFloats] = {};

// 6-5-9: gives every tool window of this thread (our class) the owner `lp`. demute-imgui: of this module only
// (another tool built on the library has a class of the same name, on the same thread).
BOOL CALLBACK ReownProc(HWND hwnd, LPARAM lp)
{
    wchar_t cls[64] = {};
    if (GetClassNameW(hwnd, cls, 64) > 0 && lstrcmpW(cls, kClass) == 0 &&
        reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd, GWLP_HINSTANCE)) == g_class_inst)
        SetWindowLongPtrW(hwnd, GWLP_HWNDPARENT, lp);
    return TRUE;
}

LRESULT CALLBACK FloatProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_NCCREATE) {
        const CREATESTRUCTW* cs = reinterpret_cast<const CREATESTRUCTW*>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
    }
    if (FloatUiWindow* w = reinterpret_cast<FloatUiWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)))
        return w->Proc(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

std::string Key(const char* pref, const char* what)
{
    return host::PrefKey(pref, what);
}

float LoadPrefFloat(const char* key, float def)
{
    return host::LoadPrefFloat(key, def);
}

void SavePrefFloat(const char* key, float value)
{
    host::SavePrefFloat(key, value);
}

host::IRect ToIRect(const RECT& r)
{
    host::IRect i;
    i.left = r.left;
    i.top = r.top;
    i.right = r.right;
    i.bottom = r.bottom;
    return i;
}

// Makes a context current for a scope, then puts the previous one back.
struct ContextScope {
    ImGuiContext* prev;
    explicit ContextScope(ImGuiContext* c) : prev(ImGui::GetCurrentContext()) { ImGui::SetCurrentContext(c); }
    ~ContextScope() { ImGui::SetCurrentContext(prev); }
};

}  // namespace

FloatUiWindow::FloatUiWindow(const char* pref, const wchar_t* title, int default_w, int default_h, int min_w, int min_h)
    : pref_(pref), title_(title), def_w_(default_w), def_h_(default_h), min_w_(min_w), min_h_(min_h)
{
    for (FloatUiWindow*& slot : g_floats)
        if (!slot) {
            slot = this;
            break;
        }
}

FloatUiWindow::~FloatUiWindow()
{
    if (hwnd_ && IsWindow(hwnd_)) SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);  // the window no longer reaches it
    for (FloatUiWindow*& slot : g_floats)
        if (slot == this) slot = nullptr;
}

void FloatUiWindow::Bind(HWND viewer, HDC viewer_dc, HGLRC rc)
{
    viewer_ = viewer;
    viewer_dc_ = viewer_dc;
    rc_ = rc;
}

bool FloatUiWindow::Create()
{
    if (!viewer_ || !viewer_dc_ || !rc_ || !ImGui::GetCurrentContext()) return false;
    HINSTANCE hi = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(viewer_, GWLP_HINSTANCE));
    if (!g_class_inst) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.style = CS_OWNDC | CS_DBLCLKS;
        wc.lpfnWndProc = FloatProc;
        wc.hInstance = hi;
        wc.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
        wc.lpszClassName = kClass;
        // A class left by an earlier load of this DLL would point into the unloaded image: replaced.
        if (!RegisterClassExW(&wc)) {
            if (GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
            UnregisterClassW(kClass, hi);
            if (!RegisterClassExW(&wc)) return false;
        }
        g_class_inst = hi;
    }
    // The rect it had (screen pixels), else beside the viewer's right edge; back on a screen when its
    // monitor is gone.
    const float s = ui::UiScale();
    RECT        vr = {};
    GetWindowRect(viewer_, &vr);
    const host::FloatRect size =
        host::FloatInitialSize(LoadPrefFloat(Key(pref_, "w").c_str(), host::FloatDefaultSizePref(def_w_, s)),
                               LoadPrefFloat(Key(pref_, "h").c_str(), host::FloatDefaultSizePref(def_h_, s)), min_w_, min_h_, s);
    host::FloatRect fr = host::FloatInitialPosition(LoadPrefFloat(Key(pref_, "x").c_str(), host::kFloatNoPosition),
                                                    LoadPrefFloat(Key(pref_, "y").c_str(), host::kFloatNoPosition), size.w,
                                                    ToIRect(vr), s);
    fr.w = size.w;
    fr.h = size.h;
    // Wholly on the nearest monitor's work area (it has no system caption: its title row must stay reachable).
    RECT        r = {fr.x, fr.y, fr.x + fr.w, fr.y + fr.h};
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(MonitorFromRect(&r, MONITOR_DEFAULTTONEAREST), &mi)) fr = host::FloatFitToWorkArea(fr, ToIRect(mi.rcWork));
    const int x = fr.x, y = fr.y, w = fr.w, h = fr.h;
    // Owned by REAPER's main window (the root owner): a floating docker's frame comes and goes with redocks.
    // 6-5-9: by the viewer itself while it is full screen (then in front of it).
    HWND owner = g_full_screen_owner ? g_full_screen_owner : GetAncestor(viewer_, GA_ROOTOWNER);
    hwnd_ = CreateWindowExW(WS_EX_TOOLWINDOW, kClass, title_, WS_POPUP | WS_THICKFRAME | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, x,
                            y, w, h, owner, nullptr, hi, this);
    if (!hwnd_) return false;
    dc_ = GetDC(hwnd_);
    // The viewer's pixel format: the shared GL context renders into either DC.
    const int             pf = GetPixelFormat(viewer_dc_);
    PIXELFORMATDESCRIPTOR pfd = {};
    if (!dc_ || !pf || !DescribePixelFormat(viewer_dc_, pf, sizeof(pfd), &pfd) || !SetPixelFormat(dc_, pf, &pfd)) {
        Destroy();
        return false;
    }
    // Its own ImGui context, on the viewer's font atlas and style.
    ImGuiIO&         mio = ImGui::GetIO();
    const ImGuiStyle style = ImGui::GetStyle();
    ImFont*          def_font = mio.FontDefault;
    atlas_ = mio.Fonts;
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ctx_ = ImGui::CreateContext(atlas_);
    ImGui::SetCurrentContext(ctx_);
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.FontDefault = def_font;
    ImGui::GetStyle() = style;
    bool ok = ImGui_ImplWin32_Init(hwnd_);
    if (ok) {
        ok = GlMakeCurrent(dc_, rc_) && ImGui_ImplOpenGL3_Init("#version 130");
        GlMakeCurrent(viewer_dc_, rc_);
        if (!ok) ImGui_ImplWin32_Shutdown();
    }
    if (!ok) {
        ImGui::DestroyContext(ctx_);
        ctx_ = nullptr;
        ImGui::SetCurrentContext(prev);
        Destroy();
        return false;
    }
    ImGui::SetCurrentContext(prev);
    return true;
}

void FloatUiWindow::Frame(bool show, bool activate, const std::function<void()>& draw)
{
    if (!show) {
        Hide();
        return;
    }
    if (hwnd_ && !IsWindow(hwnd_)) Destroy();  // destroyed under us (review): its context goes, a new one is made
    if (!hwnd_) {
        if (failed_) return;
        if (!Create()) {
            failed_ = true;
            return;
        }
    }

    if (place_mouse_) {  // 13-68e: at the mouse, wholly on its monitor's work area
        place_mouse_ = false;
        at_mouse_ = true;
        POINT pt = {};
        RECT  wr = {};
        if (GetCursorPos(&pt) && GetWindowRect(hwnd_, &wr)) {
            const int   w = wr.right - wr.left, h = wr.bottom - wr.top;
            MONITORINFO mi = {};
            mi.cbSize = sizeof(mi);
            if (GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi)) {
                const host::IRect               wa = ToIRect(mi.rcWork);
                const host::FloatMousePlacement p = host::FloatPlaceAtMouse(pt.x, pt.y, w, h, header_mid_, &wa);
                SetWindowPos(hwnd_, nullptr, p.rect.x, p.rect.y, p.rect.w, p.rect.h,
                             SWP_NOZORDER | SWP_NOACTIVATE | (p.resize ? 0 : SWP_NOSIZE));
            } else {
                const host::FloatMousePlacement p = host::FloatPlaceAtMouse(pt.x, pt.y, w, h, header_mid_, nullptr);
                SetWindowPos(hwnd_, nullptr, p.rect.x, p.rect.y, 0, 0, SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOSIZE);
            }
        }
    }

    ContextScope scope(ctx_);
    if (!GlMakeCurrent(dc_, rc_)) {
        GlMakeCurrent(viewer_dc_, rc_);
        return;
    }
    if (!IsWindowVisible(hwnd_)) {
        ShowWindow(hwnd_, activate ? SW_SHOW : SW_SHOWNOACTIVATE);
    } else if (activate) {
        SetForegroundWindow(hwnd_);
    }
    if (activate) SetFocus(hwnd_);
    // ImGui 1.92: the shared atlas's textures are made once, by whichever backend sees them first, in the
    // shared GL context; this backend reads the same ones (no SetTexID).
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplWin32_NewFrame();
    {  // The modifiers as they are now: a Ctrl released while the key went to REAPER is never held here.
        ImGuiIO& io = ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl, GetKeyState(VK_CONTROL) < 0);
        io.AddKeyEvent(ImGuiMod_Shift, GetKeyState(VK_SHIFT) < 0);
        io.AddKeyEvent(ImGuiMod_Alt, GetKeyState(VK_MENU) < 0);
    }
    ImGui::NewFrame();
    if (ui::BeginToolWindowFill("##floatui")) draw();
    ui::EndToolWindowFill();
    ImGui::Render();
    const ImGuiIO& io = ImGui::GetIO();
    host::BindDefaultFramebuffer();
    glViewport(0, 0, static_cast<int>(io.DisplaySize.x * io.DisplayFramebufferScale.x),
               static_cast<int>(io.DisplaySize.y * io.DisplayFramebufferScale.y));
    const ImVec4 bg = ui::Col(ui::kRaised);
    glClearColor(bg.x, bg.y, bg.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SwapBuffers(dc_);
    GlMakeCurrent(viewer_dc_, rc_);
    if (rect_dirty_ && (GetAsyncKeyState(VK_LBUTTON) & 0x8000) == 0) SaveRect();
}

void FloatUiWindow::PlaceAtMouse(int header_mid)
{
    place_mouse_ = true;
    header_mid_ = std::max(0, header_mid);
}

void FloatUiWindow::SetDragArea(float x0, float y0, float x1, float y1, bool over_widget)
{
    drag_[0] = x0;
    drag_[1] = y0;
    drag_[2] = x1;
    drag_[3] = y1;
    drag_over_widget_ = over_widget;
}

bool FloatUiWindow::TakeCloseRequest()
{
    const bool r = close_requested_;
    close_requested_ = false;
    return r;
}

bool FloatUiWindow::HasFocus() const
{
    if (!hwnd_ || !IsWindowVisible(hwnd_)) return false;
    const HWND f = GetFocus();
    return f == hwnd_ || (f && IsChild(hwnd_, f));
}

bool FloatUiWindow::WantsText() const
{
    return ctx_ && ctx_->IO.WantTextInput;
}

bool FloatUiWindow::IsShown() const
{
    return hwnd_ && IsWindowVisible(hwnd_);
}

bool FloatUiWindow::Owns(HWND hwnd) const
{
    return hwnd_ && hwnd && (hwnd == hwnd_ || IsChild(hwnd_, hwnd));
}

int FloatUiWindow::RouteKey(const MSG* msg, bool* is_float)
{
    if (is_float) *is_float = false;
    if (!msg) return host::kFloatRouteReaper;
    bool owned = false, wants_text = false;
    for (const FloatUiWindow* w : g_floats) {
        if (!w) continue;
        owned = owned || w->Owns(msg->hwnd);
        wants_text = wants_text || w->WantsText();
    }
    if (!owned) return host::kFloatRouteReaper;
    if (is_float) *is_float = true;
    const bool ctrl = GetKeyState(VK_CONTROL) < 0, alt = GetKeyState(VK_MENU) < 0;
    return host::FloatWindowKeyRoute(msg->message, static_cast<unsigned>(msg->wParam), ctrl, alt, wants_text);
}

void FloatUiWindow::Hide()
{
    if (!hwnd_ || !IsWindowVisible(hwnd_)) return;
    if (rect_dirty_) SaveRect();
    ShowWindow(hwnd_, SW_HIDE);
}

void FloatUiWindow::Destroy()
{
    if (ctx_) {
        ContextScope scope(ctx_);
        // Its GL objects belong to the shared context, not to a DC: the viewer's DC does when its own is gone.
        const bool own_dc = hwnd_ && dc_ && IsWindow(hwnd_);
        if (rc_ && ((own_dc && GlMakeCurrent(dc_, rc_)) || GlMakeCurrent(viewer_dc_, rc_))) ImGui_ImplOpenGL3_Shutdown();
        if (rc_) GlMakeCurrent(viewer_dc_, rc_);
        ImGui_ImplWin32_Shutdown();
        // ImGui 1.92: its backend destroyed only the textures it alone used (RefCount 1): the shared atlas's stay.
    }
    if (ctx_) {
        ImGuiContext* prev = ImGui::GetCurrentContext();
        ImGui::DestroyContext(ctx_);  // the shared atlas is not its own: kept
        ImGui::SetCurrentContext(prev == ctx_ ? nullptr : prev);
        ctx_ = nullptr;
    }
    if (hwnd_ && IsWindow(hwnd_)) {
        if (rect_dirty_) SaveRect();
        if (dc_) ReleaseDC(hwnd_, dc_);
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
    }
    hwnd_ = nullptr;
    dc_ = nullptr;
    atlas_ = nullptr;
    failed_ = false;
}

void FloatUiWindow::SetFullScreenOwner(HWND full_screen)
{
    // Leaving: back to the full screen window's own owner (REAPER's main window).
    HWND to = full_screen ? full_screen : (g_full_screen_owner ? GetWindow(g_full_screen_owner, GW_OWNER) : nullptr);
    g_full_screen_owner = full_screen;
    if (to) EnumThreadWindows(GetCurrentThreadId(), &ReownProc, reinterpret_cast<LPARAM>(to));
}

void FloatUiWindow::UnregisterWindowClass()
{
    if (g_class_inst) UnregisterClassW(kClass, g_class_inst);
    g_class_inst = nullptr;
}

void FloatUiWindow::SaveRect()
{
    rect_dirty_ = false;
    RECT r = {};
    if (!hwnd_ || !GetWindowRect(hwnd_, &r)) return;
    if (!at_mouse_) {  // 13-68e: a window placed at the mouse keeps only its size
        SavePrefFloat(Key(pref_, "x").c_str(), static_cast<float>(r.left));
        SavePrefFloat(Key(pref_, "y").c_str(), static_cast<float>(r.top));
    }
    SavePrefFloat(Key(pref_, "w").c_str(), static_cast<float>(r.right - r.left));
    SavePrefFloat(Key(pref_, "h").c_str(), static_cast<float>(r.bottom - r.top));
}

LRESULT FloatUiWindow::Proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (ctx_) {
        ContextScope scope(ctx_);
        if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wp, lp)) return 1;
    }
    switch (msg) {
    case WM_NCCALCSIZE:
        if (wp) return 0;  // no system frame: the client area is the whole window (the edges still resize)
        break;
    case WM_NCHITTEST: {
        POINT pt = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
        ScreenToClient(hwnd, &pt);
        RECT rc = {};
        GetClientRect(hwnd, &rc);
        switch (host::FloatHitTest(pt.x, pt.y, ToIRect(rc), ui::UiScale(), drag_, drag_over_widget_)) {
        case host::FloatHit::TopLeft: return HTTOPLEFT;
        case host::FloatHit::TopRight: return HTTOPRIGHT;
        case host::FloatHit::BottomLeft: return HTBOTTOMLEFT;
        case host::FloatHit::BottomRight: return HTBOTTOMRIGHT;
        case host::FloatHit::Left: return HTLEFT;
        case host::FloatHit::Right: return HTRIGHT;
        case host::FloatHit::Top: return HTTOP;
        case host::FloatHit::Bottom: return HTBOTTOM;
        case host::FloatHit::Caption: return HTCAPTION;
        case host::FloatHit::Client: break;
        }
        return HTCLIENT;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = reinterpret_cast<MINMAXINFO*>(lp);
        mm->ptMinTrackSize.x = static_cast<LONG>(host::FloatMinSize(min_w_, ui::UiScale()));
        mm->ptMinTrackSize.y = static_cast<LONG>(host::FloatMinSize(min_h_, ui::UiScale()));
        return 0;
    }
    case WM_MOVE:
    case WM_SIZE:
        rect_dirty_ = true;
        break;
    case WM_EXITSIZEMOVE:
        SaveRect();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_NCDESTROY:  // destroyed by Windows (its owner went): the next Frame frees its context and makes a new one
        if (rect_dirty_) SaveRect();
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        break;
    case WM_CLOSE:  // Alt+F4: hidden, and the content is told (TakeCloseRequest)
        close_requested_ = true;
        Hide();
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace rav

#endif  // _WIN32
