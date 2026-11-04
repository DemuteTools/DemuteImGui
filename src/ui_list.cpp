// SPDX-License-Identifier: MIT
#include "ui_list.h"

#include <imgui_internal.h>  // GImGui->CurrentItemFlags: a list drawn disabled

#include <map>

namespace rav {
namespace ui {

namespace {

struct Keyboard {
    ImGuiID owner = 0;          // the list clicked last
    int owner_seen = -2;        // the frame it was last drawn
    bool owner_multi = false;
    ImGuiID primary = 0;        // the main list of what is shown
    int primary_seen = -2;
    bool primary_multi = false;
};

std::map<ImGuiContext*, Keyboard>& keyboards()
{
    static std::map<ImGuiContext*, Keyboard> k;
    return k;
}

// The list that has the keyboard at `frame` (0: none), and whether it allows several rows.
ImGuiID holder(const Keyboard& k, int frame, bool* multi)
{
    if (k.owner && k.owner_seen >= frame - 1) {
        if (multi) *multi = k.owner_multi;
        return k.owner;
    }
    if (k.primary && k.primary_seen >= frame - 1) {
        if (multi) *multi = k.primary_multi;
        return k.primary;
    }
    if (multi) *multi = false;
    return 0;
}

}  // namespace

ListClick ListClickNow()
{
    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyShift) return ListClick::Range;
    if (io.KeyCtrl) return ListClick::Toggle;
    return ListClick::Alone;
}

ListKeysState ListKeys(const char* id, const ImVec2& min, const ImVec2& max, const ListKeysOpts& opts)
{
    ListKeysState out;
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) return out;
    Keyboard& k = keyboards()[ctx];
    const ImGuiID me = ImGui::GetID(id);
    const int frame = ImGui::GetFrameCount();
    const bool popup = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel);
    // A click (left or right: a row's menu) outside popups gives the keyboard to the list under it, takes it from
    // this one elsewhere.
    if (!popup && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right))) {
        const bool inside =
            ImGui::IsMouseHoveringRect(min, max, false) &&
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
        if (inside) {
            k.owner = me;
            out.clicked = true;
        } else if (k.owner == me) {
            k.owner = 0;
        }
    }
    if (k.owner == me) {
        k.owner_seen = frame;
        k.owner_multi = opts.multi;
    }
    if (opts.primary) {
        k.primary = me;
        k.primary_seen = frame;
        k.primary_multi = opts.multi;
    } else if (k.primary == me) {
        k.primary = 0;
    }
    out.has_keyboard = holder(k, frame, nullptr) == me;
    // A list drawn disabled (under a veil) never selects.
    const bool disabled = (GImGui->CurrentItemFlags & ImGuiItemFlags_Disabled) != 0;
    if (!out.has_keyboard || !opts.multi || popup || disabled) return out;
    const ImGuiIO& io = ImGui::GetIO();
    out.select_all = io.KeyCtrl && !io.KeyAlt && !io.KeyShift && !io.WantTextInput && ImGui::IsKeyPressed(ImGuiKey_A, false) &&
                     ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    return out;
}

bool ListWantsSelectAll(ImGuiContext* ctx)
{
    if (!ctx) ctx = ImGui::GetCurrentContext();
    if (!ctx) return false;
    const auto it = keyboards().find(ctx);
    if (it == keyboards().end()) return false;
    bool multi = false;
    // That context's frame count (the last frame started), read with it current for a moment.
    ImGuiContext* prev = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(ctx);
    const int frame = ImGui::GetFrameCount();
    ImGui::SetCurrentContext(prev);
    return holder(it->second, frame, &multi) != 0 && multi;
}

void ListKeysForget(ImGuiContext* ctx)
{
    keyboards().erase(ctx);
}

}  // namespace ui
}  // namespace rav
