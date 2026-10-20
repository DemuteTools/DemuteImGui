// SPDX-License-Identifier: MIT
//
// The geometry of a floating tool window (FloatUiWindow), pure: its first size, its default place
// beside the viewer, the fit to a monitor's work area, the placement at the mouse and the hit-test
// of its frameless edges. No Windows header: the Windows code passes plain rectangles and tests run on
// the host. The arithmetic is RAV's (float_ui_window.cpp Create, Frame and WM_NCHITTEST), unchanged.
//
// `ui_scale` is the theme's scale (ui::UiScale()); every computation uses max(1, ui_scale) as RAV.

#pragma once

namespace rav {
namespace host {

// A rectangle in screen (or client) pixels, as a Win32 RECT: right and bottom excluded.
struct IRect {
    int left = 0, top = 0, right = 0, bottom = 0;
};

// A window rect as a position and a size.
struct FloatRect {
    int x = 0, y = 0, w = 0, h = 0;
};

// The value read for an x / y never saved (ExtState default): the window then goes beside the viewer.
constexpr float kFloatNoPosition = -100000.0f;

inline float FloatScale(float ui_scale)
{
    return ui_scale > 1.0f ? ui_scale : 1.0f;  // std::max(1.0f, ui_scale)
}

namespace detail {
inline int Max(int a, int b) { return a < b ? b : a; }
inline int Min(int a, int b) { return b < a ? b : a; }
}  // namespace detail

// The default of the saved width / height: the default size at the scale.
inline float FloatDefaultSizePref(int default_px, float ui_scale)
{
    return static_cast<float>(default_px) * FloatScale(ui_scale);
}

// The minimum size (WM_GETMINMAXINFO's track size, and the first size's floor).
inline int FloatMinSize(int min_px, float ui_scale)
{
    return static_cast<int>(min_px * FloatScale(ui_scale));
}

// The first size: the saved one (or its default), never under the minimum.
inline FloatRect FloatInitialSize(float pref_w, float pref_h, int min_w, int min_h, float ui_scale)
{
    FloatRect r;
    r.w = detail::Max(static_cast<int>(pref_w), FloatMinSize(min_w, ui_scale));
    r.h = detail::Max(static_cast<int>(pref_h), FloatMinSize(min_h, ui_scale));
    return r;
}

// The first position: the saved one, else beside the viewer's right edge (24 px in) and 40 px under its top.
// `viewer` is the viewer's window rect (screen pixels), `w` the window's width.
inline FloatRect FloatInitialPosition(float pref_x, float pref_y, int w, const IRect& viewer, float ui_scale)
{
    const float s = FloatScale(ui_scale);
    FloatRect   r;
    r.x = static_cast<int>(pref_x);
    r.y = static_cast<int>(pref_y);
    if (r.x <= -99999 || r.y <= -99999) {
        r.x = detail::Max(viewer.left, viewer.right - w - static_cast<int>(24 * s));
        r.y = viewer.top + static_cast<int>(40 * s);
    }
    return r;
}

// Wholly on the work area `wa`: the size bounded by it, then the position moved into it.
inline FloatRect FloatFitToWorkArea(FloatRect r, const IRect& wa)
{
    r.w = detail::Min(r.w, wa.right - wa.left);
    r.h = detail::Min(r.h, wa.bottom - wa.top);
    r.x = detail::Max(wa.left, detail::Min(r.x, wa.right - r.w));
    r.y = detail::Max(wa.top, detail::Min(r.y, wa.bottom - r.h));
    return r;
}

// PlaceAtMouse: the pointer at the middle of the title row (x = mouse - w / 2, y = mouse - header_mid),
// wholly on the work area `wa` of the mouse's monitor (null: no monitor known, only moved). `resize` says
// the size had to shrink to fit (else the window keeps its size: SWP_NOSIZE).
struct FloatMousePlacement {
    FloatRect rect;
    bool      resize = false;
};
inline FloatMousePlacement FloatPlaceAtMouse(int mouse_x, int mouse_y, int w, int h, int header_mid, const IRect* wa)
{
    FloatMousePlacement p;
    p.rect.x = mouse_x - w / 2;
    p.rect.y = mouse_y - header_mid;
    p.rect.w = w;
    p.rect.h = h;
    if (!wa) return p;
    const int ww = wa->right - wa->left, wh = wa->bottom - wa->top;
    p.resize = w > ww || h > wh;
    p.rect = FloatFitToWorkArea(p.rect, *wa);
    return p;
}

// What a point of the client area is: an edge or corner (resize), the caption (moves the window), the client.
enum class FloatHit { Client, Caption, Left, Right, Top, Bottom, TopLeft, TopRight, BottomLeft, BottomRight };

// The width of the resize edges: 6 px at the scale, never under 4.
inline int FloatBorderPx(float ui_scale)
{
    return detail::Max(4, static_cast<int>(6.0f * FloatScale(ui_scale)));
}

// `x`, `y` in client pixels, `client` the client rect; `drag` = {x0, y0, x1, y1} the drag area (client
// pixels, empty = none), `over_widget` the mouse is over a widget there (the click goes to it).
inline FloatHit FloatHitTest(int x, int y, const IRect& client, float ui_scale, const float drag[4], bool over_widget)
{
    const int  b = FloatBorderPx(ui_scale);
    const bool l = x < b, r = x >= client.right - b, t = y < b, d = y >= client.bottom - b;
    if (t && l) return FloatHit::TopLeft;
    if (t && r) return FloatHit::TopRight;
    if (d && l) return FloatHit::BottomLeft;
    if (d && r) return FloatHit::BottomRight;
    if (l) return FloatHit::Left;
    if (r) return FloatHit::Right;
    if (t) return FloatHit::Top;
    if (d) return FloatHit::Bottom;
    if (!over_widget && x >= drag[0] && x < drag[2] && y >= drag[1] && y < drag[3]) return FloatHit::Caption;
    return FloatHit::Client;
}

}  // namespace host
}  // namespace rav
