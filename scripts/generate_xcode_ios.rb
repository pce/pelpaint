#!/usr/bin/env ruby
# generate_xcode_ios.rb
#
# Generates a minimal Xcode iOS project scaffold for an SDL2-based C++ app.
# Usage: ruby generate_xcode_ios.rb [ProjectName]
#
# Requirements:
#   - Ruby
#   - SDL2 and SDL2_image frameworks for iOS (see SDL2 docs for download)
#   - Your SDL2-based source files (main.cpp, etc.)

require 'fileutils'

PROJECT_NAME = ARGV[0] || "SDL2iOSApp"
SRC_DIR = "../src"
PROJECT_DIR = "#{PROJECT_NAME}"
APP_DELEGATE_H = <<~H
#import <UIKit/UIKit.h>

@interface AppDelegate : UIResponder <UIApplicationDelegate>
@property (strong, nonatomic) UIWindow *window;
@end
H

APP_DELEGATE_M = <<~M
#import "AppDelegate.h"

@implementation AppDelegate
@end
M

MAIN_M = <<~M
#import <UIKit/UIKit.h>
#import "AppDelegate.h"

int main(int argc, char * argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, NSStringFromClass([AppDelegate class]));
    }
}
M

INFO_PLIST = <<~PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDevelopmentRegion</key>
    <string>en</string>
    <key>CFBundleExecutable</key>
    <string>$(EXECUTABLE_NAME)</string>
    <key>CFBundleIdentifier</key>
    <string>com.example.#{PROJECT_NAME.downcase}</string>
    <key>CFBundleInfoDictionaryVersion</key>
    <string>6.0</string>
    <key>CFBundleName</key>
    <string>#{PROJECT_NAME}</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>CFBundleShortVersionString</key>
    <string>1.0</string>
    <key>CFBundleVersion</key>
    <string>1</string>
    <key>UILaunchStoryboardName</key>
    <string>LaunchScreen</string>
    <key>UIMainStoryboardFile</key>
    <string></string>
    <key>UIRequiredDeviceCapabilities</key>
    <array>
        <string>armv7</string>
    </array>
    <key>UISupportedInterfaceOrientations</key>
    <array>
        <string>UIInterfaceOrientationPortrait</string>
        <string>UIInterfaceOrientationLandscapeLeft</string>
        <string>UIInterfaceOrientationLandscapeRight</string>
    </array>
</dict>
</plist>
PLIST

PCH = <<~PCH
#ifdef __OBJC__
    #import <UIKit/UIKit.h>
#endif
PCH

PODFILE = <<~POD
platform :ios, '11.0'
target '#{PROJECT_NAME}' do
  # pod 'SDL2', :podspec => 'https://raw.githubusercontent.com/libsdl-org/SDL/main/Xcode/SDL/SDL2.podspec'
  # pod 'SDL2_image', :podspec => 'https://raw.githubusercontent.com/libsdl-org/SDL_image/main/Xcode/SDL_image/SDL2_image.podspec'
end
POD

def create_project_structure
  puts "Creating Xcode iOS project scaffold for #{PROJECT_NAME}..."

  FileUtils.rm_rf(PROJECT_DIR)
  FileUtils.mkdir_p("#{PROJECT_DIR}/#{PROJECT_NAME}")
  FileUtils.mkdir_p("#{PROJECT_DIR}/#{PROJECT_NAME}/SupportingFiles")
  FileUtils.mkdir_p("#{PROJECT_DIR}/#{PROJECT_NAME}/Sources")
end

def write_file(path, content)
  File.open(path, "w") { |f| f.write(content) }
end

def copy_sources
  src_files = Dir.glob("#{SRC_DIR}/*.{cpp,h,hpp,c}")
  src_files.each do |src|
    FileUtils.cp(src, "#{PROJECT_DIR}/#{PROJECT_NAME}/Sources/")
  end
end

def create_files
  write_file("#{PROJECT_DIR}/#{PROJECT_NAME}/AppDelegate.h", APP_DELEGATE_H)
  write_file("#{PROJECT_DIR}/#{PROJECT_NAME}/AppDelegate.m", APP_DELEGATE_M)
  write_file("#{PROJECT_DIR}/#{PROJECT_NAME}/main.m", MAIN_M)
  write_file("#{PROJECT_DIR}/#{PROJECT_NAME}/SupportingFiles/Info.plist", INFO_PLIST)
  write_file("#{PROJECT_DIR}/#{PROJECT_NAME}/SupportingFiles/#{PROJECT_NAME}.pch", PCH)
  write_file("#{PROJECT_DIR}/Podfile", PODFILE)
end

def print_next_steps
  puts "\nProject scaffold created at: #{PROJECT_DIR}/"
  puts "Next steps:"
  puts "1. Open Xcode and create a new iOS project (or use the generated structure)."
  puts "2. Add SDL2 and SDL2_image frameworks for iOS to your project (see SDL2 iOS docs)."
  puts "3. Add your C++ sources from Sources/ to the Xcode project."
  puts "4. Set up bridging for C++ (Objective-C++ .mm files) if needed."
  puts "5. Adjust build settings for C++/SDL2/ImGui as required."
  puts "6. Run 'pod install' if using CocoaPods for SDL2."
  puts "7. Build and run on an iOS device or simulator."
end

create_project_structure
create_files
copy_sources
print_next_steps
