// SPDX-License-Identifier: MIT
//
// The OpenGL context of a window, as RAV makes it (viewer_window.cpp): a legacy pixel format (32-bit
// RGBA, 24-bit depth, 8-bit stencil, double buffered, single sample) on the window's own DC (its class
// must be CS_OWNDC), and a WGL context on it. Plus the scopes a REAPER plugin needs on the main thread,
// shared with other plugins that make their own GL and ImGui contexts current. Windows only.

#pragma once

#include <windows.h>

struct ImGuiContext;

namespace rav {
namespace host {

// Makes `*dc` (GetDC of `hwnd`) and `*rc` on it. False when the pixel format or the context cannot be
// made: the DC is then released and both stay null (nothing is left).
bool CreateGlWindowContext(HWND hwnd, HDC* dc, HGLRC* rc);

// Drops the context (made not current first) and releases the DC. `owner` may be null (a window whose
// creation failed inside WM_CREATE has no handle yet): the DC's own window is found. Both end null.
void DestroyGlWindowContext(HWND owner, HDC* dc, HGLRC* rc);

// wglSwapIntervalEXT when the driver has it (0 = vsync off); its absence is not a failure.
void SetSwapInterval(int interval);

// Makes (`dc`, `rc`) current for a scope when another context is current (stolen by another plugin on
// the same thread) or this one on another DC, then puts the previous one back. ok() is false when it
// could not be made current.
class GlCurrentScope {
public:
    GlCurrentScope(HDC dc, HGLRC rc);
    ~GlCurrentScope();
    bool ok() const { return ok_; }
    GlCurrentScope(const GlCurrentScope&) = delete;
    GlCurrentScope& operator=(const GlCurrentScope&) = delete;

private:
    HDC   prev_dc_ = nullptr;
    HGLRC prev_rc_ = nullptr;
    bool  switched_ = false;
    bool  ok_ = false;
};

// Makes an ImGui context current for a scope (when non-null and not current already), then puts the
// previous one back.
class ImGuiContextScope {
public:
    explicit ImGuiContextScope(ImGuiContext* ctx);
    ~ImGuiContextScope();
    ImGuiContextScope(const ImGuiContextScope&) = delete;
    ImGuiContextScope& operator=(const ImGuiContextScope&) = delete;

private:
    ImGuiContext* prev_ = nullptr;
    bool          switched_ = false;
};

// glBindFramebuffer(GL_FRAMEBUFFER, 0), the window's own framebuffer (the entry point is resolved once by
// wglGetProcAddress, under a private name: no clash with the tool's GL loader). A context must be current.
void BindDefaultFramebuffer();

}  // namespace host
}  // namespace rav
