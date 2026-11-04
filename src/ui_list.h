// SPDX-License-Identifier: MIT
//
// The standard gestures of a list of rows, a default of the library (RAW, 2026-11-04: "ce genre de comportement
// devrait être de base dans n'importe quelle liste et faire partie de la librairie"). A list never codes them
// again; it says which rows it shows (their order) and applies the result to its own selection:
//   - a click takes the row alone, Ctrl+click toggles it, Shift+click takes the range from the anchor (the row
//     clicked last without Shift) in the list's order: ListClickNow + ListPick;
//   - Ctrl+A selects every row (ListSelectAll) when ListKeys says so: the list has the keyboard, it allows
//     several rows, no text field types (Ctrl+A is the field's then), no popup is open, neither Alt nor Shift is
//     held, the list's window is focused and the list is not drawn disabled.
// The keyboard of lists, one per ImGui context: the list clicked last (left or right button, outside popups);
// a click elsewhere takes it away. A `primary` list (the main list of what is shown) has it while no other
// list was clicked (Ctrl+A works without a click in it first); a list not drawn for a frame loses it.
// A host routes Ctrl+A to its window rather than to the host application only while ListWantsSelectAll.
//
// The templates are pure (no ImGui); ListKeys, ListWantsSelectAll and ListKeysForget need ImGui.

#pragma once

#include <imgui.h>

#include <algorithm>
#include <vector>

namespace rav {
namespace ui {

enum class ListClick { Alone, Toggle, Range };

// The click's meaning from the modifiers held now (Shift: Range, else Ctrl: Toggle, else Alone).
ListClick ListClickNow();

// Applies a click on `id` to `selected` (in click order) and `anchor`: Alone takes it alone; Toggle adds or removes
// it; Range takes the rows of `order` from the anchor to it (a Range with no anchor, or an anchor or a row not in
// `order`, is Alone). The anchor moves to the row clicked, except on a Range. True when `selected` changed.
template <class K>
bool ListPick(std::vector<K>& selected, K& anchor, const K& id, const std::vector<K>& order, ListClick how)
{
    const std::vector<K> before = selected;
    if (how == ListClick::Range) {
        const auto a = std::find(order.begin(), order.end(), anchor);
        const auto b = std::find(order.begin(), order.end(), id);
        if (a != order.end() && b != order.end()) {
            selected.assign(std::min(a, b), std::max(a, b) + 1);
            return selected != before;
        }
    }
    anchor = id;
    if (how == ListClick::Toggle) {
        const auto it = std::find(selected.begin(), selected.end(), id);
        if (it != selected.end()) selected.erase(it);
        else selected.push_back(id);
        return true;
    }
    selected.assign(1, id);
    return selected != before;
}

// Ctrl+A: every row of `order`, in its order. True when `selected` changed.
template <class K>
bool ListSelectAll(std::vector<K>& selected, const std::vector<K>& order)
{
    if (selected == order) return false;
    selected = order;
    return true;
}

struct ListKeysOpts {
    bool multi = true;     // the list allows several rows (else Ctrl+A is not its: it never selects all)
    bool primary = false;  // the main list of what is shown: it has the keyboard while no other list was clicked
};

struct ListKeysState {
    bool has_keyboard = false;  // the list has the keyboard of its context now
    bool clicked = false;       // a click in it took the keyboard at this frame
    bool select_all = false;    // Ctrl+A: select every row now
};

// Once per frame the list is shown, after its rows, with the rectangle it covers (screen coordinates: the visible
// part of a scrolling list). `id` is pushed on the ID stack (two lists of a window need two ids).
ListKeysState ListKeys(const char* id, const ImVec2& min, const ImVec2& max, const ListKeysOpts& opts = ListKeysOpts());

// A list of that context (null: the current one) has the keyboard and allows several rows now: a host hands
// Ctrl+A to that context's window.
bool ListWantsSelectAll(ImGuiContext* ctx = nullptr);

// Forgets the lists' keyboard of a context (call before destroying it).
void ListKeysForget(ImGuiContext* ctx);

}  // namespace ui
}  // namespace rav
