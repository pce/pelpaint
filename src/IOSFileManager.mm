#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>
#import <objc/runtime.h>

#include "IOSFileManager.h"

#if TARGET_OS_IOS || TARGET_OS_TV

// Helper to get the root view controller
static UIViewController* GetRootViewController() {
    UIWindow *window = nil;
    for (UIWindow *w in [UIApplication sharedApplication].windows) {
        if (w.isKeyWindow) {
            window = w;
            break;
        }
    }

    if (!window) {
        window = [UIApplication sharedApplication].windows.firstObject;
    }

    return window.rootViewController;
}

// Save file using iOS share sheet (UIActivityViewController)
bool iOS_SaveFile(const char* filename, const void* data, size_t dataSize) {
    if (!filename || !data || dataSize == 0) {
        return false;
    }

    @autoreleasepool {
        // Create NSData from the buffer
        NSData *fileData = [NSData dataWithBytes:data length:dataSize];

        // Create a temporary file in the temp directory
        NSString *tempDir = NSTemporaryDirectory();
        NSString *tempFilePath = [tempDir stringByAppendingPathComponent:@(filename)];

        // Write data to temp file
        NSError *error = nil;
        if (![fileData writeToFile:tempFilePath options:NSDataWritingAtomic error:&error]) {
            NSLog(@"Failed to write temp file: %@", error);
            return false;
        }

        NSURL *fileURL = [NSURL fileURLWithPath:tempFilePath];

        // Create activity view controller
        UIActivityViewController *activityVC = [[UIActivityViewController alloc]
            initWithActivityItems:@[fileURL]
            applicationActivities:nil];

        // For iPad, we need to set the popover presentation controller
        if (iOS_IsIPad()) {
            activityVC.popoverPresentationController.sourceView = GetRootViewController().view;
            activityVC.popoverPresentationController.sourceRect = CGRectMake(
                GetRootViewController().view.bounds.size.width / 2,
                GetRootViewController().view.bounds.size.height / 2,
                1, 1
            );
            activityVC.popoverPresentationController.permittedArrowDirections = 0;
        }

        // Present the share sheet
        dispatch_async(dispatch_get_main_queue(), ^{
            UIViewController *rootVC = GetRootViewController();
            [rootVC presentViewController:activityVC animated:YES completion:nil];
        });

        return true;
    }
}

// Save file directly to app's Documents directory
bool iOS_SaveToDocuments(const char* filename, const void* data, size_t dataSize) {
    if (!filename || !data || dataSize == 0) {
        return false;
    }

    @autoreleasepool {
        // Get Documents directory
        NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        if (paths.count == 0) {
            return false;
        }

        NSString *documentsDirectory = paths[0];
        NSString *filePath = [documentsDirectory stringByAppendingPathComponent:@(filename)];

        // Create NSData and write to file
        NSData *fileData = [NSData dataWithBytes:data length:dataSize];
        NSError *error = nil;

        if (![fileData writeToFile:filePath options:NSDataWritingAtomic error:&error]) {
            NSLog(@"Failed to write to Documents: %@", error);
            return false;
        }

        NSLog(@"File saved to: %@", filePath);
        return true;
    }
}

// Get Documents directory path
char* iOS_GetDocumentsPath(void) {
    @autoreleasepool {
        NSArray *paths = NSSearchPathForDirectoriesInDomains(NSDocumentDirectory, NSUserDomainMask, YES);
        if (paths.count == 0) {
            return NULL;
        }

        NSString *documentsDirectory = paths[0];
        const char *cPath = [documentsDirectory UTF8String];

        // Allocate and copy the string
        size_t len = strlen(cPath) + 1;
        char *result = (char*)malloc(len);
        if (result) {
            strcpy(result, cPath);
        }

        return result;
    }
}

// Document picker delegate
@interface DocumentPickerDelegate : NSObject <UIDocumentPickerDelegate>
@property (nonatomic, copy) void (^completionBlock)(const char*);
@property (nonatomic, assign) void* context;
@property (nonatomic, assign) iOS_FilePickerCallback callback;
@end

@implementation DocumentPickerDelegate

