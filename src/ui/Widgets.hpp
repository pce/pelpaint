#pragma once

#include <algorithm>
#include <string>
#include <functional>
#include <map>
#include <imgui.h>
#include "../PixelPaintView.hpp"

namespace pelpaint::ui {

// ---------------------------------------------------------------------------
// Snap helpers
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Button pair helper
// ---------------------------------------------------------------------------

inline bool ButtonPair(const char* minusLabel, const char* plusLabel, bool& minusPressed, bool& plusPressed) {
    minusPressed = false;
    plusPressed  = false;
    bool changed = false;

    if (ImGui::Button(minusLabel)) { minusPressed = true; changed = true; }
    ImGui::SameLine();
    if (ImGui::Button(plusLabel))  { plusPressed  = true; changed = true; }

    return changed;
}

// ---------------------------------------------------------------------------
// Integer slider with step buttons.
// Label is rendered as small text ABOVE the control row.
// ---------------------------------------------------------------------------

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

    // Label above
    ImGui::TextUnformatted(label);

    // [ - ]  [====slider====]  [ + ]
    if (ImGui::Button("-")) {
        int newValue = std::max(min, *value - step);
        if (newValue != *value) { *value = newValue; changed = true; }
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(-38.0f); // leave room for + button
    if (ImGui::SliderInt("##v", value, min, max)) {
        int snapped = SnapToStep(*value, min, step);
        snapped = std::clamp(snapped, min, max);
        if (snapped != *value) *value = snapped;
        changed = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("+")) {
        int newValue = std::min(max, *value + step);
        if (newValue != *value) { *value = newValue; changed = true; }
    }

    ImGui::PopID();
    return changed;
}

// ---------------------------------------------------------------------------
// Float slider with step buttons.
// Label is rendered as small text ABOVE the control row.
// ---------------------------------------------------------------------------

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

    // Label above
    ImGui::TextUnformatted(label);

    // [ - ]  [====slider====]  [ + ]
    if (ImGui::Button("-")) {
        float newValue = std::max(min, *value - step);
        if (newValue != *value) { *value = newValue; changed = true; }
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(-38.0f); // leave room for + button
    if (ImGui::SliderFloat("##v", value, min, max)) {
        float snapped = SnapToStep(*value, min, step);
        snapped = std::clamp(snapped, min, max);
        if (snapped != *value) *value = snapped;
        changed = true;
    }

    ImGui::SameLine();
    if (ImGui::Button("+")) {
        float newValue = std::min(max, *value + step);
        if (newValue != *value) { *value = newValue; changed = true; }
    }

    ImGui::PopID();
    return changed;
}

// ---------------------------------------------------------------------------
// Persistent state store (keyed by unique string ID)
// ---------------------------------------------------------------------------

inline int& GetSliderIntState(const std::string& id, int initial) {
    static std::map<std::string, int> s_states;
    auto [it, inserted] = s_states.emplace(id, initial);
    return it->second;
}

inline float& GetSliderFloatState(const std::string& id, float initial) {
    static std::map<std::string, float> s_states;
    auto [it, inserted] = s_states.emplace(id, initial);
    return it->second;
}

// ---------------------------------------------------------------------------
// Stateful integer slider
// Manages its own value in a static map; syncs back via callback.
// ---------------------------------------------------------------------------

inline bool SliderIntStepStateful(const char*              label,
                                  int                      min,
                                  int                      max,
                                  int                      step,
                                  const char*              id,
                                  int                      initial,
                                  std::function<void(int)> callback = {})
{
    int& value   = GetSliderIntState(id, initial);
    bool changed = SliderIntStep(label, min, max, step, &value);
    if (changed && callback) callback(value);
    return changed;
}

// ---------------------------------------------------------------------------
// Stateful float slider
// Manages its own value in a static map; syncs back via callback.
// ---------------------------------------------------------------------------

inline bool SliderFloatStepStateful(const char*                label,
                                    float                      min,
                                    float                      max,
                                    float                      step,
                                    const char*                id,
                                    float                      initial,
                                    std::function<void(float)> callback = {})
{
    float& value  = GetSliderFloatState(id, initial);
    bool   changed = SliderFloatStep(label, min, max, step, &value);
    if (changed && callback) callback(value);
    return changed;
}

// ---------------------------------------------------------------------------
// Layer panel widget
// ---------------------------------------------------------------------------

inline bool LayerPanel(std::vector<pelpaint::Layer>& layers,
                       int& activeLayerIndex,
                       int& nextLayerId,
                       const std::function<void(const std::string&)>& addLayer,
                       const std::function<void(int)>&                removeLayer,
                       const std::function<void(int, int)>&           reorderLayers,
                       const std::function<void()>&                   renderLayers)
{
    bool changed = false;

    if (!ImGui::BeginChild("LayerPanel", ImVec2(0, 0), true)) {
        ImGui::EndChild();
        return false;
    }

    ImGui::Text("Layer Stack (%zu total)", layers.size());
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Add Layer##new", ImVec2(-1, 0))) {
        if (addLayer) {
            addLayer("");
        } else {
            int newZIndex = nextLayerId++;
            pelpaint::Layer newLayer("Layer_" + std::to_string(newZIndex), 1, 1, newZIndex);
            layers.push_back(newLayer);
            activeLayerIndex = static_cast<int>(layers.size()) - 1;
        }
        if (renderLayers) renderLayers();
        changed = true;
    }
    ImGui::Spacing();

