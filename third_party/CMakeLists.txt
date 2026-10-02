# All third party libraries
add_subdirectory(lua-5.3.5)

#cpr test programs
option(CPR_BUILD_TESTS "Set to ON to build cpr tests." OFF)
set(CPR_ENABLE_SSL ON)
#TODO: Don't have two curl builds in the game build
#if(MSVC)
#    set(CPR_FORCE_USE_SYSTEM_CURL ON) #curl should be available through vcpkg
#endif(MSVC)
if(USC_IOS AND NOT USC_IOS_HTTP)
    # HTTP disabled for this iOS build: cpr is replaced by an API compatible shim
    # that fails every request, so no libcurl has to be built for iOS.
    add_library(cpr INTERFACE)
    target_include_directories(cpr INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/../platform/ios/stubs)
elseif(USC_IOS)
    # vcpkg provides cpr (and libcurl) for the iOS triplets as well.
    find_package(cpr CONFIG REQUIRED)
    add_library(cpr INTERFACE)
    target_link_libraries(cpr INTERFACE cpr::cpr)
    if(DEFINED cpr_INCLUDE_DIRS)
        target_include_directories(cpr INTERFACE ${cpr_INCLUDE_DIRS})
    endif()
elseif(USE_SYSTEM_CPR)
    find_package(CPR REQUIRED)
    add_library(cpr INTERFACE)
    target_include_directories(cpr INTERFACE ${CPR_INCLUDE_DIRS})
    target_link_libraries(cpr INTERFACE ${CPR_LIBRARIES})
else()
    add_subdirectory(cpr)
    target_include_directories(cpr PUBLIC ${CPR_INCLUDE_DIRS})
endif(USE_SYSTEM_CPR)

#discord example program
set(CLANG_FORMAT_SUFFIX "ignore") #try to disable clang formatter
option(BUILD_EXAMPLES "Build example apps" OFF)
if(USC_IOS)
    # discord-rpc is a desktop IPC client; iOS only allows IPC through declared
    # app extensions, so the calls are stubbed out instead.
    add_library(discord-rpc STATIC
        ${CMAKE_CURRENT_SOURCE_DIR}/../platform/ios/stubs/discord_rpc_stub.cpp
    )
    target_include_directories(discord-rpc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../platform/ios/stubs)
else()
    add_subdirectory(discord-rpc)
    target_include_directories(discord-rpc PUBLIC discord-rpc/include)
endif()

#nanovg
add_library(nanovg nanovg/src/nanovg.c)
target_include_directories(nanovg PUBLIC nanovg/src)
target_link_libraries(nanovg ${FREETYPE_LIBRARIES})
target_include_directories(nanovg PRIVATE ${FREETYPE_INCLUDE_DIRS})
target_compile_definitions(nanovg PRIVATE -DFONS_USE_FREETYPE)

#miniaudio
add_library(miniaudio INTERFACE)
target_include_directories(miniaudio INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/miniaudio)

#json for modern c++
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/nlohmann_json)

#sqlite
add_library(sqlite3
    sqlite3/sqlite3.c
    sqlite3/sqlite3.h
    sqlite3/sqlite3ext.h
)
target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/sqlite3)

#rapidjson -- add as interface library to prevent building it
add_library(rapidjson INTERFACE)
target_include_directories(rapidjson INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/rapidjson)

#minimp3
add_library(minimp3
    minimp3/minimp3.c
    minimp3/minimp3.h
    minimp3/libc.h
)
target_include_directories(minimp3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/minimp3)

#soundtouch
add_library(soundtouch
    soundtouch/src/AAFilter.cpp
    soundtouch/src/AAFilter.h
    soundtouch/src/BPMDetect.cpp
    soundtouch/include/BPMDetect.h
    soundtouch/src/cpu_detect_x86.cpp
    soundtouch/src/cpu_detect.h
    soundtouch/src/FIFOSampleBuffer.cpp
    soundtouch/include/FIFOSampleBuffer.h
    soundtouch/include/FIFOSamplePipe.h
    soundtouch/src/FIRFilter.cpp
    soundtouch/src/FIRFilter.h
    soundtouch/src/InterpolateCubic.cpp
    soundtouch/src/InterpolateCubic.h
    soundtouch/src/InterpolateLinear.cpp
    soundtouch/src/InterpolateLinear.h
    soundtouch/src/InterpolateShannon.cpp
    soundtouch/src/InterpolateShannon.h
    soundtouch/src/mmx_optimized.cpp
    soundtouch/src/PeakFinder.cpp
    soundtouch/src/PeakFinder.h
    soundtouch/src/RateTransposer.cpp
    soundtouch/src/RateTransposer.cpp
    soundtouch/include/soundtouch_config.h
    soundtouch/src/SoundTouch.cpp
    soundtouch/include/SoundTouch.h
    soundtouch/include/STTypes.h
    soundtouch/src/sse_optimized.cpp
    soundtouch/src/TDStretch.cpp
    soundtouch/src/TDStretch.h
)
target_include_directories(soundtouch PUBLIC soundtouch/include)
target_include_directories(soundtouch PRIVATE soundtouch/src)

#GLEW
# OpenGL ES exposes its entry points directly, so GLEW is not needed on iOS.
if(NOT USC_IOS)
    add_library(GLEW
        glew/include/GL/glew.h
        glew/include/GL/glxew.h
        glew/include/GL/wglew.h
        glew/src/glew.c
    )
    # GLEW is included statically and also doesn't need GLU(Which doesn't even exist on linux)
    target_compile_definitions(GLEW PUBLIC -DGLEW_NO_GLU -DGLEW_STATIC)
    target_include_directories(GLEW PUBLIC glew/include)
endif()

# Enable multiprocess compiling
if(MSVC)
    target_compile_options(cpr PRIVATE /MP)
    target_compile_options(discord-rpc PRIVATE /MP)
    target_compile_options(GLEW PRIVATE /MP)
    target_compile_options(lua PRIVATE /MP)
    target_compile_options(minimp3 PRIVATE /MP)
    target_compile_options(nanovg PRIVATE /MP)
    target_compile_options(soundtouch PRIVATE /MP)
    target_compile_options(sqlite3 PRIVATE /MP)
endif(MSVC)