- (void)documentPicker:(UIDocumentPickerViewController *)controller
didPickDocumentsAtURLs:(NSArray<NSURL *> *)urls {
    if (urls.count > 0) {
        NSURL *url = urls[0];

        // Start accessing security-scoped resource
        BOOL didStartAccessing = [url startAccessingSecurityScopedResource];

        const char *path = [[url path] UTF8String];
        if (self.callback) {
            self.callback(self.context, path);
        }
        if (self.completionBlock) {
            self.completionBlock(path);
        }

        if (didStartAccessing) {
            [url stopAccessingSecurityScopedResource];
        }
    }
}

- (void)documentPickerWasCancelled:(UIDocumentPickerViewController *)controller {
    NSLog(@"Document picker cancelled");
}

@end

// Present file picker with context
bool iOS_OpenFilePickerWithContext(void* context, iOS_FilePickerCallback callback) {
    if (!callback) {
        return false;
    }

    @autoreleasepool {
        DocumentPickerDelegate *delegate = [[DocumentPickerDelegate alloc] init];
        delegate.context = context;
        delegate.callback = callback;

        UIDocumentPickerViewController *picker;

        if (@available(iOS 14.0, *)) {
            NSArray *contentTypes = @[
                UTTypeImage,
                UTTypePNG,
                UTTypeJPEG
            ];
            picker = [[UIDocumentPickerViewController alloc]
                initForOpeningContentTypes:contentTypes];
        } else {
            NSArray *documentTypes = @[@"public.image", @"public.png", @"public.jpeg"];
            picker = [[UIDocumentPickerViewController alloc]
                initWithDocumentTypes:documentTypes
                inMode:UIDocumentPickerModeOpen];
        }

        picker.delegate = delegate;
        picker.allowsMultipleSelection = NO;

        objc_setAssociatedObject(picker, "pelpaint_doc_picker_delegate", delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

        // For iPad, configure popover
        if (iOS_IsIPad()) {
            picker.modalPresentationStyle = UIModalPresentationPopover;
            UIPopoverPresentationController *popover = picker.popoverPresentationController;
            if (popover) {
                popover.sourceView = GetRootViewController().view;
                popover.sourceRect = CGRectMake(
                    GetRootViewController().view.bounds.size.width / 2,
                    GetRootViewController().view.bounds.size.height / 2,
                    1, 1
                );
                popover.permittedArrowDirections = 0;
            }
        }

        // Present the picker
        dispatch_async(dispatch_get_main_queue(), ^{
            UIViewController *rootVC = GetRootViewController();
            [rootVC presentViewController:picker animated:YES completion:nil];
        });

        return true;
    }
}

// Present file picker (legacy callback signature)
bool iOS_OpenFilePicker(void (*callback)(const char* filepath)) {
    if (!callback) {
        return false;
    }

    @autoreleasepool {
        DocumentPickerDelegate *delegate = [[DocumentPickerDelegate alloc] init];
        delegate.completionBlock = ^(const char *path) {
            callback(path);
        };

        UIDocumentPickerViewController *picker;

        if (@available(iOS 14.0, *)) {
            NSArray *contentTypes = @[
                UTTypeImage,
                UTTypePNG,
                UTTypeJPEG
            ];
            picker = [[UIDocumentPickerViewController alloc]
                initForOpeningContentTypes:contentTypes];
        } else {
            NSArray *documentTypes = @[@"public.image", @"public.png", @"public.jpeg"];
            picker = [[UIDocumentPickerViewController alloc]
                initWithDocumentTypes:documentTypes
                inMode:UIDocumentPickerModeOpen];
        }

        picker.delegate = delegate;
        picker.allowsMultipleSelection = NO;

        objc_setAssociatedObject(picker, "pelpaint_doc_picker_delegate", delegate, OBJC_ASSOCIATION_RETAIN_NONATOMIC);

        dispatch_async(dispatch_get_main_queue(), ^{
            UIViewController *rootVC = GetRootViewController();
            [rootVC presentViewController:picker animated:YES completion:nil];
        });

        return true;
    }
}

// Check if device is iPad
bool iOS_IsIPad(void) {
    return UI_USER_INTERFACE_IDIOM() == UIUserInterfaceIdiomPad;
}

#endif // TARGET_OS_IOS
