// SPDX-License-Identifier: MIT
//
// Headless test of the `theme` target: a Dear ImGui context with NO backend (no window, no GL, no
// platform layer), the theme applied, then frames that draw every widget and every icon. It passes
// when the frames run to the end without an assertion (ImGui asserts on a misuse: unbalanced
// Push/Pop, bad ID stack...) and when the draw lists carry geometry.
//
// Dear ImGui 1.92 needs a texture-capable renderer to build its atlas; this test plays that part
// by flagging the (absent) backend as RendererHasTextures and answering the atlas' texture
// requests itself with CPU memory.

#include <imgui.h>

#include <cstdio>
#include <cstring>
#include <vector>

#include "fold_scope_buttons.h"
#include "library_icons.h"
#include "sound_node_type.h"
#include "ui_flow.h"
#include "ui_flow_math.h"
#include "ui_fold_all.h"
#include "ui_theme.h"

using namespace rav;

namespace {

int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);     \
            ++g_fail;                                                    \
        }                                                                \
    } while (0)

// The part of a backend that 1.92 asks of one: give each texture of the atlas a (fake) id.
void ServiceTextures(ImDrawData* dd)
{
    if (!dd->Textures) return;
    for (ImTextureData* tex : *dd->Textures) {
        if (tex->Status == ImTextureStatus_WantCreate) {
            tex->SetTexID(static_cast<ImTextureID>(1));
            tex->SetStatus(ImTextureStatus_OK);
        } else if (tex->Status == ImTextureStatus_WantUpdates) {
            tex->SetStatus(ImTextureStatus_OK);
        } else if (tex->Status == ImTextureStatus_WantDestroy) {
            tex->SetStatus(ImTextureStatus_Destroyed);
        }
    }
}

ImFont* AddRoleFont(ImGuiIO& io, float px)
{
    ImFontConfig cfg;
    cfg.SizePixels = px;
    return io.Fonts->AddFontDefault(&cfg);
}

void DrawIcons(ImDrawList* dl)
{
    ImVec2 p = ImGui::GetCursorScreenPos();
    const float s = ui::IconPx();
    // Every Library icon (LibraryGlyph, kGlyphAudioBus last), every tool icon (ToolGlyph), the four node types.
    for (int g = kGlyphSound; g <= kGlyphAudioBus; ++g) {
        DrawLibraryGlyph(dl, p, s, g, ui::kMuted);
        p.x += s + 4.0f;
    }
    DrawEventGlyph(dl, ImVec2(p.x + 5.0f, p.y + 5.0f), 9.0f, kGlyphEvAttached);
    p.x += 14.0f;
    DrawLinkGlyph(dl, p, s, ui::kAccentInk);
    p.x += s + 4.0f;
    for (int g = kToolCross; g <= kToolOpenArrow; ++g) {
        DrawToolGlyph(dl, p, s, g, ui::kText);
        DrawToolGlyphAt(dl, ImVec2(p.x + s * 0.5f, p.y + s * 2.0f), 4.0f, g, ui::kAccent);
        p.x += s + 4.0f;
    }
    const SoundNodeType types[] = {SoundNodeType::Sound, SoundNodeType::Random, SoundNodeType::Blend,
                                   SoundNodeType::Switch};
    for (SoundNodeType t : types) {
        DrawNodeTypeGlyph(dl, p, s, t, ui::kAccentInk);
        CHECK(GlyphOfType(t) >= kGlyphSound && GlyphOfType(t) <= kGlyphSwitch);
        const ui::Icon a = TypeIcon(t), b = TypeIconInk(t);
        CHECK(a.fn != nullptr && b.fn != nullptr);
        p.x += s + 4.0f;
    }
    ImGui::Dummy(ImVec2(p.x - ImGui::GetCursorScreenPos().x, s * 3.0f));
}

