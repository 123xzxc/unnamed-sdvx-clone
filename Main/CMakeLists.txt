# Main Project

# Find files used for project

set(SRCROOT ${CMAKE_CURRENT_SOURCE_DIR}/src/)
set(INCROOT ${CMAKE_CURRENT_SOURCE_DIR}/include/)
set(PCHROOT ${CMAKE_CURRENT_SOURCE_DIR}/)

file(GLOB SRC "${SRCROOT}/*.cpp" "${SRCROOT}/*.hpp")
source_group("Source" FILES ${SRC})

file(GLOB INC "${INCROOT}/*.hpp")
source_group("Include" FILES ${INC})

file(GLOB AUDIO_SRC "${SRCROOT}/Audio/*.cpp" "${SRCROOT}/Audio/*.hpp")
source_group("Source\\Audio" FILES ${AUDIO_SRC})

file(GLOB AUDIO_INC "${INCROOT}/Audio/*.hpp")
source_group("Include\\Audio" FILES ${AUDIO_INC})

file(GLOB FAST_GUI_SRC "${SRCROOT}/FastGUI/*.cpp" "${SRCROOT}/FastGUI/*.hpp")
source_group("Source\\FastGUI" FILES ${FAST_GUI_SRC})

file(GLOB FAST_GUI_INC "${INCROOT}/FastGUI/*.hpp")
source_group("Include\\FastGUI" FILES ${FAST_GUI_INC})

file(GLOB NUK_INC "${INCROOT}/nuklear/*.h")
source_group("Include\\nuklear" FILES ${NUK_INC})

file(GLOB NUK_INC "${INCROOT}/nuklear/*.h")
source_group("Include\\nuklear" FILES ${NUK_INC})

file(GLOB LIGHT_INC "${INCROOT}/LightPlugin/*.h")
source_group("Include\\LightPlugin" FILES ${LIGHT_INC})

set(MAIN_SRC ${SRC} ${INC} ${AUDIO_SRC} ${AUDIO_INC} ${FAST_GUI_SRC} ${FAST_GUI_INC} ${NUK_INC} ${LIGHT_INC})

file(GLOB RESOURCE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/Resource.rc)
source_group("" FILES ${RESOURCE_SRC})

set(PCH_SRC ${PCHROOT}/stdafx.cpp)
set(PCH_INC ${PCHROOT}/stdafx.h)
set(PCH_FILES ${PCH_SRC} ${PCH_INC})
source_group("" FILES ${PCH_FILES})

# Compiler stuff
enable_precompiled_headers("${MAIN_SRC}" ${PCH_SRC})

include_directories(Audio GUI .)
if(USC_IOS)
    # The iOS build produces an application bundle (.app).
    #
    # The platform layer is listed here, at target creation time, instead of
    # being appended with target_sources() from platform/ios/CMakeLists.txt: the
    # Xcode generator only puts sources that exist when the target is created
    # into usc-game.LinkFileList, so sources added later are compiled but never
    # linked.
    file(GLOB USC_IOS_PLATFORM_SOURCES
        "${CMAKE_SOURCE_DIR}/platform/ios/src/*.cpp"
        "${CMAKE_SOURCE_DIR}/platform/ios/src/*.mm"
    )
    add_executable(usc-game MACOSX_BUNDLE ${MAIN_SRC} ${PCH_FILES} ${USC_IOS_PLATFORM_SOURCES})
else()
    add_executable(usc-game ${MAIN_SRC} ${RESOURCE_SRC} ${PCH_FILES})
endif()
target_include_directories(usc-game PRIVATE
    ${SRCROOT}
    ${SRCROOT}/GUI
    ${SRCROOT}/Audio
    ${INCROOT}
    ${INCROOT}/GUI
    ${INCROOT}/Audio
)
target_compile_features(usc-game PUBLIC cxx_std_17)
target_compile_definitions(usc-game PRIVATE VERSION_MINOR=${PROJECT_VERSION_MINOR})
target_compile_definitions(usc-game PRIVATE VERSION_MAJOR=${PROJECT_VERSION_MAJOR})
target_compile_definitions(usc-game PRIVATE VERSION_PATCH=${PROJECT_VERSION_PATCH})

