/// @file PaletteEditorDialog.cpp
/// @brief Implements PaletteEditorDialog — an inline modal palette editor.
#include "PaletteEditorDialog.hpp"

#include <algorithm>
#include <cstring>
#include <imgui.h>

namespace pelpaint::ui {

// ── Layout constants ──────────────────────────────────────────────────────────

namespace {

/// Edge length (px) of each swatch colour button.
constexpr float kSwatchSize = 28.0f;

/// Number of swatch buttons per row.
constexpr int kSwatchPerRow = 8;

/// Horizontal gap (px) between adjacent swatches.
constexpr float kSwatchGap = 2.0f;

/// Width (px) of the swatch panel child window.
/// Must accommodate (kSwatchPerRow * kSwatchSize) + ((kSwatchPerRow-1) * kSwatchGap)
/// plus child padding and a possible vertical scrollbar.
constexpr float kSwatchPanelW = 252.0f;

/// Height (px) shared by the swatch panel and colour-editor panel.
/// Sized to leave room above (name row + separators) and below (button row).
constexpr float kPanelH = 385.0f;

} // namespace

// ── Pixel ↔ float conversion helpers ─────────────────────────────────────────

/// Convert a Pixel (RGBA uint8) to an ImVec4 (RGBA float in [0, 1]).
[[nodiscard]] static ImVec4 ToImVec4(const Pixel& p) noexcept
{
    return { p.r / 255.0f, p.g / 255.0f, p.b / 255.0f, p.a / 255.0f };
}

/// Convert a float[4] RGBA array (values in [0, 1]) to a Pixel.
[[nodiscard]] static Pixel ToPixel(const float c[4]) noexcept
{
    return {
        static_cast<uint8_t>(std::clamp(c[0], 0.0f, 1.0f) * 255.0f + 0.5f),
        static_cast<uint8_t>(std::clamp(c[1], 0.0f, 1.0f) * 255.0f + 0.5f),
        static_cast<uint8_t>(std::clamp(c[2], 0.0f, 1.0f) * 255.0f + 0.5f),
        static_cast<uint8_t>(std::clamp(c[3], 0.0f, 1.0f) * 255.0f + 0.5f),
    };
}

// ── Open ──────────────────────────────────────────────────────────────────────

void PaletteEditorDialog::Open(const PaletteRef& ref, const PaletteLibrary& library)
{
    // Queue the OpenPopup call for the next Draw() to fire inside an active frame.
    pendingOpen_   = true;
    open_          = false;
    savedFlag_     = false;
    selectedIdx_   = -1;
    confirmSaveAs_ = false;
    std::memset(saveAsName_, 0, sizeof(saveAsName_));

    // Default picker to opaque black.
    editColor_[0] = editColor_[1] = editColor_[2] = 0.0f;
    editColor_[3] = 1.0f;

    const auto& all = library.All();

    switch (ref.source) {
    case PaletteSource::Named:
        if (ref.namedIndex >= 0 &&
            ref.namedIndex < static_cast<int>(all.size()))
        {
            const ColorPalette& pal = all[ref.namedIndex];
            editColors_ = pal.colors;
            editName_   = pal.name;
        } else {
            editColors_.clear();
            editName_.clear();
        }
        break;

    case PaletteSource::Custom:
        editColors_ = ref.colors;
        editName_   = "Custom";
        break;

    case PaletteSource::Auto:
    case PaletteSource::None:
    default:
        editColors_.clear();
        editName_.clear();
        break;
    }

    // Pre-fill the Save As name field with whatever name we settled on.
    std::strncpy(saveAsName_, editName_.c_str(), sizeof(saveAsName_) - 1);
}

// ── DrawSwatchGrid ────────────────────────────────────────────────────────────

void PaletteEditorDialog::DrawSwatchGrid()
{
    const int n = static_cast<int>(editColors_.size());

    if (n == 0) {
        ImGui::TextDisabled("(empty — use Add Color below)");
        return;
    }

    for (int i = 0; i < n; ++i) {
        const ImVec4 col      = ToImVec4(editColors_[i]);
        const bool   selected = (i == selectedIdx_);

        // Continue the same row unless this is the first item of a new row.
        if (i % kSwatchPerRow != 0)
            ImGui::SameLine(0.0f, kSwatchGap);

        ImGui::PushID(i);

        // Selected swatch gets a bright white border.
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 1.0f));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 2.0f);
        }

        if (ImGui::ColorButton(
                "##sw", col,
                ImGuiColorEditFlags_NoPicker |
                ImGuiColorEditFlags_NoTooltip |
                ImGuiColorEditFlags_AlphaPreview,
                ImVec2(kSwatchSize, kSwatchSize)))
        {
            selectedIdx_  = i;
            editColor_[0] = col.x;
            editColor_[1] = col.y;
            editColor_[2] = col.z;
            editColor_[3] = col.w;
        }

        if (selected) {
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }

        // Right-click context menu for this swatch.
        if (ImGui::BeginPopupContextItem("##swctx")) {
            if (ImGui::MenuItem("Delete this color")) {
                editColors_.erase(editColors_.begin() + i);
                // Keep selectedIdx_ in bounds.
                const int newN = static_cast<int>(editColors_.size());
                if (selectedIdx_ >= newN)
                    selectedIdx_ = newN - 1;
            }
            ImGui::EndPopup();
        }

        ImGui::PopID();
    }
}

// ── DrawColorEditor ───────────────────────────────────────────────────────────

