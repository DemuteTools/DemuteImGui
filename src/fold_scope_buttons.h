// SPDX-License-Identifier: MIT
//
// Which of the Expand all / Collapse all buttons a fold-all scope shows (ui::FoldScopeBar).
// Moved here from RAV's library_tree_nav so the library has no dependency on RAV's Sound model;
// RAV drops its copy when it moves onto this library (story 1.3).

#pragma once

namespace rav {

// 13-33: a fold-all scope shows Expand all / Collapse all only with at least two foldable cards;
// Expand all is on while one is folded, Collapse all while one is open.
struct FoldScopeButtons {
    bool shown = false, can_expand = false, can_collapse = false;
};

inline FoldScopeButtons FoldScopeButtonsFor(int foldable, int open)
{
    FoldScopeButtons b;
    b.shown = foldable >= 2;
    if (!b.shown) return b;
    b.can_expand = open < foldable;
    b.can_collapse = open > 0;
    return b;
}

}  // namespace rav
