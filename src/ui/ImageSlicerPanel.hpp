#pragma once

#include "../tools/ImageSlicer.hpp"
#include <imgui.h>
#include <vector>

namespace pelpaint::ui {

class ImageSlicerPanel {
public:
    void Render() {
        if (!ImGui::Begin("Image Slicer")) {
            ImGui::End();
            return;
        }

        ImGui::Text("Beginner Mode");
        
        static int preset = 0;
        const char* presets[] = {"Cinematic Depth", "Paper Cutout", "Glass Shatter", "Anime Parallax", "Polygon Dream", "Motion Poster"};
        ImGui::Combo("Preset", &preset, presets, IM_ARRAYSIZE(presets));
        
        static float intensity = 0.5f;
        ImGui::SliderFloat("Intensity", &intensity, 0.0f, 1.0f);

        ImGui::Separator();

        static bool advanced = false;
        ImGui::Checkbox("Advanced", &advanced);

        if (advanced) {
            if (ImGui::CollapsingHeader("Generator", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::SliderInt("Num Slices", &options_.numSlices, 1, 10);
                ImGui::Checkbox("Invert Depth", &options_.invertDepth);
            }
            if (ImGui::CollapsingHeader("Edge Repair")) {
                ImGui::Checkbox("Feather Edges", &options_.featherEdges);
            }
            if (ImGui::CollapsingHeader("Export")) {
                if (ImGui::Button("Export to PNG")) {
                    // Logic here
                }
            }
        }
        
        if (ImGui::Button("Generate Slices")) {
            // Trigger generation
        }

        ImGui::End();
    }

private:
    pelpaint::slicer::GeneratorOptions options_;
};

} // namespace pelpaint::ui