    ImGui::TextUnformatted("Layers (Bottom to Top):");
    ImGui::Separator();

    if (ImGui::BeginListBox("##LayerStack", ImVec2(-1, 200))) {
        for (int i = 0; i < static_cast<int>(layers.size()); ++i) {
            pelpaint::Layer& layer     = layers[i];
            bool             isSelected = (i == activeLayerIndex);

            std::string layerLabel =
                (layer.visible ? "[V] " : "[ ] ") +
                layer.name +
                " (z:" + std::to_string(layer.zIndex) + ")";

            ImGui::PushID(i);

            if (ImGui::Selectable(layerLabel.c_str(), isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
                activeLayerIndex = i;
                if (renderLayers) renderLayers();
                changed = true;
            }

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
                    pelpaint::Layer newLayer = layer;
                    newLayer.name   += " copy";
                    newLayer.zIndex  = nextLayerId++;
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
                        if (activeLayerIndex >= static_cast<int>(layers.size()))
                            activeLayerIndex = static_cast<int>(layers.size()) - 1;
                    }
                    if (renderLayers) renderLayers();
                    changed = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Merge Down##layer") && i > 0) {
                    // TODO: implement merge down
                }
                ImGui::EndPopup();
            }

            ImGui::SameLine();
            ImGui::PushItemWidth(60);
            if (i > 0 && ImGui::Button("^##up", ImVec2(25, 0))) {
                if (reorderLayers) reorderLayers(i, i - 1);
                else { std::swap(layers[i], layers[i - 1]); activeLayerIndex = i - 1; }
                if (renderLayers) renderLayers();
                changed = true;
            }
            ImGui::SameLine();
            if (i < static_cast<int>(layers.size()) - 1 && ImGui::Button("v##down", ImVec2(25, 0))) {
                if (reorderLayers) reorderLayers(i, i + 1);
                else { std::swap(layers[i], layers[i + 1]); activeLayerIndex = i + 1; }
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

    // Active layer properties
    pelpaint::Layer* activeLayer = nullptr;
    if (activeLayerIndex >= 0 && activeLayerIndex < static_cast<int>(layers.size()))
        activeLayer = &layers[activeLayerIndex];

    if (activeLayer) {
        ImGui::Text("Active Layer: %s", activeLayer->name.c_str());
        ImGui::Spacing();

        if (ImGui::CollapsingHeader("Layer Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::SliderFloat("Opacity", &activeLayer->opacity, 0.0f, 1.0f, "%.2f")) {
                if (renderLayers) renderLayers();
                changed = true;
            }

            const char* blendModes[] = { "Normal", "Multiply", "Screen", "Overlay" };
            if (ImGui::Combo("Blend Mode", &activeLayer->blendMode, blendModes, IM_ARRAYSIZE(blendModes))) {
                if (renderLayers) renderLayers();
                changed = true;
            }

            ImVec4 tintColor(activeLayer->blendColor.r, activeLayer->blendColor.g,
                             activeLayer->blendColor.b, activeLayer->blendColor.a);
            if (ImGui::ColorEdit4("Tint Color", &tintColor.x)) {
                activeLayer->blendColor = pelpaint::Color4f{tintColor.x, tintColor.y, tintColor.z, tintColor.w};
                if (renderLayers) renderLayers();
                changed = true;
            }

            if (ImGui::Checkbox("Lock Layer", &activeLayer->locked))
                changed = true;
        }
    }

    ImGui::EndChild();
    return changed;
}

} // namespace pelpaint::ui