void PaletteEditorDialog::DrawColorEditor()
{
    // Constrain the picker to the available width so it doesn't overflow.
    ImGui::PushItemWidth(ImGui::GetContentRegionAvail().x);
    ImGui::ColorPicker4(
        "##picker", editColor_,
        ImGuiColorEditFlags_NoSidePreview |
        ImGuiColorEditFlags_PickerHueBar);
    ImGui::PopItemWidth();

    ImGui::Spacing();

    const bool hasSelection = (selectedIdx_ >= 0 &&
                               selectedIdx_ < static_cast<int>(editColors_.size()));

    // [Update] writes the current picker colour back into the selected swatch.
    if (!hasSelection) ImGui::BeginDisabled();
    if (ImGui::Button("Update")) {
        if (hasSelection)
            editColors_[selectedIdx_] = ToPixel(editColor_);
    }
    if (!hasSelection) ImGui::EndDisabled();

    ImGui::SameLine();

    // [Add Color] appends the current picker colour as a new swatch.
    if (ImGui::Button("Add Color")) {
        editColors_.push_back(ToPixel(editColor_));
        selectedIdx_ = static_cast<int>(editColors_.size()) - 1;
    }
}

// ── DrawButtons ───────────────────────────────────────────────────────────────

void PaletteEditorDialog::DrawButtons(PaletteRef& out_ref, PaletteLibrary& library)
{
    if (!confirmSaveAs_) {
        // ── Normal button row ─────────────────────────────────────────────

        if (ImGui::Button("Save")) {
            out_ref    = PaletteRef::fromCustom(editColors_);
            savedFlag_ = true;
            open_      = false;
            ImGui::CloseCurrentPopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Save As...")) {
            confirmSaveAs_ = true;
            // Pre-fill name field with whatever is currently in editName_.
            std::strncpy(saveAsName_, editName_.c_str(), sizeof(saveAsName_) - 1);
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel")) {
            open_          = false;
            confirmSaveAs_ = false;
            ImGui::CloseCurrentPopup();
        }
    } else {
        // ── Inline "Save As" name-entry flow ──────────────────────────────

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Name:");
        ImGui::SameLine();

        ImGui::SetNextItemWidth(180.0f);
        ImGui::InputText("##san", saveAsName_, sizeof(saveAsName_));
        ImGui::SameLine();

        const bool nameValid = (saveAsName_[0] != '\0');

        if (!nameValid) ImGui::BeginDisabled();
        if (ImGui::Button("OK##san")) {
            const std::string name(saveAsName_);
            library.Upsert(ColorPalette{ name, editColors_ });
            const int idx = library.IndexOf(name);
            out_ref = (idx >= 0)
                ? PaletteRef::fromNamed(idx)
                : PaletteRef::fromCustom(editColors_);
            savedFlag_     = true;
            open_          = false;
            confirmSaveAs_ = false;
            ImGui::CloseCurrentPopup();
        }
        if (!nameValid) ImGui::EndDisabled();

        ImGui::SameLine();

        if (ImGui::Button("Back##san")) {
            confirmSaveAs_ = false;
        }
    }
}

// ── Draw ──────────────────────────────────────────────────────────────────────

bool PaletteEditorDialog::Draw(PaletteRef& out_ref, PaletteLibrary& library)
{
    if (!open_ && !pendingOpen_) return false;

    // OpenPopup must be called from within a running frame, not from Open().
    if (pendingOpen_) {
        ImGui::OpenPopup("Edit Palette##ped");
        pendingOpen_ = false;
        open_        = true;
    }

    savedFlag_ = false;

    ImGui::SetNextWindowSize(ImVec2(500.0f, 540.0f), ImGuiCond_Appearing);

    constexpr ImGuiWindowFlags kFlags =
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::BeginPopupModal("Edit Palette##ped", nullptr, kFlags)) {

        // ── a. Palette name field ─────────────────────────────────────────
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Name:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(-1.0f);

        char nameBuf[256] = {};
        std::strncpy(nameBuf, editName_.c_str(), sizeof(nameBuf) - 1);
        if (ImGui::InputText("##palname", nameBuf, sizeof(nameBuf)))
            editName_ = nameBuf;

        ImGui::Separator();
        ImGui::Spacing();

        // ── b + c. Side-by-side: swatch panel (left) + colour editor (right) ──

        const float editorW = ImGui::GetContentRegionAvail().x
                              - kSwatchPanelW
                              - ImGui::GetStyle().ItemSpacing.x;

        // ── b. Swatch grid ────────────────────────────────────────────────
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
        const bool swatchVisible =
            ImGui::BeginChild("##swatchpanel", ImVec2(kSwatchPanelW, kPanelH),
                              ImGuiChildFlags_Borders);
        ImGui::PopStyleVar();
        if (swatchVisible)
            DrawSwatchGrid();
        ImGui::EndChild();

        ImGui::SameLine();

        // ── c. Colour editor ──────────────────────────────────────────────
        const bool editorVisible =
            ImGui::BeginChild("##editorpanel", ImVec2(editorW, kPanelH),
                              ImGuiChildFlags_None);
        if (editorVisible)
            DrawColorEditor();
        ImGui::EndChild();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── d. Action buttons ─────────────────────────────────────────────
        DrawButtons(out_ref, library);

        ImGui::EndPopup();
    } else if (open_) {
        // BeginPopupModal returned false while we believed the dialog was open
        // (e.g. dismissed externally by pressing Escape).
        open_ = false;
    }

    return savedFlag_;
}

} // namespace pelpaint::ui
