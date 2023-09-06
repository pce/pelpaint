#include "FileUtils.hpp"

FileUtils::FileUtils()
{
}

void FileUtils::OpenImage(const fs::path &filePath)
{
}

void FileUtils::SaveImage(const fs::path &filePath)
{
    // auto out = std::ofstream(filePath, std::ios::binary);

    // if (!out)
    // {
    //     throw std::runtime_error("failded to open file for writing");
    // }

}

void FileUtils::OpenFile(const fs::path &filePath)
{
#ifdef _WIN32
    const auto command = "start \"\" \"" + filePath.string() + "\"";
#elif __APPLE__
    const auto command = "open \"" + filePath.string() + "\"";
#else
    const auto command = "xdg-open \"" + filePath.string() + "\"";
#endif

    std::system(command.c_str());
}