void DrawWidgets(int frame)
{
    static float  fader = 0.5f;
    static double num = 1.5;
    static char   text[64] = "Footsteps";
    static char   hint[64] = "";
    static bool   on = true, sw = false, chk = false;
    static int    seg = 0, tab = 0, tri = 1, combo_pick = 0;
    static float  rgb[3] = {0.2f, 0.5f, 0.9f};

    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2(480, 900));
    ImGui::Begin("headless", nullptr, ImGuiWindowFlags_NoSavedSettings);

    // Text and tags.
    ui::Caption("Caption");
    ui::GroupCaption("Group");
    ui::SubText("Sub text");
    ui::ToneText("Tone text", ui::kMuted, true, 200.0f);
    ui::Tag("proj", ui::TagTone::Project);
    ImGui::SameLine();
    ui::Tag("bad", ui::TagTone::Bad, ui::kBad);
    ImGui::SameLine();
    ui::SmallTag("small");
    ImGui::SameLine();
    {
        ImDrawList* tdl = ImGui::GetWindowDrawList();
        const ImVec2 tp = ImGui::GetCursorScreenPos();
        const float  tw = ui::DrawSmallTagTone(tdl, tp, "both", ui::kBoth, ui::kBothLine, ui::kBothSoft);
        CHECK(tw > 0.0f);
        ImGui::Dummy(ImVec2(tw, ui::SmallTagHeight()));
    }
    ImGui::SameLine();
    ui::CountChip("12");
    ui::StateTag(ui::kOk, "state");
    CHECK(ui::TagWidth("proj") > 0.0f);

    // Buttons.
    ui::PrimaryButton("Primary");
    ImGui::SameLine();
    ui::SolidButton("Solid");
    ImGui::SameLine();
    ui::ToggleButton("Toggle", on);
    ImGui::SameLine();
    ui::ChipButton("Chip", ImVec2(0, 0), true);
    ImGui::SameLine();
    ui::SplitChip("Split chip", ToolIcon(kToolPlus), true, frame % 2 == 0, true, frame % 2 == 1);
    ImGui::SameLine();
    CHECK(ui::SplitChipWidth("Split chip") > 0.0f && ui::SplitChipWidth("Split chip", true, 40.0f) > 0.0f);
    ui::LinkButton("Link");
    ImGui::SameLine();
    ui::HelpMark("help", "A help mark");
    ui::AddButton("Add", ImVec2(0, 0), "A");
    ImGui::SameLine();
    ui::ConfirmButton("Confirm");
    ImGui::SameLine();
    ui::ToneButton("Tone", ui::TagTone::Warn);
    ui::FittedButton("Fitted");
    ImGui::SameLine();
    ui::IconOnlyButton("##ion", ToolIcon(kToolGear), "Gear");
    ImGui::SameLine();
    ui::IconButton(LibraryIcon(kGlyphSpeaker), "Speaker");
    ui::MenuButton("Menu");
    ImGui::Button("Plain");

    // Choice widgets.
    const char* labels[] = {"One", "Two", "Three"};
    const ui::Icon icons[] = {TypeIcon(SoundNodeType::Sound), TypeIcon(SoundNodeType::Random),
                              TypeIcon(SoundNodeType::Blend)};
    ui::Segmented("seg", labels, 3, &seg, 0.0f, -1, icons);
    ui::Switch("sw", sw);
    ui::Checkbox("Check", &chk);
    ui::TriCheckbox("tri", tri);
    ui::VerticalTabs("vt", labels, 3, &tab);

    // Numbers and fields.
    ui::DragNumber("num", &num, 0.1, 2, "dB");
    ui::Fader("fader", &fader, 0.0f, 1.0f, 200.0f);
    ui::TextField("tf", "hint", text, sizeof text);
    ui::TextField("tf2", "empty hint", hint, sizeof hint);
    ui::AddField("af", "add", hint, sizeof hint);
    ui::TitleField("title", text, sizeof text);
    ui::RenameField("ren", text, sizeof text);
    ui::ColorSwatchButton("col", rgb, "Colour");
    const ImU32 pal[] = {ui::kAccent, ui::kOk, ui::kWarn, ui::kBad};
    int picked = -1;
    ui::SwatchPalette("pal", pal, 4, ui::kOk, &picked);

    // Combos.
    if (ui::BeginComboStyled("combo", labels[combo_pick])) {
        ImGui::Selectable("One");
        ui::EndComboStyled();
    }
    if (ui::BeginChipCombo("chipcombo", "chip")) {
        ImGui::Selectable("x");
        ui::EndChipCombo();
    }
    ui::SelectField("sel", "Label", "value");

    // Rows, chevron, labels.
    ui::SelectRow("A row", frame % 2 == 0);
    ui::Chevron("chev", true);
    ImGui::SameLine();
    ui::Chevron("chev2", false);
    ui::LabelRow("Label", 100.0f);
    ImGui::TextUnformatted("beside");
    ui::FitText("Some text that may be cut when the window is narrow");
    ui::ClampedText("A hint long enough to wrap over two lines\nand then be cut on the last one, in a narrow "
                    "window or a wide one, whatever its width may be today",
                    ui::kMuted, 2);
    ui::ClampedText("Short", ui::kMuted, 2, true);
    CHECK(ui::TextWidth("abc") > 0.0f && ui::ButtonWidth("abc") > 0.0f);

    // Containers: card, well, toolbar, confirm band, pane, section, sentence.
    ui::Card card;
    ui::BeginCard(card);
    ImGui::TextUnformatted("In a card");
    ui::EndCard(card, ui::kSurface, ui::kStroke);

    ui::Well well;
    ui::BeginWell(well);
    ImGui::TextUnformatted("In a well");
    ui::EndWell(well);
    ui::BeginToolbar(well);
    ImGui::Button("tb");
    ui::EndToolbar(well);

    ui::ConfirmBand band;
    ui::BeginConfirmBand(band);
    ImGui::TextUnformatted("Sure?");
    ui::EndConfirmBand(band);

    if (ui::BeginPane("pane", ImVec2(200, 60))) {
        ImGui::TextUnformatted("pane");
    }
    ui::EndPane();
    if (ui::BeginListPane("lpane", ImVec2(200, 50))) {
        ImGui::TextUnformatted("list pane");
    }
    ui::EndListPane();

    ui::BeginFoldScope("scope");
    ui::FoldScopeBar("scope");
    ui::Section sec;
    if (ui::BeginSection(sec, "Section")) {
        ImGui::TextUnformatted("body");
        const char* sl[] = {"A", "B"};
        ui::Sentence s = ui::MakeSentence(ImGui::GetCursorScreenPos().x, ImGui::GetCursorScreenPos().x + 300.0f,
                                          ui::SentenceColumnWidth(sl, 2));
        ui::SentenceRow(s, "A");
        ui::SentenceChip(s, 40.0f);
        ImGui::Button("chip");
        ImGui::NewLine();
    }
    ui::EndSection(sec);
    ui::EndFoldScope();

    // Tooltips, splitter, keycap, ellipsis, vertical text, fold triangle, dashed rect.
    ImGui::Button("hover me");
    ui::ItemTipText("tip");
    ui::SplitterBar("split", ImVec2(4, 20));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ui::KeyCap(dl, p, "Ctrl");
    ui::EllipsisText(dl, ImVec2(p.x, p.y + 20), p.x + 60.0f, ui::kText, "A very long text indeed");
    ui::DrawFoldTriangle(dl, ImVec2(p.x + 100, p.y + 10), true, ui::kMuted);
    ui::DashedRect(dl, ImVec2(p.x + 120, p.y), ImVec2(p.x + 160, p.y + 20), ui::kStrokeStrong);
    ui::RowBackground(dl, ImVec2(p.x + 170, p.y), ImVec2(p.x + 260, p.y + 20), ui::kRowSelected);
    ImGui::Dummy(ImVec2(300, 44));
    CHECK(!ui::EllipsizeToWidth(nullptr, "A very long text indeed", 60.0f).empty());

    // Menu popup (opened on the first frame).
    if (frame == 0) ImGui::OpenPopup("menu");
    if (ui::BeginMenuPopup("menu")) {
        ui::MenuCaption("Caption");
        ui::MenuEntry("Entry", "Ctrl+E");
        ui::IconMenuItem(ToolIcon(kToolPlus), "With icon");
        if (ui::BeginSubMenu("Sub")) {
            ui::MenuEntry("Inner");
            ImGui::EndMenu();
        }
        ui::MenuSeparator();
        ui::EndMenuPopup();
    }

    // Confirm bubble over a button (opened on the third frame, after the menu popup closed).
    ui::SolidButton("Delete");
    const ImVec2 anchor(ImGui::GetItemRectMax().x, ImGui::GetItemRectMin().y);
    if (frame == 2) ImGui::OpenPopup("bubble");
    if (ui::BeginConfirmBubble("bubble", anchor)) {
        ImGui::TextUnformatted("Delete it?");
        ui::EndConfirmBubble();
    }

    // Icons.
    DrawIcons(dl);

    ImGui::End();

    // Floating pieces outside the window.
    if (ui::BeginOverlay("overlay", ImVec2(500, 10))) {
        ImGui::TextUnformatted("overlay");
        ui::EndOverlay();
    }
    if (ui::BeginToast("toast", ImVec2(300, 880))) {
        ui::ToastNote("toast");
        ui::EndToast();
    }
}

}  // namespace

