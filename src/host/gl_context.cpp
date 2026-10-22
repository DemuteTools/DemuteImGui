// SPDX-License-Identifier: MIT
//
// See gl_context.h. Taken from RAV (viewer_window.cpp CreateGLContextFor, DestroyGLContext, TrySetVsync,
// the context re-assertion of RenderTick and the ImGui context scope of WindowProc).

#include "gl_context.h"

#include <imgui.h>

#include "gl_switch.h"

namespace rav {
namespace host {

bool CreateGlWindowContext(HWND hwnd, HDC* out_dc, HGLRC* out_rc)
{
    if (!out_dc || !out_rc) return false;
    *out_dc = nullptr;
    *out_rc = nullptr;
    if (!hwnd) return false;  // GetDC(nullptr) would be the whole screen's DC: never given a pixel format
    HDC dc = GetDC(hwnd);
    if (!dc) return false;

    // Single sample: a multisample target, if any, is the tool's own offscreen framebuffer (SetPixelFormat
    // is one-shot per HDC and the window is never recreated, so a baked-in level could never change).
    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize        = sizeof(pfd);
    pfd.nVersion     = 1;
    pfd.dwFlags      = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType   = PFD_TYPE_RGBA;
    pfd.cColorBits   = 32;
    pfd.cDepthBits   = 24;
    pfd.cStencilBits = 8;
    pfd.iLayerType   = PFD_MAIN_PLANE;

    const int pf = ChoosePixelFormat(dc, &pfd);
    if (!pf || !SetPixelFormat(dc, pf, &pfd)) {
        ReleaseDC(hwnd, dc);
        return false;
    }

    HGLRC rc = wglCreateContext(dc);
    if (!rc) {
        ReleaseDC(hwnd, dc);
        return false;
    }
    *out_dc = dc;
    *out_rc = rc;
    return true;
}

void DestroyGlWindowContext(HWND owner, HDC* dc, HGLRC* rc)
{
    if (rc && *rc) {
        GlMakeCurrent(nullptr, nullptr);
        wglDeleteContext(*rc);
        *rc = nullptr;
    }
    if (dc && *dc) {
        // The owner is still null if the window's creation bails inside WM_CREATE (its handle is known only
        // once CreateWindowExW returns): the DC's own window is used; the DC never dangles.
        if (HWND w = owner ? owner : WindowFromDC(*dc)) ReleaseDC(w, *dc);
        *dc = nullptr;
    }
}

void SetSwapInterval(int interval)
{
    typedef BOOL(WINAPI * PFNSWAPINTERVAL)(int);
    if (auto swap = reinterpret_cast<PFNSWAPINTERVAL>(reinterpret_cast<void*>(wglGetProcAddress("wglSwapIntervalEXT"))))
        swap(interval);  // absence is not a failure: it just means vsync can't be toggled
}

GlCurrentScope::GlCurrentScope(HDC dc, HGLRC rc)
{
    // Another GL plugin on REAPER's main thread may have made ITS context current: every GL call would then
    // fail silently. wglGetCurrentContext is a cheap TLS read; the switch only happens when stolen, and the
    // previous context is given back so that plugin is not broken by us in turn.
    prev_rc_ = wglGetCurrentContext();
    prev_dc_ = wglGetCurrentDC();
    if (!dc || !rc) return;
    // Its DC too (RAV freeze fix 2026-10-21): a floating window whose switch back timed out leaves this
    // context current on ITS DC.
    switched_ = prev_rc_ != rc || prev_dc_ != dc;
    if (switched_ && !GlMakeCurrent(dc, rc)) {
        switched_ = false;
        return;
    }
    ok_ = true;
}

GlCurrentScope::~GlCurrentScope()
{
    if (switched_) GlMakeCurrent(prev_dc_, prev_rc_);
}

ImGuiContextScope::ImGuiContextScope(ImGuiContext* ctx)
{
    ImGuiContext* cur = ImGui::GetCurrentContext();
    if (ctx && cur != ctx) {
        prev_ = cur;
        ImGui::SetCurrentContext(ctx);
        switched_ = true;
    }
}

ImGuiContextScope::~ImGuiContextScope()
{
    if (switched_) ImGui::SetCurrentContext(prev_);
}

namespace {

typedef void(APIENTRY* PfnDemuteBindFramebuffer)(unsigned int target, unsigned int framebuffer);
PfnDemuteBindFramebuffer g_demute_bind_framebuffer = nullptr;
constexpr unsigned int   kGlFramebuffer = 0x8D40;  // GL_FRAMEBUFFER

}  // namespace

void BindDefaultFramebuffer()
{
    if (!g_demute_bind_framebuffer) {
        PROC p = wglGetProcAddress("glBindFramebuffer");
        // wglGetProcAddress may answer 1, 2, 3 or -1 for "missing" on some drivers.
        const INT_PTR v = reinterpret_cast<INT_PTR>(p);
        if (v == 0 || v == 1 || v == 2 || v == 3 || v == -1) return;  // tried again next call (GL 3.0+ has it)
        g_demute_bind_framebuffer = reinterpret_cast<PfnDemuteBindFramebuffer>(reinterpret_cast<void*>(p));
    }
    g_demute_bind_framebuffer(kGlFramebuffer, 0);
}

}  // namespace host
}  // namespace rav