if(USC_IOS)
    # SDL_main.h normally renames the application's main() to SDL_main(), and only
    # declares SDL_UIKitRunApp when it is included for this platform. Main.cpp
    # provides the whole entry point itself (see the comment there), so the header
    # is included by that file directly and no extra definition is needed there.
    # SDL_MAIN_AVAILABLE is deliberately NOT set: it would rename main() in every
    # translation unit that includes SDL.h, and Main.cpp wants both names.
    #
    # USC_IOS is repeated here, next to the sources that depend on it, so the
    # platform layer and the entry point stay selected even if the definition in
    # the top level CMakeLists.txt changes.
    target_compile_definitions(usc-game PRIVATE USC_IOS)
endif()

find_package (Git)
if(GIT_FOUND)
    target_compile_definitions(usc-game PRIVATE GIT_COMMIT=${GIT_DATE_HASH})
    execute_process(
      COMMAND git rev-parse --abbrev-ref HEAD
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      OUTPUT_VARIABLE GIT_BRANCH
      OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    target_compile_definitions(usc-game PRIVATE GIT_BRANCH="${GIT_BRANCH}")
else(GIT_FOUND)
    target_compile_definitions(usc-game PRIVATE GIT_COMMIT="")
    target_compile_definitions(usc-game PRIVATE GIT_BRANCH="master")
endif(GIT_FOUND)

# The bundle name has to match the executable name on iOS, so it keeps its plain
# name there instead of getting a _Debug/_Release suffix.
if(NOT USC_IOS)
    set_output_postfixes(usc-game)
else()
    set_target_properties(usc-game PROPERTIES DEBUG_POSTFIX "" RELEASE_POSTFIX "")
endif()
set_werror(usc-game)

if(MSVC)
	# Set debugging folder
    set_target_properties(usc-game PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}/bin")
    target_compile_options(usc-game PRIVATE /EHsc /MP)
	# Target subsystem on windows, enable multiprocess build and faster PDB gen
	set_target_properties(usc-game PROPERTIES LINK_FLAGS_DEBUG "/SUBSYSTEM:WINDOWS /DEBUG:FASTLINK")
	set_target_properties(usc-game PROPERTIES LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /DEBUG:FULL")
endif(MSVC)

# Dependencies
target_link_libraries(usc-game Shared)
target_link_libraries(usc-game Graphics)
target_link_libraries(usc-game Audio)
target_link_libraries(usc-game GUI)
target_link_libraries(usc-game Beatmap)
target_link_libraries(usc-game nanovg)
target_link_libraries(usc-game lua)
target_link_libraries(usc-game discord-rpc)
target_link_libraries(usc-game cpr)
target_link_libraries(usc-game nlohmann_json)

target_link_libraries(usc-game ${SDL2_LIBRARY})
target_link_libraries(usc-game ${LibArchive_LIBRARIES})
target_include_directories(usc-game SYSTEM PRIVATE ${LibArchive_INCLUDE_DIRS})

if(WIN32)
    target_link_libraries(usc-game
        optimized ${PROJECT_SOURCE_DIR}/third_party/breakpad/exception_handler_Release.lib
		optimized ${PROJECT_SOURCE_DIR}/third_party/breakpad/crash_generation_client_Release.lib
		optimized ${PROJECT_SOURCE_DIR}/third_party/breakpad/common_Release.lib
    )
    target_link_libraries(usc-game
        debug ${PROJECT_SOURCE_DIR}/third_party/breakpad/exception_handler_Debug.lib
		debug ${PROJECT_SOURCE_DIR}/third_party/breakpad/crash_generation_client_Debug.lib
		debug ${PROJECT_SOURCE_DIR}/third_party/breakpad/common_Debug.lib
    )
    target_include_directories(usc-game PUBLIC ${PROJECT_SOURCE_DIR}/third_party/breakpad/include)
endif()

find_package(ZLIB REQUIRED)
if(ZLIB_FOUND)
    target_compile_definitions(usc-game PRIVATE ZLIB_FOUND=${ZLIB_FOUND})
endif()

target_link_libraries(usc-game cc-common)

if(USC_IOS)
    # iOS platform layer (sandbox paths, on-screen touch controller), the
    # application bundle properties and the game data folded into the bundle.
    #
    # The file is included rather than added as a subdirectory: it extends the
    # usc-game target, which CMake only allows from the directory that created
    # the target (target_link_libraries needs CMP0079 NEW otherwise, and
    # add_custom_command(TARGET) is always rejected from another directory).
    include(${CMAKE_SOURCE_DIR}/platform/ios/CMakeLists.txt)
endif()
