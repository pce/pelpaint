/// @file PaletteEditorDialog.hpp
/// @brief Modal ImGui dialog for editing a colour palette inline.
#pragma once

#include <string>
#include <vector>
#include "../ColorPalettes.hpp"
#include "../core/PaletteRef.hpp"
#include "../core/PaletteLibrary.hpp"
#include <imgui.h>

namespace pelpaint::ui {

/// Modal dialog for editing a colour palette inline.
///
/// Open with Open(), then call Draw() every frame.
/// On OK/Save the caller receives an updated PaletteRef via the return value of Draw().
class PaletteEditorDialog {
public:
    /// Open the dialog pre-filled from @p ref.
    ///
    /// - Named  source: working copy is a clone of library.All()[ref.namedIndex].colors.
    /// - Custom source: working copy is ref.colors.
    /// - Auto / None:   working copy starts empty.
    void Open(const PaletteRef& ref, const PaletteLibrary& library);

    /// Render the modal.
    ///
    /// Returns true and updates @p out_ref when the user saves/confirms.
    /// Call every frame regardless of IsOpen(); internally does nothing when closed.
    bool Draw(PaletteRef& out_ref, PaletteLibrary& library);

    [[nodiscard]] bool IsOpen() const noexcept { return open_; }

private:
    bool               open_          = false;
    bool               pendingOpen_   = false;
    /// Set by DrawButtons() when the user confirms a save; read and cleared by Draw().
    bool               savedFlag_     = false;
    std::string        editName_;
    std::vector<Pixel> editColors_;
    int                selectedIdx_   = -1;       ///< Index of the selected swatch, or -1.
    float              editColor_[4]  = {};        ///< RGBA floats in [0,1] for the colour picker.
    bool               confirmSaveAs_ = false;
    char               saveAsName_[128] = {};

    /// Render the grid of colour swatches.
    void DrawSwatchGrid();

    /// Render the colour picker plus Update / Add Color buttons.
    void DrawColorEditor();

    /// Render the Save / Save As… / Cancel button row.
    /// Sets savedFlag_ and updates out_ref when the user confirms.
    void DrawButtons(PaletteRef& out_ref, PaletteLibrary& library);
};

} // namespace pelpaint::ui
