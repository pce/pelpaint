#include <iostream>
#include <fstream>

#include <cstdlib>
#include <fmt/format.h>
#include <imgui.h>
#include <implot.h>
#include <filesystem>
#include <string_view>
#include <string>
#include <stdexcept>

#include <opencv2/opencv.hpp>
#include <opencv2/highgui/highgui.hpp>

#include "ImGuiFileDialog.h"

#include "PixelPaintView.hpp"

namespace fs = std::filesystem;

void PixelPaintView::Draw(std::string_view label)
{
    constexpr static auto window_flags =
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse;

    ImVec2 screenSize = ImGui::GetIO().DisplaySize;
    // window size to match the screen width and height
    ImGui::SetNextWindowSize(screenSize);

    constexpr static auto window_pos = ImVec2(0.0F, 0.0F);
    ImGui::SetNextWindowPos(window_pos);

    canvasSize = ImVec2(static_cast<float>(numCols), static_cast<float>(numRows));

    ImGui::Begin(label.data(), nullptr, window_flags);

    const auto ctrl_pressed = ImGui::GetIO().KeyCtrl;
    const auto s_pressed =
        ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_S));
    const auto o_pressed =
        ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_O));
    const auto esc_pressed =
        ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape));

    const auto isBackspaceKeyPressed = ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Backspace));

    // const auto isShiftPressed = ImGui::IsKeyDown(ImGui::GetKeyIndex(ImGuiKey_LeftShift));

    if (ImGui::Button("Load") || (ctrl_pressed && o_pressed))
    {
        ImGui::OpenPopup("Load Image");
    }
    ImGui::SameLine();
    if (ImGui::Button("Save") || (ctrl_pressed && s_pressed))
    {
        ImGui::OpenPopup("Save Image");
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
    {
        ClearCanvas();
    }

    ImGui::SameLine();
    if (ImGui::Button("Undo") || isBackspaceKeyPressed)
    {
        Undo();
    }

    ImGui::SameLine();
    if (ImGui::Button("Smart Repaint"))
    {
        SmartRepaint();
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(32);
    ImGui::InputFloat("max", &smartMaxPointSize);

    DrawOptionsButtons();
    DrawColorButtons();

    ImGui::Text("Draw Size");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(256);
    ImGui::SliderFloat("###DRAW_SIZE_SLIDER", &pointDrawSize, 1.0f, 42.0f);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(64);
    ImGui::InputFloat("###DRAW_SIZE_INPUT", &pointDrawSize);

    if (ImGui::BeginPopupModal("Save Image"))
    {
        ImGui::InputText("Filename", filenameBuffer, sizeof(filenameBuffer));
        if (ImGui::Button("Save"))
        {
            SaveImage(filenameBuffer);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();

        if (ImGui::Button("Cancel") || esc_pressed)
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Load Image"))
    {
        ImGui::InputText("Filename", filenameBuffer, sizeof(filenameBuffer));
        if (ImGui::Button("Load"))
        {
            LoadImage(filenameBuffer);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();

        if (ImGui::Button("Cancel") || esc_pressed)
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    canvasPos = ImGui::GetCursorScreenPos();
    const float canvasBorderThickness = 1.5f;
    const auto buttonSize = ImVec2(canvasSize.x + 2.0f * canvasBorderThickness, canvasSize.y + 2.0f * canvasBorderThickness);

    // prevent drawing over the UI thru a clipping rectangle
    ImVec2 clipRectMin = ImVec2(canvasPos.x + canvasBorderThickness, canvasPos.y + canvasBorderThickness);
    ImVec2 clipRectMax = ImVec2(clipRectMin.x + canvasSize.x, clipRectMin.y + canvasSize.y);
    ImGui::PushClipRect(clipRectMin, clipRectMax, true);

    ImGui::InvisibleButton("###canvas", buttonSize);

    const auto mousePos = ImGui::GetMousePos();
    const auto isMouseHovering = ImGui::IsItemHovered();
    if (isMouseHovering && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {

        if (isEraserMode)
        {
            RemovePointsAt(mousePos, pointDrawSize);
        }
        else if (isLineMode)
        {
            if (!isDrawingLine)
            {
                // first click to start a line
                lineStartPoint = ImVec2(
                    mousePos.x - canvasPos.x - canvasBorderThickness,
                    mousePos.y - canvasPos.y - canvasBorderThickness);
                isDrawingLine = true;
            }
            else
            {
                // second click: calculate the line points
                const auto lineEndPoint = ImVec2(
                    mousePos.x - canvasPos.x - canvasBorderThickness,
                    mousePos.y - canvasPos.y - canvasBorderThickness);

                if (isSnappy)
                {
                    // start and end points aligned to the grid
                    const auto gridSize = pointDrawSize;
                    const float snappedX1 = roundf(lineStartPoint.x / gridSize) * gridSize;
                    const float snappedY1 = roundf(lineStartPoint.y / gridSize) * gridSize;
                    const float snappedX2 = roundf(lineEndPoint.x / gridSize) * gridSize;
                    const float snappedY2 = roundf(lineEndPoint.y / gridSize) * gridSize;

                    DrawLineBresenham(ImVec2(snappedX1, snappedY1), ImVec2(snappedX2, snappedY2));
                }
                else
                {
                    DrawLineBresenham(lineStartPoint, lineEndPoint);
                }
                isDrawingLine = false;
            }
        }
        else
        {
            const auto point = ImVec2(
                mousePos.x - canvasPos.x - canvasBorderThickness,
                mousePos.y - canvasPos.y - canvasBorderThickness);

            if (isSnappy)
            {
                // store point at the nearest grid position based on pointDrawSize
                const auto gridSize = pointDrawSize;
                // const auto snappedX = roundf((mousePos.x - canvasPos.x - canvasBorderThickness) / gridSize) * gridSize;
                // const auto snappedY = roundf((mousePos.y - canvasPos.y - canvasBorderThickness) / gridSize) * gridSize;
                const float snappedX = roundf(point.x / gridSize) * gridSize;
                const float snappedY = roundf(point.y / gridSize) * gridSize;
                points.push_back(std::make_tuple(ImVec2(snappedX, snappedY), currentDrawColor, pointDrawSize));
            }
            else
            {
                points.push_back(std::make_tuple(point, currentDrawColor, pointDrawSize));
            }
        }
    }

    auto *drawList = ImGui::GetWindowDrawList();
    for (const auto &[point, color, size] : points)
    {
        drawList->AddCircleFilled(
            ImVec2(
                canvasPos.x + canvasBorderThickness + point.x,
                canvasPos.y + canvasBorderThickness + point.y),
            size,
            color,
            16);
    }

    ImGui::PopClipRect();

    const auto borderMin = canvasPos;
    const auto borderMax = ImVec2(
        canvasPos.x + buttonSize.x - canvasBorderThickness,
        canvasPos.y + buttonSize.y - canvasBorderThickness);

    drawList->AddRect(borderMin, borderMax, IM_COL32(255, 255, 255, 255), 0.f, ImDrawCornerFlags_All, canvasBorderThickness);

    ImGui::End();
}

void PixelPaintView::RemovePointsAt(const ImVec2 &position, float eraseRadius)
{
    auto iter = points.begin();
    while (iter != points.end())
    {
        const ImVec2 &pointPos = std::get<0>(*iter);
        const float distance = CalculateDistance(pointPos, position);
        // point is within the erase radius?
        if (distance <= eraseRadius)
        {
            iter = points.erase(iter);
        }
        else
        {
            ++iter;
        }
    }
}

void PixelPaintView::SaveImage(std::string_view filename)
{
    // convert the string_view to an fs::path
    // fs::path filePath(filename.data());
    // fileUtils.SaveImage(filePath);
    try
    {
        auto out = std::ofstream(filename.data(), std::ios::binary);
        if (!out || !out.is_open())
        {
            throw std::runtime_error("failded to open file for writing");
        }
        const auto pointCount = points.size();
        out.write(reinterpret_cast<const char *>(&pointCount), sizeof(pointCount));

        for (const auto &[point, color, size] : points)
        {
            out.write(reinterpret_cast<const char *>(&point), sizeof(point));
            out.write(reinterpret_cast<const char *>(&color), sizeof(color));
            out.write(reinterpret_cast<const char *>(&size), sizeof(size));
        }
        out.close();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception @SaveImage: " << e.what() << std::endl;
    }
}

cv::Mat PixelPaintView::LoadAndResizeImage(std::string_view filename)
{
    cv::Mat image = cv::imread(std::string(filename), cv::IMREAD_COLOR);
    if (image.empty())
    {
        throw std::runtime_error("Failed to open image file");
    }
    cv::resize(image, image, cv::Size(numRows, numCols));
    return image;
}

void PixelPaintView::ImportImage(std::string_view filename)
{
    try
    {
        cv::Mat image;
        if (!hasImportToFit)
        {
            image = cv::imread(std::string(filename), cv::IMREAD_COLOR);
        }
        else
        {
            // resize the image to fit maxWidth * maxHeight
            image = LoadAndResizeImage(filename);
        }

        if (image.empty())
        {
            throw std::runtime_error("Failed to open image file");
        }
        ClearCanvas();

        canvasSize = ImVec2(static_cast<float>(numCols), static_cast<float>(numRows));

        for (int y = 0; y < image.rows; y += static_cast<int>(pointDrawSize))
        {
            for (int x = 0; x < image.cols; x += static_cast<int>(pointDrawSize))
            {
                cv::Vec3b pixel = image.at<cv::Vec3b>(y, x);

                ImVec2 point(static_cast<float>(x), static_cast<float>(y));
                ImColor color(static_cast<float>(pixel[2]) / 255.0f, static_cast<float>(pixel[1]) / 255.0f, static_cast<float>(pixel[0]) / 255.0f);
                float size = pointDrawSize; // desired size

                points.push_back(std::make_tuple(point, color, size));
            }
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception @ImportImage: " << e.what() << std::endl;
    }
}

void PixelPaintView::ExportImage(std::string_view filename)
{

    try
    {
        const std::string filePath(filename);

        cv::Mat image(numRows, numCols, CV_8UC3, cv::Scalar(0, 0, 0));

        for (const auto &[point, color, size] : points)
        {
            // calculate the radius based on the point size
            float radius = size * 0.5f;
            // convert ImVec2 to cv::Point
            cv::Point cvPoint(static_cast<int>(point.x), static_cast<int>(point.y));
            // draw a filled circle on the image
            cv::circle(image, cvPoint, static_cast<int>(radius),
                       cv::Scalar(color.Value.x * 255, // R
                                  color.Value.y * 255, // G
                                  color.Value.z * 255  // B
                                  ),
                       -1); // -1 means filled circle
        }

        /*
        // draw pixels instead of circles
        for (const auto &[point, color, size] : points)
        {
            cv::Point cvPoint(static_cast<int>(point.x), static_cast<int>(point.y));

            cv::Vec3b &pixel = image.at<cv::Vec3b>(cvPoint);
            pixel[0] = color.Value.x * 255; // R
            pixel[1] = color.Value.y * 255; // G
            pixel[2] = color.Value.z * 255; // B
        }
        */

        // save as PNG or JPG based on the file extension
        std::string fileExtension = fs::path(filename).extension().string();
        if (fileExtension == ".png" || fileExtension == ".PNG")
        {
            if (!cv::imwrite(filePath, image))
            {
                throw std::runtime_error("Failed to save PNG image");
            }
        }
        else if (fileExtension == ".jpg" || fileExtension == ".JPG" || fileExtension == ".jpeg" || fileExtension == ".JPEG")
        {
            std::vector<int> compression_params;
            compression_params.push_back(cv::IMWRITE_JPEG_QUALITY);
            compression_params.push_back(95); // quality

            if (!cv::imwrite(filePath, image, compression_params))
            {
                throw std::runtime_error("Failed to save JPG image");
            }
        }
        else
        {
            throw std::runtime_error("Unsupported image format");
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception @ExportImage: " << e.what() << std::endl;
    }
}

void PixelPaintView::LoadImage(std::string_view filename)
{
    // convert the string_view to an fs::path
    // fs::path filePath(filename.data());
    // fileUtils.OpenImage(filePath);
    try
    {
        auto in = std::ifstream(filename.data(), std::ios::binary);
        if (!in || !in.is_open())
        {
            throw std::runtime_error("failed to open file for reading");
        }
        auto pointCount = std::size_t{0};
        in.read(reinterpret_cast<char *>(&pointCount), sizeof(pointCount));

        ClearCanvas();
        // points.reserve();

        for (std::size_t i = 0; i < pointCount; i++)
        {
            auto point = ImVec2{};
            auto color = ImColor{};
            auto size = float{};

            in.read(reinterpret_cast<char *>(&point), sizeof(point));
            in.read(reinterpret_cast<char *>(&color), sizeof(color));
            in.read(reinterpret_cast<char *>(&size), sizeof(size));

            points.push_back(std::make_tuple(point, color, size));
        }
        in.close();
    }
    catch (const std::exception &e)
    {
        std::cerr << "Exception @LoadImage: " << e.what() << std::endl;
    }
}

void PixelPaintView::DrawColorButtons()
{
    if (ImGui::Button("Color"))
        ImGui::OpenPopup("Color Picker");
    if (ImGui::BeginPopup("Color Picker"))
    {
        ImGui::ColorPicker3("##picker",
                            reinterpret_cast<float *>(&currentDrawColor));
        ImGui::EndPopup();
    }
}

void PixelPaintView::DrawOptionsButtons()
{
    const auto esc_pressed =
        ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape));

    if (ImGui::Button("Import"))
    {
        std::cout << "Open the file dialog for image import" << std::endl;

        ImGuiFileDialog::Instance()->OpenDialog("ImportImageDlg", "Choose File", ".png,.jpg,.jpeg", ".");
    }

    if (ImGuiFileDialog::Instance()->Display("ImportImageDlg"))
    {
        std::cout << "Display the file dialog" << std::endl;
        if (ImGuiFileDialog::Instance()->IsOk())
        {
            std::string filePath = ImGuiFileDialog::Instance()->GetFilePathName();
            // call ImportImage function with the selected file path
            ImportImage(filePath);
        }
        else
        {
            std::cerr << "Can not display the file dialog" << std::endl;
        }
        ImGuiFileDialog::Instance()->Close();
    }

    ImGui::SameLine();

    ImGui::Checkbox("Resize", &hasImportToFit);
    ImGui::SameLine();
    ImGui::PushItemWidth(100);
    ImGui::InputInt("Width", &numCols);
    ImGui::SameLine();
    ImGui::InputInt("Height", &numRows);
    ImGui::PopItemWidth();

    ImGui::SameLine();
    if (ImGui::Button("Export"))
        ImGui::OpenPopup("Export");
    if (ImGui::BeginPopup("Export"))
    {
        ImGui::InputText("Filename", filenameBuffer, sizeof(filenameBuffer));
        if (ImGui::Button("Export"))
        {
            ExportImage(filenameBuffer);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();

        if (ImGui::Button("Cancel") || esc_pressed)
        {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Snap to Grid", &isSnappy);
    ImGui::SameLine();
    ImGui::Checkbox("Lines Mode", &isLineMode);
    ImGui::SameLine();
    ImGui::Checkbox("Eraser Mode", &isEraserMode);
}

void PixelPaintView::ClearCanvas()
{
    points.clear();
}

void PixelPaintView::Undo()
{
    // No History Stack, undo import etc. Atm removes last pixel
    if (!points.empty())
    {
        points.pop_back();
    }
}

float PixelPaintView::CalculateDistance(const ImVec2 &p1, const ImVec2 &p2)
{
    float dx = p2.x - p1.x;
    float dy = p2.y - p1.y;
    return sqrt(dx * dx + dy * dy);
}

void PixelPaintView::SmartRepaint()
{
    // const float smartMaxPointSize = 12.0f;

    auto mergedPoints = std::vector<std::tuple<ImVec2, ImColor, float>>();
    auto currentColor = ImColor(0, 0, 0);

    for (const auto &[point, color, size] : points)
    {
        if (color != currentColor)
        {
            // new group when the color changes
            currentColor = color;
            mergedPoints.push_back(std::make_tuple(point, color, size));
        }
        else
        {
            // same color increases the size
            if (!mergedPoints.empty())
            {
                auto &[mergedPoint, mergedColor, mergedSize] = mergedPoints.back();
                mergedSize = std::min(mergedSize + size, smartMaxPointSize);
            }
        }
    }
    points = std::move(mergedPoints);
}

void PixelPaintView::DrawLineBresenham(const ImVec2 &p1, const ImVec2 &p2)
{
    //  calculate points along a line using Bresenham's algorithm
    const int x1 = static_cast<int>(p1.x);
    const int y1 = static_cast<int>(p1.y);
    const int x2 = static_cast<int>(p2.x);
    const int y2 = static_cast<int>(p2.y);

    const int dx = abs(x2 - x1);
    const int dy = abs(y2 - y1);
    const int sx = (x1 < x2) ? 1 : -1;
    const int sy = (y1 < y2) ? 1 : -1;

    int err = dx - dy;
    int x = x1;
    int y = y1;

    while (true)
    {
        // add the current point to the points vector
        points.push_back(std::make_tuple(ImVec2(static_cast<float>(x), static_cast<float>(y)), currentDrawColor, pointDrawSize));

        if (x == x2 && y == y2)
            break;

        int err2 = 2 * err;

        if (err2 > -dy)
        {
            err -= dy;
            x += sx;
        }

        if (err2 < dx)
        {
            err += dx;
            y += sy;
        }
    }
}