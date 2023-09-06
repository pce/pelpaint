#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <tuple>
#include <vector>
#include <imgui.h>
#include <implot.h>

#include "FileUtils.hpp"

namespace fs = std::filesystem;

class PixelPaintView
{
public:
    using PointData = std::tuple<ImVec2, ImColor, float>;
public:
    PixelPaintView():points({}), canvasPos({}), currentDrawColor(ImColor(255, 255, 255)), pointDrawSize(2.0f){};
    void Draw(std::string_view label);
    void SaveImage(std::string_view filename);
    void ExportImage(std::string_view filename);
    void ImportImage(std::string_view filename);
    void LoadImage(std::string_view filename);
    void DrawColorButtons();
    void DrawOptionsButtons();
    void ClearCanvas();
    
private:
    const std::uint32_t numRows = 800;
    const std::uint32_t numCols = 800;
    const std::uint32_t channels = 3;

    const ImVec2 canvasSize = ImVec2(numRows, numCols);

    std::vector<PointData> points;

    ImVec2 canvasPos;

    ImColor currentDrawColor = ImColor(255,255,255);
    float pointDrawSize = 2.0f;

    char filenameBuffer[255] =  "new.bin";
    FileUtils fileUtils;

};
