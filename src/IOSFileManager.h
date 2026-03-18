#pragma once

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#if TARGET_OS_IOS || TARGET_OS_TV

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*iOS_FilePickerCallback)(void* context, const char* filepath);

// Save file to iOS using the system share sheet
// This will present UIActivityViewController to let user choose where to save
// Returns true if the save dialog was presented successfully
bool iOS_SaveFile(const char* filename, const void* data, size_t dataSize);

// Save file to app's Documents directory (accessible via Files app)
// Returns true if saved successfully
bool iOS_SaveToDocuments(const char* filename, const void* data, size_t dataSize);

// Present iOS document picker to choose a file to open
// Callback will be called with the file path if user selects a file
// Returns true if picker was presented successfully
bool iOS_OpenFilePicker(void (*callback)(const char* filepath));

// Present iOS document picker with a context pointer
// Callback will be called with the file path if user selects a file
// Returns true if picker was presented successfully
bool iOS_OpenFilePickerWithContext(void* context, iOS_FilePickerCallback callback);

// Get the path to the app's Documents directory
// Returns NULL if failed, otherwise returns a string that must be freed by caller
char* iOS_GetDocumentsPath(void);

// Check if running on iPad
bool iOS_IsIPad(void);

#ifdef __cplusplus
}
#endif

#endif // TARGET_OS_IOS
