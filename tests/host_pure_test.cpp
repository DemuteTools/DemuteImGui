// SPDX-License-Identifier: MIT
//
// The pure parts of the host target, on every system (no window, no windows.h): the persisted float
// (host_prefs.h), the floating window's geometry (float_geometry.h), its key routing (float_key_route.h)
// and the injected services (host_services.cpp: absent, the tool's pair, REAPER's ExtState).

#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "float_geometry.h"
#include "float_key_route.h"
#include "host_prefs.h"
#include "host_services.h"

using namespace rav::host;

namespace {

int g_fail = 0;
#define CHECK(c)                                                     \
    do {                                                             \
        if (!(c)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            ++g_fail;                                                \
        }                                                            \
    } while (0)

void TestPrefs()
{
    CHECK(ParsePrefFloat("123.0", 5.0f) == 123.0f);
    CHECK(ParsePrefFloat("-40.5", 5.0f) == -40.5f);
    CHECK(ParsePrefFloat("abc", 5.0f) == 5.0f);
    CHECK(ParsePrefFloat("", 5.0f) == 5.0f);
    CHECK(ParsePrefFloat(nullptr, 5.0f) == 5.0f);
    CHECK(ParsePrefFloat("1e9", 5.0f) == 5.0f);    // out of +-1e6
    CHECK(ParsePrefFloat("-1e9", 5.0f) == 5.0f);
    CHECK(ParsePrefFloat("nan", 5.0f) == 5.0f);
    CHECK(ParsePrefFloat("inf", 5.0f) == 5.0f);
    CHECK(ParsePrefFloat("12px", 5.0f) == 5.0f);   // trailing text
    CHECK(ParsePrefFloat("1e6", 5.0f) == 1.0e6f);  // the bound itself is kept
    CHECK(FormatPrefFloat(123.0f) == "123.0");
    CHECK(FormatPrefFloat(-7.5f) == "-7.5");
    CHECK(FormatPrefFloat(0.04f) == "0.0");
    CHECK(FormatPrefFloat(640.0f) == "640.0");
    CHECK(ParsePrefFloat(FormatPrefFloat(812.0f).c_str(), 0.0f) == 812.0f);  // round trip
    CHECK(PrefKey("assign.float", "x") == "assign.float.x");
}

void TestGeometry()
{
    // First size: the default at the scale, the floor at the scale.
    CHECK(FloatDefaultSizePref(400, 1.5f) == 600.0f);
    CHECK(FloatDefaultSizePref(400, 0.5f) == 400.0f);  // max(1, scale)
    FloatRect s = FloatInitialSize(600.0f, 300.0f, 320, 240, 1.5f);
    CHECK(s.w == 600 && s.h == 360);  // 240 x 1.5 floor
    s = FloatInitialSize(100.0f, 100.0f, 320, 240, 1.0f);
    CHECK(s.w == 320 && s.h == 240);
    CHECK(FloatMinSize(320, 2.0f) == 640);

    // Default place: beside the viewer's right edge, 24 px in, 40 px under its top (at the scale).
    IRect viewer;
    viewer.left = 100;
    viewer.top = 50;
    viewer.right = 1100;
    viewer.bottom = 850;
    FloatRect p = FloatInitialPosition(kFloatNoPosition, kFloatNoPosition, 400, viewer, 1.0f);
    CHECK(p.x == 1100 - 400 - 24 && p.y == 90);
    p = FloatInitialPosition(kFloatNoPosition, kFloatNoPosition, 400, viewer, 2.0f);
    CHECK(p.x == 1100 - 400 - 48 && p.y == 130);
    p = FloatInitialPosition(kFloatNoPosition, kFloatNoPosition, 2000, viewer, 1.0f);
    CHECK(p.x == 100);  // never left of the viewer
    p = FloatInitialPosition(10.0f, kFloatNoPosition, 400, viewer, 1.0f);
    CHECK(p.x == 676 && p.y == 90);  // one coordinate missing: both default
    p = FloatInitialPosition(-99998.0f, 20.0f, 400, viewer, 1.0f);
    CHECK(p.x == -99998 && p.y == 20);  // a saved position is kept as is (fitted afterwards)

    // A rect far off the screen comes back wholly on the work area, its size bounded.
    IRect wa;
    wa.left = 0;
    wa.top = 0;
    wa.right = 1920;
    wa.bottom = 1040;
    FloatRect r;
    r.x = 5000;
    r.y = -3000;
    r.w = 400;
    r.h = 300;
    r = FloatFitToWorkArea(r, wa);
    CHECK(r.x == 1520 && r.y == 0 && r.w == 400 && r.h == 300);
    r.x = -99998;
    r.y = 90000;
    r.w = 2500;
    r.h = 1200;
    r = FloatFitToWorkArea(r, wa);
    CHECK(r.x == 0 && r.y == 0 && r.w == 1920 && r.h == 1040);
    IRect wa2;  // a second monitor left of the first
    wa2.left = -1280;
    wa2.top = 0;
    wa2.right = 0;
    wa2.bottom = 1024;
    r.x = -2000;
    r.y = 500;
    r.w = 400;
    r.h = 600;
    r = FloatFitToWorkArea(r, wa2);
    CHECK(r.x == -1280 && r.y == 424);

    // At the mouse: the pointer at the title row's middle; resize only when it had to shrink.
    FloatMousePlacement m = FloatPlaceAtMouse(800, 500, 400, 300, 14, &wa);
    CHECK(m.rect.x == 600 && m.rect.y == 486 && !m.resize && m.rect.w == 400);
    m = FloatPlaceAtMouse(10, 1030, 400, 300, 14, &wa);
    CHECK(m.rect.x == 0 && m.rect.y == 740 && !m.resize);
    m = FloatPlaceAtMouse(800, 500, 2400, 300, 14, &wa);
    CHECK(m.resize && m.rect.w == 1920 && m.rect.x == 0);
    m = FloatPlaceAtMouse(800, 500, 400, 300, 14, nullptr);
    CHECK(m.rect.x == 600 && m.rect.y == 486 && !m.resize);

    // Hit-test: 6 px edges at the scale (never under 4), corners first, the caption in the drag area.
    CHECK(FloatBorderPx(1.0f) == 6);
    CHECK(FloatBorderPx(0.5f) == 6);
    CHECK(FloatBorderPx(2.0f) == 12);
    IRect client;
    client.right = 400;
    client.bottom = 300;
    const float drag[4] = {0.0f, 0.0f, 400.0f, 28.0f};
    const float none[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    CHECK(FloatHitTest(2, 2, client, 1.0f, drag, false) == FloatHit::TopLeft);
    CHECK(FloatHitTest(397, 2, client, 1.0f, drag, false) == FloatHit::TopRight);
    CHECK(FloatHitTest(2, 297, client, 1.0f, drag, false) == FloatHit::BottomLeft);
    CHECK(FloatHitTest(397, 297, client, 1.0f, drag, false) == FloatHit::BottomRight);
    CHECK(FloatHitTest(2, 150, client, 1.0f, drag, false) == FloatHit::Left);
    CHECK(FloatHitTest(394, 150, client, 1.0f, drag, false) == FloatHit::Right);
    CHECK(FloatHitTest(393, 150, client, 1.0f, drag, false) == FloatHit::Client);
    CHECK(FloatHitTest(200, 5, client, 1.0f, drag, false) == FloatHit::Top);
    CHECK(FloatHitTest(200, 294, client, 1.0f, drag, false) == FloatHit::Bottom);
    CHECK(FloatHitTest(200, 10, client, 1.0f, drag, false) == FloatHit::Caption);
    CHECK(FloatHitTest(200, 10, client, 1.0f, drag, true) == FloatHit::Client);  // over a widget
    CHECK(FloatHitTest(200, 28, client, 1.0f, drag, false) == FloatHit::Client);  // y1 excluded
    CHECK(FloatHitTest(200, 10, client, 1.0f, none, false) == FloatHit::Client);
    CHECK(FloatHitTest(200, 10, client, 2.0f, drag, false) == FloatHit::Top);  // 12 px edge at 2x
}

void TestKeyRoute()
{
    const unsigned own[] = {kVkEscape, kVkUp, kVkDown, kVkLeft, kVkRight, kVkReturn, kVkTab, kVkDelete, kVkBack};
    for (unsigned vk : own) {
        CHECK(FloatWindowKeyRoute(kMsgKeyDown, vk, false, false, false) == kFloatRouteWindow);
        CHECK(FloatWindowKeyRoute(kMsgKeyUp, vk, false, false, false) == kFloatRouteWindow);
        CHECK(FloatWindowKeyRoute(kMsgKeyDown, vk, true, false, false) == kFloatRouteReaper);  // Ctrl
        CHECK(FloatWindowKeyRoute(kMsgKeyDown, vk, false, true, false) == kFloatRouteReaper);  // Alt
        CHECK(FloatWindowKeyRoute(kMsgChar, vk, false, false, false) == kFloatRouteReaper);
    }
    CHECK(FloatWindowKeyRoute(kMsgKeyDown, 0x20, false, false, false) == kFloatRouteReaper);  // Space plays
    CHECK(FloatWindowKeyRoute(kMsgKeyDown, 'Z', true, false, false) == kFloatRouteReaper);    // Ctrl+Z
    CHECK(FloatWindowKeyRoute(kMsgSysKeyDown, kVkF4, false, true, false) == kFloatRouteWindowSys);
    CHECK(FloatWindowKeyRoute(kMsgSysKeyDown, kVkF4, false, true, true) == kFloatRouteWindowSys);  // even typing
    CHECK(FloatWindowKeyRoute(kMsgSysKeyUp, kVkF4, false, true, false) == kFloatRouteReaper);
    // A text field active: every key message is the window's, the SYS ones excepted.
    CHECK(FloatWindowKeyRoute(kMsgKeyDown, 0x20, false, false, true) == kFloatRouteWindow);
    CHECK(FloatWindowKeyRoute(kMsgKeyDown, 'Z', true, false, true) == kFloatRouteWindow);
    CHECK(FloatWindowKeyRoute(kMsgChar, 'a', false, false, true) == kFloatRouteWindow);
    CHECK(FloatWindowKeyRoute(kMsgKeyUp, 'a', false, false, true) == kFloatRouteWindow);
    CHECK(FloatWindowKeyRoute(kMsgSysKeyDown, 'S', false, true, true) == kFloatRouteReaper);
    CHECK(FloatWindowKeyRoute(kMsgSysKeyUp, 'S', false, true, true) == kFloatRouteReaper);
    CHECK(FloatWindowKeyRoute(kMsgSysChar, 's', false, true, true) == kFloatRouteReaper);
    // Not a key message.
    CHECK(FloatWindowKeyRoute(0x0200 /* WM_MOUSEMOVE */, 0, false, false, false) == kFloatRouteReaper);
    CHECK(FloatWindowKeyRoute(0x020A /* WM_MOUSEWHEEL */, 0, false, false, true) == kFloatRouteReaper);
    CHECK(FloatWindowKeyRoute(0x0103 /* WM_DEADCHAR */, 0, false, false, true) == kFloatRouteReaper);
}

// Stand-ins for the tool's / REAPER's functions.
std::map<std::string, std::string> g_ext;  // "section/key" -> value
std::vector<std::string>           g_calls;

const char* FakeGetExtState(const char* section, const char* key)
{
    const auto it = g_ext.find(std::string(section) + "/" + key);
    return it == g_ext.end() ? "" : it->second.c_str();  // REAPER answers "" for a missing key
}
void FakeSetExtState(const char* section, const char* key, const char* value, bool persist)
{
    g_ext[std::string(section) + "/" + key] = value;
    g_calls.push_back(std::string("set ") + section + "/" + key + "=" + value + (persist ? " persist" : ""));
}
float FakeLoad(const char* key, float def)
{
    return std::strcmp(key, "k") == 0 ? 42.0f : def;
}
void FakeSave(const char* key, float value)
{
    g_calls.push_back(std::string("save ") + key + "=" + FormatPrefFloat(value));
}
void FakeDockAdd(HWND, const char* name, const char* ident, bool show)
{
    g_calls.push_back(std::string("dock ") + name + " " + ident + (show ? " show" : ""));
}
void FakeDockActivate(HWND)
{
    g_calls.push_back("activate");
}
void FakeDockRemove(HWND)
{
    g_calls.push_back("remove");
}
void FakeLog(LogLevel level, const char* text)
{
    g_calls.push_back(std::string(level == LogLevel::Error ? "error " : "log ") + text);
}

void TestServices()
{
    // Absent: defaults, nothing written, no crash.
    SetHostServices(HostServices{});
    CHECK(LoadPrefFloat("a.x", 7.0f) == 7.0f);
    CHECK(LoadPrefFloat(nullptr, 7.0f) == 7.0f);
    SavePrefFloat("a.x", 1.0f);
    DockAddEx(nullptr, "n", "i", true);
    DockActivate(nullptr);
    DockRemove(nullptr);
    Log(LogLevel::Info, "nothing %d", 1);
    CHECK(g_calls.empty());

    // REAPER's ExtState and a section (copied: the caller's buffer may go).
    {
        std::string section = "ReaAnimViewer";
        HostServices s;
        s.get_ext_state = &FakeGetExtState;
        s.set_ext_state = &FakeSetExtState;
        s.ext_section = section.c_str();
        s.dock_add_ex = &FakeDockAdd;
        s.dock_activate = &FakeDockActivate;
        s.dock_remove = &FakeDockRemove;
        s.log = &FakeLog;
        SetHostServices(s);
        section = "overwritten";
    }
    CHECK(std::strcmp(GetHostServices().ext_section, "ReaAnimViewer") == 0);
    CHECK(LoadPrefFloat("a.x", 7.0f) == 7.0f);  // missing: ""
    SavePrefFloat("a.x", 123.0f);
    CHECK(g_ext["ReaAnimViewer/a.x"] == "123.0");
    CHECK(!g_calls.empty() && g_calls.back() == "set ReaAnimViewer/a.x=123.0 persist");
    CHECK(LoadPrefFloat("a.x", 7.0f) == 123.0f);
    g_ext["ReaAnimViewer/a.y"] = "abc";
    CHECK(LoadPrefFloat("a.y", 7.0f) == 7.0f);
    g_ext["ReaAnimViewer/a.y"] = "1e9";
    CHECK(LoadPrefFloat("a.y", 7.0f) == 7.0f);
    g_ext["ReaAnimViewer/a.y"] = "nan";
    CHECK(LoadPrefFloat("a.y", 7.0f) == 7.0f);
    DockAddEx(nullptr, "RAW", "RAW.Panel", true);
    CHECK(g_calls.back() == "dock RAW RAW.Panel show");
    DockActivate(nullptr);
    CHECK(g_calls.back() == "activate");
    DockRemove(nullptr);
    CHECK(g_calls.back() == "remove");
    Log(LogLevel::Error, "%s: %d", "panel", 3);
    CHECK(g_calls.back() == "error panel: 3");

    // ExtState without a section: defaults, nothing written.
    HostServices nosec;
    nosec.get_ext_state = &FakeGetExtState;
    nosec.set_ext_state = &FakeSetExtState;
    SetHostServices(nosec);
    g_calls.clear();
    CHECK(LoadPrefFloat("a.x", 7.0f) == 7.0f);
    SavePrefFloat("a.x", 1.0f);
    CHECK(g_calls.empty());

    // The tool's own pair wins over ExtState.
    HostServices pair = nosec;
    pair.ext_section = "S";
    pair.load_pref_float = &FakeLoad;
    pair.save_pref_float = &FakeSave;
    SetHostServices(pair);
    CHECK(LoadPrefFloat("k", 1.0f) == 42.0f);
    CHECK(LoadPrefFloat("other", 1.0f) == 1.0f);
    SavePrefFloat("k", 5.0f);
    CHECK(g_calls.size() == 1 && g_calls.back() == "save k=5.0");
    SetHostServices(HostServices{});
}

}  // namespace

int main()
{
    TestPrefs();
    TestGeometry();
    TestKeyRoute();
    TestServices();
    if (g_fail) {
        std::printf("host_pure: %d failure(s)\n", g_fail);
        return 1;
    }
    std::printf("host_pure: ok\n");
    return 0;
}
