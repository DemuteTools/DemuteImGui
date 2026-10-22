// SPDX-License-Identifier: MIT
//
// Story 1.4, no window and no ImGui context: (1) the WCAG contrast of the white text / tick on the
// accent and on the Primary hover, computed from the theme's own tokens (AA: >= 4.5); (2) the 8
// Wwise icons drawn on a stand-alone ImDrawList (geometry inside the 16x16 box, all different);
// (3) the ink rule (kAccentInk for containers, kMuted for the rest, Sound included).

#include <imgui.h>
#include <imgui_internal.h>

#include <cmath>
#include <cstdio>
#include <vector>

#include "library_icons.h"
#include "ui_theme.h"

using namespace rav;

namespace {

int g_fail = 0;
#define CHECK(c)                                                     \
    do {                                                             \
        if (!(c)) {                                                  \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            ++g_fail;                                                \
        }                                                            \
    } while (0)

float Lin(int c8)
{
    const float c = static_cast<float>(c8) / 255.0f;
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float Luminance(ImU32 c)
{
    const int r = (c >> IM_COL32_R_SHIFT) & 0xFF, g = (c >> IM_COL32_G_SHIFT) & 0xFF, b = (c >> IM_COL32_B_SHIFT) & 0xFF;
    return 0.2126f * Lin(r) + 0.7152f * Lin(g) + 0.0722f * Lin(b);
}

float Contrast(ImU32 a, ImU32 b)
{
    const float la = Luminance(a), lb = Luminance(b);
    const float hi = la > lb ? la : lb, lo = la > lb ? lb : la;
    return (hi + 0.05f) / (lo + 0.05f);
}

struct Shape {
    std::vector<ImVec2> pts;
    std::vector<ImDrawIdx> idx;
};

}  // namespace

int main()
{
    // 1. Contrast.
    const float rest = Contrast(ui::kWhite, ui::kAccent), hover = Contrast(ui::kWhite, ui::kPrimaryHover);
    std::printf("white on accent %.2f, on primary-hover %.2f\n", rest, hover);
    CHECK(rest >= 4.5f);
    CHECK(hover >= 4.5f);
    CHECK(std::fabs(rest - 5.02f) < 0.02f);
    CHECK(std::fabs(hover - 6.01f) < 0.02f);
    CHECK(ui::kAccent == IM_COL32(0x35, 0x68, 0xDE, 0xFF));
    CHECK(ui::kPrimaryHover == IM_COL32(0x2A, 0x5B, 0xCF, 0xFF));
    CHECK(ui::kAccentInk == IM_COL32(0xA8, 0xC2, 0xFF, 0xFF));
    // soft / line / faint follow the accent (same alphas)
    CHECK((ui::kAccentSoft & 0x00FFFFFF) == (ui::kAccent & 0x00FFFFFF) && (ui::kAccentSoft >> 24) == 0x29);
    CHECK((ui::kAccentLine & 0x00FFFFFF) == (ui::kAccent & 0x00FFFFFF) && (ui::kAccentLine >> 24) == 0x8C);
    CHECK((ui::kAccentHoverSoft & 0x00FFFFFF) == (ui::kAccent & 0x00FFFFFF) && (ui::kAccentHoverSoft >> 24) == 0x3D);
    CHECK((ui::kAccentFaint & 0x00FFFFFF) == (ui::kAccent & 0x00FFFFFF) && (ui::kAccentFaint >> 24) == 0x1A);

    // 2. The 8 icons on a stand-alone draw list (no context, no AA fringe: strict bounds).
    ImDrawListSharedData sd;
    sd.InitialFlags = ImDrawListFlags_None;
    sd.SetCircleTessellationMaxError(0.3f);
    const int glyphs[8] = {kGlyphSequence, kGlyphWorkUnit, kGlyphVirtualFolder, kGlyphActorMixer,
                           kGlyphSoundVoice, kGlyphEvent, kGlyphSoundBank, kGlyphAudioBus};
    std::vector<Shape> shapes;
    const ImVec2 origin(100.0f, 50.0f);
    for (int g : glyphs) {
        ImDrawList dl(&sd);
        dl._ResetForNewFrame();
        dl.PushTexture(ImTextureRef());
        DrawLibraryGlyph(&dl, origin, 16.0f, g, IM_COL32_WHITE);
        CHECK(dl.VtxBuffer.Size > 0 && dl.IdxBuffer.Size > 0);
        Shape s;
        const float eps = 0.01f;
        for (int i = 0; i < dl.VtxBuffer.Size; ++i) {
            const ImVec2 q = dl.VtxBuffer[i].pos;
            s.pts.push_back(ImVec2(q.x - origin.x, q.y - origin.y));
            CHECK(q.x >= origin.x - eps && q.x <= origin.x + 16.0f + eps);
            CHECK(q.y >= origin.y - eps && q.y <= origin.y + 16.0f + eps);
        }
        for (int i = 0; i < dl.IdxBuffer.Size; ++i) s.idx.push_back(dl.IdxBuffer[i]);
        shapes.push_back(s);
        dl._ClearFreeMemory();
    }
    for (size_t a = 0; a < shapes.size(); ++a)
        for (size_t b = a + 1; b < shapes.size(); ++b) {
            bool same = shapes[a].pts.size() == shapes[b].pts.size() && shapes[a].idx == shapes[b].idx;
            if (same)
                for (size_t i = 0; i < shapes[a].pts.size() && same; ++i)
                    same = shapes[a].pts[i].x == shapes[b].pts[i].x && shapes[a].pts[i].y == shapes[b].pts[i].y;
            CHECK(!same);
        }

    // The enum keeps RAV's numbers (kGlyphLink is 12 there); the Wwise icons follow them.
    CHECK(kGlyphSound == 0 && kGlyphSpeaker == 6 && kGlyphEvMissing == 11 && kGlyphLink == 12);
    CHECK(kGlyphSequence == 13 && kGlyphAudioBus == 20);

    // 3. Ink.
    for (int g : {kGlyphRandom, kGlyphBlend, kGlyphSwitch, kGlyphSequence}) CHECK(LibraryIconInk(g).col == ui::kAccentInk);
    for (int g : {kGlyphSound, kGlyphGroup, kGlyphValue, kGlyphSpeaker, kGlyphWorkUnit, kGlyphVirtualFolder, kGlyphActorMixer,
                  kGlyphSoundVoice, kGlyphEvent, kGlyphSoundBank, kGlyphAudioBus})
        CHECK(LibraryIconInk(g).col == ui::kMuted);
    CHECK(TypeIconInk(SoundNodeType::Sound).col == LibraryIconInk(kGlyphSound).col);
    CHECK(TypeIconInk(SoundNodeType::Random).col == LibraryIconInk(kGlyphRandom).col);

    if (g_fail == 0) std::printf("ui_contrast_icons: OK\n");
    return g_fail == 0 ? 0 : 1;
}