int main()
{
    // ---- pure helpers (no context needed) ----
    {
        CHECK(!FoldScopeButtonsFor(1, 0).shown);
        const FoldScopeButtons b = FoldScopeButtonsFor(3, 3);
        CHECK(b.shown && !b.can_expand && b.can_collapse);
        CHECK(FoldScopeButtonsFor(3, 0).can_expand && !FoldScopeButtonsFor(3, 0).can_collapse);
    }

    IMGUI_CHECKVERSION();
    ImGuiContext* ctx = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(1280, 960);
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;

    // One font per role, each at its mock-up size, as a host registers them.
    ui::FontSet fs;
    fs.body = AddRoleFont(io, 12);
    fs.small = AddRoleFont(io, 11);
    fs.title = AddRoleFont(io, 13);
    fs.big = AddRoleFont(io, 15);
    fs.mono = AddRoleFont(io, 12);
    fs.tiny = AddRoleFont(io, 10);
    fs.badge = AddRoleFont(io, 9);
    fs.semibold = AddRoleFont(io, 12);
    fs.caption_bold = AddRoleFont(io, 11);
    fs.heading = AddRoleFont(io, 14);
    fs.pane_title = AddRoleFont(io, 15);
    fs.mono_small = AddRoleFont(io, 11);
    fs.mono_tiny = AddRoleFont(io, 10);
    fs.mono_micro = AddRoleFont(io, 9);
    ui::SetFonts(fs);

    ui::ApplyRavTheme();
    CHECK(ui::FontBody() == fs.body && ui::FontTitle() == fs.title && ui::FontMono() == fs.mono);
    // Metrics follow the size each role was registered at (ImFont::LegacySize in 1.92).
    CHECK(fs.big->LegacySize == 15.0f && fs.badge->LegacySize == 9.0f);
    CHECK(ui::TextSizeIn(fs.big, "Xg").y > ui::TextSizeIn(fs.badge, "Xg").y);
    CHECK(ui::TextSizeIn(fs.title, "Xg").x > ui::TextSizeIn(fs.small, "Xg").x);
    CHECK(ui::TextSizeIn(nullptr, "Xg").y > 0.0f);  // a null font answers the body font
    CHECK(ui::TagHeight() > 0.0f);

    std::size_t last_vtx = 0;
    for (int frame = 0; frame < 4; ++frame) {
        io.DeltaTime = 1.0f / 60.0f;
        io.MousePos = ImVec2(40, 40);  // over a widget: hover states and tooltips
        ImGui::NewFrame();
        DrawWidgets(frame);
        ImGui::Render();
        ImDrawData* dd = ImGui::GetDrawData();
        ServiceTextures(dd);
        last_vtx = static_cast<std::size_t>(dd->TotalVtxCount);
    }
    CHECK(last_vtx > 1000);  // the frame really drew something

    // A second pass at another scale and without the role fonts (the fallback to the default font).
    ui::SetUiScale(1.5f);
    CHECK(ui::Dp(10.0f) == 15.0f);
    ui::SetFonts(ui::FontSet());
    for (int frame = 0; frame < 2; ++frame) {
        ImGui::NewFrame();
        DrawWidgets(frame + 1);
        ImGui::Render();
        ServiceTextures(ImGui::GetDrawData());
    }
    CHECK(ImGui::GetDrawData()->TotalVtxCount > 1000);

    ui::SetUiScale(1.0f);
    ImGui::DestroyContext(ctx);
    if (g_fail == 0) std::printf("theme_headless_test: OK (%zu vertices in the last styled frame)\n", last_vtx);
    return g_fail == 0 ? 0 : 1;
}
