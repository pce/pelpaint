#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>
#include <tuple>
#include <vector>
#include <imgui.h>
#include <implot.h>
#include <opencv2/opencv.hpp>
#include <opencv2/highgui/highgui.hpp>

#include "FileUtils.hpp"

namespace fs = std::filesystem;

class PixelPaintView
{
public:
    using PointData = std::tuple<ImVec2, ImColor, float>;

public:
    PixelPaintView() : points({}), canvasPos({}), currentDrawColor(ImColor(255, 255, 255)), pointDrawSize(2.0f){};
    void Draw(std::string_view label);

private:
    void SaveImage(std::string_view filename);
    void ExportImage(std::string_view filename);
    void ImportImage(std::string_view filename);
    cv::Mat LoadAndResizeImage(std::string_view filename);
    void LoadImage(std::string_view filename);
    void DrawColorButtons();
    void DrawOptionsButtons();
    void SmartRepaint();
    void DrawLineBresenham(const ImVec2 &p1, const ImVec2 &p2);
    float CalculateDistance(const ImVec2 &p1, const ImVec2 &p2);
    void RemovePointsAt(const ImVec2 &position, float eraseRadius);
    void Undo();
    void ClearCanvas();

    int numRows = 800;
    int numCols = 600;
    // const std::uint32_t channels = 3;

    ImVec2 canvasSize = ImVec2(numRows, numCols);

    std::vector<PointData> points;

    ImVec2 canvasPos;

    ImColor currentDrawColor = ImColor(255, 255, 255);
    float pointDrawSize = 2.0f;
    float smartMaxPointSize{12.f};

    char filenameBuffer[255] = "new.bin";
    FileUtils fileUtils;

    bool isEraserMode{false};
    bool isSnappy{true};
    bool isLineMode{false};
    bool isDrawingLine{false};
    bool hasImportToFit{false};
    ImVec2 lineStartPoint;
};
