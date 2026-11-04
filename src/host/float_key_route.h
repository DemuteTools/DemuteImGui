// SPDX-License-Identifier: MIT
//
// Which keys a floating tool window (FloatUiWindow) keeps, pure: the decision REAPER's accelerator
// hook makes for a key message aimed at one of them (RAV's ViewerTranslateAccel, 13-15). Typing in a
// field, and its own keys (Esc, the arrows, Enter, Tab, Delete / Backspace, with neither Ctrl nor Alt),
// go to the window; Ctrl+A too while a list of the window has the keyboard (ui_list.h); every other key
// (Space, Ctrl+Z...) stays REAPER's. Alt+F4 closes it.
//
// No Windows header: the message and virtual-key codes are constants here, checked against the SDK by a
// static_assert in float_ui_window.cpp.

#pragma once

namespace rav {
namespace host {

// Window messages (WM_*).
constexpr unsigned kMsgKeyDown = 0x0100, kMsgKeyUp = 0x0101, kMsgChar = 0x0102, kMsgSysKeyDown = 0x0104,
                   kMsgSysKeyUp = 0x0105, kMsgSysChar = 0x0106;
// Virtual keys (VK_*).
constexpr unsigned kVkBack = 0x08, kVkTab = 0x09, kVkReturn = 0x0D, kVkEscape = 0x1B, kVkLeft = 0x25, kVkUp = 0x26,
                   kVkRight = 0x27, kVkDown = 0x28, kVkDelete = 0x2E, kVkF4 = 0x73, kVkA = 0x41;

// What the hook answers REAPER: keep the key (0), hand it to the window (-1), hand it to the window as a
// system key (-20: WM_SYSKEY* / WM_SYSCHAR, which REAPER would otherwise drop).
constexpr int kFloatRouteReaper = 0, kFloatRouteWindow = -1, kFloatRouteWindowSys = -20;

// `msg` the message, `vk` its wParam, `ctrl` / `alt` held now, `any_wants_text` a text field of a floating
// window is active, `list_select_all` a list of the window has the keyboard and allows several rows
// (ui::ListWantsSelectAll): Ctrl+A (key down / up) is the window's, every row of that list; otherwise REAPER's.
inline int FloatWindowKeyRoute(unsigned msg, unsigned vk, bool ctrl, bool alt, bool any_wants_text,
                               bool list_select_all = false)
{
    const bool key_msg = msg == kMsgKeyDown || msg == kMsgKeyUp || msg == kMsgChar || msg == kMsgSysKeyDown ||
                         msg == kMsgSysKeyUp || msg == kMsgSysChar;
    if (!key_msg) return kFloatRouteReaper;
    if (msg == kMsgSysKeyDown && vk == kVkF4) return kFloatRouteWindowSys;  // Alt+F4 closes it
    if (any_wants_text)
        return msg == kMsgSysKeyDown || msg == kMsgSysKeyUp || msg == kMsgSysChar ? kFloatRouteReaper : kFloatRouteWindow;
    const bool own = vk == kVkEscape || vk == kVkUp || vk == kVkDown || vk == kVkLeft || vk == kVkRight || vk == kVkReturn ||
                     vk == kVkTab || vk == kVkDelete || vk == kVkBack;
    if ((msg == kMsgKeyDown || msg == kMsgKeyUp) && !ctrl && !alt && own) return kFloatRouteWindow;
    if ((msg == kMsgKeyDown || msg == kMsgKeyUp) && ctrl && !alt && vk == kVkA && list_select_all) return kFloatRouteWindow;
    return kFloatRouteReaper;
}

}  // namespace host
}  // namespace rav
