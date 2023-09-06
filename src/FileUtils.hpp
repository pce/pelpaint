#ifndef FILE_UTILS_H
#define FILE_UTILS_H

#include <filesystem>
#include <iostream>
#include <vector>


namespace fs = std::filesystem;

class FileUtils {
public:
    FileUtils();
    void OpenFile(const fs::path &filePath);
    void OpenImage(const fs::path &filePath);
    void SaveImage(const fs::path &filePath);

    // private std::vector<PointData> points;

};

#endif