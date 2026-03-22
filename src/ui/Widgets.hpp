#pragma once

#include <algorithm>
#include <string>
#include <functional>
#include <imgui.h>
#include "../PixelPaintView.hpp"

namespace pelpaint::ui {

// Snap helpers
inline int SnapToStep(int value, int min, int step) noexcept {
    if (step <= 0) return value;
    const int offset = value - min;
    return min + (offset / step) * step;
}

inline float SnapToStep(float value, float min, float step) noexcept {
    if (step <= 0.0f) return value;
    const float offset = value - min;
    return min + std::floor(offset / step) * step;
}

// Small button pair: useful for +/- steppers or custom controls.
inline bool ButtonPair(const char* minusLabel, const char* plusLabel, bool& minusPressed, bool& plusPressed) {
    minusPressed = false;
    plusPressed = false;

    bool changed = false;

    if (ImGui::Button(minusLabel)) {
        minusPressed = true;
        changed = true;
    }

    ImGui::SameLine();

    if (ImGui::Button(plusLabel)) {
        plusPressed = true;
        changed = true;
    }

    return changed;
}

// Integer slider with step buttons.
inline bool SliderIntStep(const char* label,
                          int min,
                          int max,
                          int step,
                          int* value)
{
    if (!value) return false;
    if (step <= 0) step = 1;

    bool changed = false;

    ImGui::PushID(label);

    ImGui::TextUnformatted(label);
    ImGui::SameLine();

    if (ImGui::Button("-")) {
        int newValue = std::max(min, *value - step);
        if (newValue != *value) {
            *value = newValue;
            changed = true;
        }
    }

    ImGui::SameLine();

    if (ImGui::SliderInt("##slider", value, min, max)) {
        int snapped = SnapToStep(*value, min, step);
        snapped = std::clamp(snapped, min, max);
        if (snapped != *value) {
            *value = snapped;
        }
        changed = true;
    }

    ImGui::SameLine();

    if (ImGui::Button("+")) {
        int newValue = std::min(max, *value + step);
        if (newValue != *value) {
            *value = newValue;
            changed = true;
        }
    }

    ImGui::PopID();
    return changed;
}

// Float slider with step buttons.
inline bool SliderFloatStep(const char* label,
                            float min,
                            float max,
                            float step,
                            float* value)
{
    if (!value) return false;
    if (step <= 0.0f) step = 1.0f;

    bool changed = false;

    ImGui::PushID(label);

    ImGui::TextUnformatted(label);
    ImGui::SameLine();

    if (ImGui::Button("-")) {
        float newValue = std::max(min, *value - step);
        if (newValue != *value) {
            *value = newValue;
            changed = true;
        }
    }

    ImGui::SameLine();

    if (ImGui::SliderFloat("##slider", value, min, max)) {
        float snapped = SnapToStep(*value, min, step);
        snapped = std::clamp(snapped, min, max);
        if (snapped != *value) {
            *value = snapped;
        }
        changed = true;
    }

    ImGui::SameLine();

    if (ImGui::Button("+")) {
        float newValue = std::min(max, *value + step);
        if (newValue != *value) {
            *value = newValue;
            changed = true;
        }
    }

    ImGui::PopID();
    return changed;
}

inline bool LayerPanel(std::vector<Layer>& layers,
                       int& activeLayerIndex,
                       int& nextLayerId,
                       const std::function<void(const std::string&)>& addLayer,
                       const std::function<void(int)>& removeLayer,
                       const std::function<void(int, int)>& reorderLayers,
                       const std::function<void()>& renderLayers)
{
    bool changed = false;

    if (!ImGui::BeginChild("LayerPanel", ImVec2(0, 0), true)) {
        ImGui::EndChild();
        return false;
    }

    ImGui::Text("Layer Stack (%zu total)", layers.size());
    ImGui::Separator();
    ImGui::Spacing();

    // Layer management buttons
    if (ImGui::Button("Add Layer##new", ImVec2(-1, 0))) {
        if (addLayer) {
            addLayer("");
        } else {
            int newZIndex = nextLayerId++;
            Layer newLayer("Layer_" + std::to_string(newZIndex), 1, 1, newZIndex);
            layers.push_back(newLayer);
            activeLayerIndex = static_cast<int>(layers.size()) - 1;
        }
        if (renderLayers) renderLayers();
        changed = true;
    }
    ImGui::Spacing();

    // Layer stack visualization (rendered from bottom to top, matching visual order)
    ImGui::TextUnformatted("Layers (Bottom to Top):");
    ImGui::Separator();

    if (ImGui::BeginListBox("##LayerStack", ImVec2(-1, 200))) {
        for (int i = 0; i < static_cast<int>(layers.size()); ++i) {
            Layer& layer = layers[i];
            bool isSelected = (i == activeLayerIndex);

            // Build layer label with visibility indicator and z-index
            std::string layerLabel =
                (layer.visible ? "[V] " : "[ ] ") +
                layer.name +
                " (z:" + std::to_string(layer.zIndex) + ")";

            ImGui::PushID(i);

            // Selectable layer entry
            if (ImGui::Selectable(layerLabel.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
                activeLayerIndex = i;
                if (renderLayers) renderLayers();
                changed = true;
            }

            // Context menu for layer operations
            if (ImGui::BeginPopupContextItem(("LayerContextMenu##" + std::to_string(i)).c_str())) {
                if (ImGui::MenuItem("Visibility", nullptr, &layer.visible)) {
                    if (renderLayers) renderLayers();
                    changed = true;
                }
                if (ImGui::MenuItem("Lock", nullptr, &layer.locked)) {
                    changed = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Duplicate##layer")) {
                    Layer newLayer = layer;
                    newLayer.name += " copy";
                    newLayer.zIndex = nextLayerId++;
                    layers.push_back(newLayer);
                    activeLayerIndex = static_cast<int>(layers.size()) - 1;
                    if (renderLayers) renderLayers();
                    changed = true;
                }
                if (ImGui::MenuItem("Delete##layer") && layers.size() > 1) {
                    if (removeLayer) {
                        removeLayer(i);
                    } else {
                        layers.erase(layers.begin() + i);
                        if (activeLayerIndex >= static_cast<int>(layers.size())) {
                            activeLayerIndex = static_cast<int>(layers.size()) - 1;
                        }
                    }
                    if (renderLayers) renderLayers();
                    changed = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Merge Down##layer") && i > 0) {
                    // TODO: Implement layer merging
                }
                ImGui::EndPopup();
            }

            // Drag to reorder (simple approach - show up/down buttons)
            ImGui::SameLine();
            ImGui::PushItemWidth(60);
            if (i > 0 && ImGui::Button("^##up", ImVec2(25, 0))) {
                if (reorderLayers) {
                    reorderLayers(i, i - 1);
                } else {
                    std::swap(layers[i], layers[i - 1]);
                    activeLayerIndex = i - 1;
                }
                if (renderLayers) renderLayers();
                changed = true;
            }
            ImGui::SameLine();
            if (i < static_cast<int>(layers.size()) - 1 && ImGui::Button("v##down", ImVec2(25, 0))) {
                if (reorderLayers) {
                    reorderLayers(i, i + 1);
                } else {
                    std::swap(layers[i], layers[i + 1]);
                    activeLayerIndex = i + 1;
                }
                if (renderLayers) renderLayers();
                changed = true;
            }
            ImGui::PopItemWidth();
            ImGui::PopID();
        }
        ImGui::EndListBox();
    }

    ImGui::Spacing();
    ImGui::Separator();

    // Active layer properties panel
    Layer* activeLayer = nullptr;
    if (activeLayerIndex >= 0 && activeLayerIndex < static_cast<int>(layers.size())) {
        activeLayer = &layers[activeLayerIndex];
    }

    if (activeLayer) {
        ImGui::Text("Active Layer: %s", activeLayer->name.c_str());
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Layer Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
            // Opacity slider
            if (ImGui::SliderFloat("Opacity", &activeLayer->opacity, 0.0f, 1.0f, "%.2f")) {
                if (renderLayers) renderLayers();
                changed = true;
            }

            // Blend mode selector (basic blend modes)
            const char* blendModes[] = { "Normal", "Multiply", "Screen", "Overlay" };
            if (ImGui::Combo("Blend Mode", &activeLayer->blendMode, blendModes, IM_ARRAYSIZE(blendModes))) {
                if (renderLayers) renderLayers();
                changed = true;
            }

            // Tint color
            ImVec4 tintColor(activeLayer->blendColor.x, activeLayer->blendColor.y,
                            activeLayer->blendColor.z, activeLayer->blendColor.w);
            if (ImGui::ColorEdit4("Tint Color", &tintColor.x)) {
                activeLayer->blendColor = tintColor;
                if (renderLayers) renderLayers();
                changed = true;
            }

            // Lock toggle
            if (ImGui::Checkbox("Lock Layer", &activeLayer->locked)) {
                changed = true;
            }
        }
    }

    ImGui::EndChild();
    return changed;
}

} // namespace pelpaint::ui
