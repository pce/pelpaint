// FileChooser_macOS.mm
// Native macOS file dialogs using NSOpenPanel / NSSavePanel.
// Compiled ONLY on macOS desktop (not iOS, not WASM).
//
// Called from FileChooser.cpp via forward-declared C++ functions.

#import <Foundation/Foundation.h>
#import <AppKit/AppKit.h>
#if __has_include(<UniformTypeIdentifiers/UniformTypeIdentifiers.h>)
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#endif

#include "FileChooser.h"
#include <sstream>
#include <string>

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Convert ".png,.jpg,.tga" → NSArray<NSString*> of bare extensions ("png","jpg","tga")
static NSArray<NSString*>* ExtensionsFromFilters(const std::string& filters)
{
    NSMutableArray<NSString*> *result = [NSMutableArray array];
    std::istringstream stream(filters);
    std::string token;
    while (std::getline(stream, token, ',')) {
        // Strip leading/trailing spaces and leading dots
        while (!token.empty() && (token.front() == ' ' || token.front() == '.'))
            token.erase(token.begin());
        while (!token.empty() && token.back() == ' ')
            token.pop_back();
        if (!token.empty())
            [result addObject:@(token.c_str())];
    }
    return [result copy];
}

/// Apply allowed file types to an NSSavePanel (covers both open and save panels)
static void ApplyFilters(NSSavePanel *panel, const std::string& filters)
{
    NSArray<NSString*> *exts = ExtensionsFromFilters(filters);
    if (exts.count == 0) return;

#if __has_include(<UniformTypeIdentifiers/UniformTypeIdentifiers.h>)
    if (@available(macOS 12.0, *)) {
        NSMutableArray<UTType*> *utTypes = [NSMutableArray array];
        for (NSString *ext in exts) {
            UTType *t = [UTType typeWithFilenameExtension:ext];
            if (t) [utTypes addObject:t];
        }
        if (utTypes.count > 0) {
            panel.allowedContentTypes = utTypes;
            return; // done — skip the deprecated path
        }
    }
#endif

    // Fallback: extension strings (deprecated in macOS 12 but universally supported)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    panel.allowedFileTypes = exts;
#pragma clang diagnostic pop
}

// ─────────────────────────────────────────────────────────────────────────────
// Public C++ API (forward-declared in FileChooser.cpp)
// ─────────────────────────────────────────────────────────────────────────────

void FileChooser_macOS_OpenFile(
    const std::string& title,
    const std::string& filters,
    const std::string& startPath,
    FileChooserCallback callback)
{
    // Copy all C++ strings by value -- the references would dangle after this
    // function returns, before the dispatch_async block runs on the main queue.
    std::string titleCopy     = title;
    std::string filtersCopy   = filters;
    std::string startPathCopy = startPath;

    // NSOpenPanel must be shown on the main thread.
    dispatch_async(dispatch_get_main_queue(), ^{
        NSOpenPanel *panel = [NSOpenPanel openPanel];
        panel.title = @(titleCopy.c_str());
        panel.canChooseFiles          = YES;
        panel.canChooseDirectories    = NO;
        panel.allowsMultipleSelection = NO;
        panel.treatsFilePackagesAsDirectories = NO;

        if (!startPathCopy.empty()) {
            NSURL *dirURL = [NSURL fileURLWithPath:@(startPathCopy.c_str()) isDirectory:YES];
            panel.directoryURL = dirURL;
        }

        ApplyFilters(panel, filtersCopy);

        [panel beginWithCompletionHandler:^(NSModalResponse response) {
            std::string chosen;
            if (response == NSModalResponseOK && panel.URL)
                chosen = [[panel.URL path] UTF8String];
            if (callback) callback(chosen);
        }];
    });
}

void FileChooser_macOS_SaveFile(
    const std::string& title,
    const std::string& filters,
    const std::string& suggestedFilename,
    const std::string& startPath,
    FileChooserCallback callback)
{
    // Copy all C++ strings by value -- the references would dangle after this
    // function returns, before the dispatch_async block runs on the main queue.
    std::string titleCopy     = title;
    std::string filtersCopy   = filters;
    std::string filenameCopy  = suggestedFilename;
    std::string startPathCopy = startPath;

    dispatch_async(dispatch_get_main_queue(), ^{
        NSSavePanel *panel = [NSSavePanel savePanel];
        panel.title = @(titleCopy.c_str());
        panel.nameFieldStringValue = @(filenameCopy.c_str());
        panel.canCreateDirectories = YES;

        if (!startPathCopy.empty()) {
            NSURL *dirURL = [NSURL fileURLWithPath:@(startPathCopy.c_str()) isDirectory:YES];
            panel.directoryURL = dirURL;
        }

        ApplyFilters(panel, filtersCopy);

        [panel beginWithCompletionHandler:^(NSModalResponse response) {
            std::string chosen;
            if (response == NSModalResponseOK && panel.URL)
                chosen = [[panel.URL path] UTF8String];
            if (callback) callback(chosen);
        }];
    });
}

