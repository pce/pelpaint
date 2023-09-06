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

#include "PixelPaintView.hpp"

namespace fs = std::filesystem;

void PixelPaintView::Draw(std::string_view label)
{
    constexpr static auto window_flags =
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse;

    constexpr static auto window_size = ImVec2(1280.0F, 720.0F);
    constexpr static auto window_pos = ImVec2(0.0F, 0.0F);

    ImGui::SetNextWindowSize(window_size);
    ImGui::SetNextWindowPos(window_pos);

    ImGui::Begin(label.data(), nullptr, window_flags);

    const auto ctrl_pressed = ImGui::GetIO().KeyCtrl;
    const auto s_pressed =
        ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_S));
    const auto o_pressed =
        ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_O));
    const auto esc_pressed =
        ImGui::IsKeyPressed(ImGui::GetKeyIndex(ImGuiKey_Escape));

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

    DrawOptionsButtons();
    DrawColorButtons();

    ImGui::Text("Draw Size");
    ImGui::SameLine();
    ImGui::SliderFloat("###DRAW_SIZE", &pointDrawSize, 1.0f, 10.0f);

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

    ImGui::InvisibleButton("###canvas", buttonSize);

    const auto mousePos = ImGui::GetMousePos();
    const auto isMouseHovering = ImGui::IsItemHovered();
    if (isMouseHovering && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        const auto point = ImVec2(mousePos.x - canvasPos.x - canvasBorderThickness,
                                  mousePos.y - canvasPos.y - canvasBorderThickness);

        points.push_back(std::make_tuple(point, currentDrawColor, pointDrawSize));
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

    const auto borderMin = canvasPos;
    const auto borderMax = ImVec2(
        canvasPos.x + buttonSize.x - canvasBorderThickness,
        canvasPos.y + buttonSize.y - canvasBorderThickness);

    drawList->AddRect(borderMin, borderMax, IM_COL32(255, 255, 255, 255), 0.f, ImDrawCornerFlags_All, canvasBorderThickness);

    ImGui::End();
}

void PixelPaintView::SaveImage(std::string_view filename)
{
    // convert the string_view to an fs::path
    // fs::path filePath(filename.data());
    // fileUtils.SaveImage(filePath);
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

void PixelPaintView::ImportImage(std::string_view filename)
{
    cv::Mat image = cv::imread(std::string(filename), cv::IMREAD_COLOR);

    if (image.empty())
    {
        throw std::runtime_error("Failed to open image file");
    }
    ClearCanvas();

    for (int y = 0; y < image.rows; ++y)
    {
        for (int x = 0; x < image.cols; ++x)
        {
            cv::Vec3b pixel = image.at<cv::Vec3b>(y, x);

            ImVec2 point(static_cast<float>(x), static_cast<float>(y));
            ImColor color(static_cast<float>(pixel[2]) / 255.0f, static_cast<float>(pixel[1]) / 255.0f, static_cast<float>(pixel[0]) / 255.0f);
            float size = pointDrawSize; // desired size

            points.push_back(std::make_tuple(point, color, size));
        }
    }
}

void PixelPaintView::ExportImage(std::string_view filename)
{
    const std::string filePath(filename);

    cv::Mat image(numRows, numCols, CV_8UC3, cv::Scalar(0, 0, 0));

    for (const auto &[point, color, size] : points)
    {
        // convert ImVec2 to cv::Point
        cv::Point cvPoint(static_cast<int>(point.x), static_cast<int>(point.y));

        cv::Vec3b &pixel = image.at<cv::Vec3b>(cvPoint);
        pixel[0] = color.Value.x * 255; // R
        pixel[1] = color.Value.y * 255; // G
        pixel[2] = color.Value.z * 255; // B
    }
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

void PixelPaintView::LoadImage(std::string_view filename)
{
    // convert the string_view to an fs::path
    // fs::path filePath(filename.data());
    // fileUtils.OpenImage(filePath);

    auto in = std::ifstream(filename.data(), std::ios::binary);
    if (!in || !in.is_open())
    {
        throw std::runtime_error("failded to open file for reading");
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
        ImGui::OpenPopup("Import");
    if (ImGui::BeginPopup("Import"))
    {
        ImGui::EndPopup();
    }
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
}

void PixelPaintView::ClearCanvas()
{
    points.clear();
}