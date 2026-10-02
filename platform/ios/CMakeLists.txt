# iPadOS / iOS platform integration.
#
# Included by Main/CMakeLists.txt when building for iOS (USC_IOS). It
#   * compiles the platform layer (sandbox paths, on-screen touch controller)
#     into the game executable,
#   * turns that executable into an application bundle and
#   * copies the game data that ships with the app into the bundle resources.
#
# It is included from the directory that creates the usc-game target because it
# extends that target.
#
# The dependencies themselves (SDL2, freetype, libvorbis, libarchive, ...) come
# from vcpkg's arm64-ios / x64-ios triplets; see platform/ios/README.md.

set(USC_IOS_DIR ${CMAKE_CURRENT_LIST_DIR})
set(USC_IOS_SRC_DIR ${USC_IOS_DIR}/src)

if(NOT USC_IOS_BUNDLE_ID)
    set(USC_IOS_BUNDLE_ID "me.drewol.usc" CACHE STRING "Bundle identifier used for the iOS app")
endif()
if(NOT USC_IOS_TEAM_ID)
    set(USC_IOS_TEAM_ID "" CACHE STRING "Apple development team id used for code signing")
endif()

file(GLOB USC_IOS_SOURCES
    "${USC_IOS_SRC_DIR}/*.cpp"
    "${USC_IOS_SRC_DIR}/*.mm"
    "${USC_IOS_SRC_DIR}/*.c"
)
file(GLOB USC_IOS_HEADERS
    "${USC_IOS_SRC_DIR}/*.h"
    "${USC_IOS_SRC_DIR}/*.hpp"
)

target_sources(usc-game PRIVATE ${USC_IOS_SOURCES} ${USC_IOS_HEADERS})
target_include_directories(usc-game PRIVATE ${USC_IOS_SRC_DIR})

# The platform sources include the game precompiled header as "stdafx.h"; Main/ is
# not on the include path for sources outside of Main/src.
target_include_directories(usc-game PRIVATE ${CMAKE_SOURCE_DIR}/Main)

# --- Application bundle ------------------------------------------------------

set_target_properties(usc-game PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${USC_IOS_DIR}/Info.plist"
    MACOSX_BUNDLE_BUNDLE_NAME "USC"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${USC_IOS_BUNDLE_ID}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"

    # iPad (2) and iPhone (1); remove the 1 to ship an iPad-only app.
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
    XCODE_ATTRIBUTE_IPHONEOS_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}"
    XCODE_ATTRIBUTE_ENABLE_BITCODE "NO"

    # The app is built unsigned: a re-signing tool (AltStore, Sideloadly, iOS App
    # Signer, TrollStore) replaces the signature before it is installed, and the
    # CI runner has no certificate anyway. To build a signed app, pass
    # -DUSC_IOS_TEAM_ID=<team> and override these settings on the xcodebuild
    # command line (CODE_SIGNING_ALLOWED=YES CODE_SIGN_IDENTITY="iPhone Developer").
    XCODE_ATTRIBUTE_CODE_SIGN_STYLE "Automatic"
    XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
    XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${USC_IOS_TEAM_ID}"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "${USC_IOS_BUNDLE_ID}"
    XCODE_ATTRIBUTE_GCC_C_LANGUAGE_STANDARD "c11"
    XCODE_ATTRIBUTE_CLANG_CXX_LANGUAGE_STANDARD "c++17"
    XCODE_ATTRIBUTE_CLANG_CXX_LIBRARY "libc++"
)

# Game data that is read at runtime but never written: folded into the bundle and
# copied into the writable game directory on first launch
# (see iOSPlatform::InstallGameDataIfNeeded).
set(USC_IOS_BUNDLED_DATA skins fonts audio)
foreach(dataDir ${USC_IOS_BUNDLED_DATA})
    add_custom_command(TARGET usc-game POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_SOURCE_DIR}/bin/${dataDir}"
                "$<TARGET_BUNDLE_DIR:usc-game>/${dataDir}"
        COMMENT "Installing bundled game data: ${dataDir}"
        VERBATIM
    )
endforeach()

# --- System frameworks -------------------------------------------------------
#
# The game looks SDL2 up as a plain library, so the frameworks SDL2 links against
# have to be listed here as well.

set(USC_IOS_FRAMEWORKS
    Foundation
    UIKit
    CoreGraphics
    QuartzCore
    CoreAudio
    AudioToolbox
    AVFoundation
    CoreMedia
    CoreVideo
    CoreMotion
    CoreHaptics
    GameController
    CoreBluetooth
    OpenGLES
    Metal
)

foreach(framework ${USC_IOS_FRAMEWORKS})
    find_library(FRAMEWORK_${framework} ${framework})
    if(FRAMEWORK_${framework})
        # Plain signature: Main/CMakeLists.txt links usc-game the same way, and
        # the two signatures cannot be mixed on one target.
        target_link_libraries(usc-game ${FRAMEWORK_${framework}})
    endif()
endforeach()
