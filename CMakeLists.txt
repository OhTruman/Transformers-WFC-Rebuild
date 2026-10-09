cmake_minimum_required(VERSION 3.20)
project(WFCRebuild CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_definitions(WFC_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
# Development tools (the DEBUG-ONLY QA panel, F10). ON for development builds; a shipping-style build configures
# -DWFC_DEV_TOOLS=OFF (build.ps1 -Shipping) and the panel is compiled out.
option(WFC_DEV_TOOLS "Compile the developer QA panel (F10)" ON)
if(WFC_DEV_TOOLS)
    add_compile_definitions(WFC_DEV_TOOLS=1)
else()
    add_compile_definitions(WFC_DEV_TOOLS=0)
endif()

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Gameplay/core/render/game sources are platform-independent.
file(GLOB_RECURSE COMMON_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/core/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/render/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/game/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/assets/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/frontend/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/*.cpp"
)

# Platform-neutral platform helpers (src/platform/*.cpp, not the per-OS subfolders).
file(GLOB PLATFORM_COMMON_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/platform/*.cpp")
list(APPEND COMMON_SOURCES ${PLATFORM_COMMON_SOURCES})

# Platform layer is selected per target OS.
if(WIN32)
    file(GLOB_RECURSE PLATFORM_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_SOURCE_DIR}/src/platform/win32/*.cpp")
else()
    set(PLATFORM_SOURCES "")
endif()

add_executable(wfc_rebuild
    ${CMAKE_SOURCE_DIR}/src/main.cpp
    ${COMMON_SOURCES}
    ${PLATFORM_SOURCES}
)

target_include_directories(wfc_rebuild PRIVATE "${CMAKE_SOURCE_DIR}/src")

target_compile_options(wfc_rebuild PRIVATE -Wall -Wextra -Wno-unused-parameter)

if(WIN32)
    target_compile_definitions(wfc_rebuild PRIVATE UNICODE _UNICODE)
    # Static-link the C++/unwind/pthread runtimes so the exe is self-contained
    # (no llvm-mingw DLLs required beside it).
    # The linker map lands beside the exe: the crash report names its frames from it (RenderWatchdog.cpp).
    target_link_options(wfc_rebuild PRIVATE -static "-Wl,-Map=${CMAKE_BINARY_DIR}/bin/wfc_rebuild.map")
    target_link_libraries(wfc_rebuild PRIVATE opengl32 gdi32 gdiplus user32 xinput winmm mfplat mfreadwrite mfuuid ole32)
endif()

set_target_properties(wfc_rebuild PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# [Systems, 300 fps] Release CPU, no change to what the game does: ThinLTO, and mimalloc as the C++ operator new / delete
# (src/core/AllocProf.cpp; WFC_ALLOCPROF still works on top). Both switchable for A/B (-DWFC_LTO=OFF / -DWFC_MIMALLOC=OFF).
option(WFC_LTO "ThinLTO for optimised builds (A/B neutral at 32 v 32; off by default)" OFF)
option(WFC_MIMALLOC "mimalloc as the C++ operator new / delete" ON)
if(WFC_LTO AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    target_compile_options(wfc_rebuild PRIVATE -flto=thin)
    target_link_options(wfc_rebuild PRIVATE -flto=thin "-Wl,--thinlto-cache-dir=${CMAKE_BINARY_DIR}/thinlto-cache")
endif()
if(WFC_MIMALLOC)
    enable_language(C)
    add_library(wfc_mimalloc STATIC "${CMAKE_SOURCE_DIR}/third_party/mimalloc/src/static.c")
    target_include_directories(wfc_mimalloc PUBLIC "${CMAKE_SOURCE_DIR}/third_party/mimalloc/include")
    target_compile_definitions(wfc_mimalloc PRIVATE MI_STATIC_LIB=1 $<$<NOT:$<CONFIG:Debug>>:NDEBUG>)
    target_compile_options(wfc_mimalloc PRIVATE -w)
    target_link_libraries(wfc_rebuild PRIVATE wfc_mimalloc)
    target_compile_definitions(wfc_rebuild PRIVATE WFC_MIMALLOC=1)
endif()

# [Systems] The original FMOD sound banks (content/**/X.fsb, XMA2) decoded in-process by libvgmstream r2117 with its FFmpeg DLLs
# (third_party/vgmstream/README.WFC.txt: source, versions, licences). The WAV beside a bank stays the fallback; at run time
# WFC_AUDIO_SOURCE=auto (WAV if present, else the bank) | fsb | wav. The four DLLs and the licence files are copied next to the exe.
option(WFC_VGMSTREAM "decode the original sound banks in-process (libvgmstream r2117)" ON)
if(WIN32 AND WFC_VGMSTREAM)
    enable_language(C)
    set(WFC_VGM "${CMAKE_SOURCE_DIR}/third_party/vgmstream")
    file(GLOB_RECURSE WFC_VGM_SOURCES CONFIGURE_DEPENDS "${WFC_VGM}/src/*.c")
    add_library(wfc_vgmstream STATIC ${WFC_VGM_SOURCES})
    target_include_directories(wfc_vgmstream PUBLIC "${WFC_VGM}/src" PRIVATE "${WFC_VGM}/ffmpeg_include")
    target_compile_definitions(wfc_vgmstream PRIVATE VGM_USE_FFMPEG)
    set_target_properties(wfc_vgmstream PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
    target_compile_options(wfc_vgmstream PRIVATE -w)
    get_filename_component(WFC_CC_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
    find_program(WFC_DLLTOOL NAMES llvm-dlltool dlltool HINTS "${WFC_CC_DIR}" REQUIRED)
    set(WFC_VGM_IMPORTS "")
    foreach(dll avcodec-vgmstream-59 avformat-vgmstream-59 avutil-vgmstream-57 swresample-vgmstream-4)
        set(imp "${CMAKE_BINARY_DIR}/vgmstream/lib${dll}.a")
        add_custom_command(OUTPUT "${imp}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/vgmstream"
            COMMAND "${WFC_DLLTOOL}" -m i386:x86-64 -d "${WFC_VGM}/ext_libs/${dll}.def" -D "${dll}.dll" -l "${imp}"
            DEPENDS "${WFC_VGM}/ext_libs/${dll}.def" VERBATIM)
        list(APPEND WFC_VGM_IMPORTS "${imp}")
        add_custom_command(TARGET wfc_rebuild POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${WFC_VGM}/ext_libs/dll-x64/${dll}.dll" "$<TARGET_FILE_DIR:wfc_rebuild>")
    endforeach()
    add_custom_target(wfc_vgmstream_imports DEPENDS ${WFC_VGM_IMPORTS})
    add_dependencies(wfc_rebuild wfc_vgmstream_imports)
    target_link_libraries(wfc_rebuild PRIVATE wfc_vgmstream ${WFC_VGM_IMPORTS})
    target_compile_definitions(wfc_rebuild PRIVATE WFC_VGMSTREAM=1)
    add_custom_command(TARGET wfc_rebuild POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:wfc_rebuild>/licenses/vgmstream"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${WFC_VGM}/COPYING" "${WFC_VGM}/README.WFC.txt"
                "${WFC_VGM}/licenses/ffmpeg.COPYING.LGPLv2.1" "${WFC_VGM}/licenses/ffmpeg.COPYING.LGPLv3"
                "$<TARGET_FILE_DIR:wfc_rebuild>/licenses/vgmstream")
endif()

# [Systems, 300 fps] PGO: -DWFC_PGO=gen builds the instrumented exe (training: tools/systems/pgo_train.sh); -DWFC_PGO=<path to
# .profdata> builds with the profile; empty = off. [integration 09c] Release defaults to tools/pgo/wfc.profdata when present (a stale
# profile only loses gain, never correctness). No behaviour change.
set(WFC_PGO_DEFAULT "")
if(CMAKE_BUILD_TYPE STREQUAL "Release" AND EXISTS "${CMAKE_SOURCE_DIR}/tools/pgo/wfc.profdata")
    set(WFC_PGO_DEFAULT "${CMAKE_SOURCE_DIR}/tools/pgo/wfc.profdata")
endif()
set(WFC_PGO "${WFC_PGO_DEFAULT}" CACHE STRING "PGO: 'gen' (instrumented) or a .profdata path (use); empty = off")
if(WFC_PGO STREQUAL "gen")
    target_compile_options(wfc_rebuild PRIVATE -fprofile-generate)
    target_link_options(wfc_rebuild PRIVATE -fprofile-generate)
elseif(WFC_PGO)
    target_compile_options(wfc_rebuild PRIVATE "-fprofile-use=${WFC_PGO}" -Wno-profile-instr-unprofiled
                           -Wno-profile-instr-out-of-date -Wno-backend-plugin)
endif()

# Headless frontend / game-flow tests (docs/FRONTEND.md): catalog, URLs, UI controller, boot -> match -> return.
file(GLOB FRONTEND_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/frontend/*.cpp")
add_executable(wfc_frontend_tests ${CMAKE_SOURCE_DIR}/tests/FrontendTests.cpp ${FRONTEND_SOURCES} ${PLATFORM_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/core/Log.cpp)
target_include_directories(wfc_frontend_tests PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_options(wfc_frontend_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
if(WIN32)
    target_link_options(wfc_frontend_tests PRIVATE -static)
endif()
set_target_properties(wfc_frontend_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
enable_testing()
add_test(NAME frontend COMMAND wfc_frontend_tests WORKING_DIRECTORY "${CMAKE_BINARY_DIR}")

# Fidelity regression/measurement harness (tools/fidelity/README.md).
option(WFC_BUILD_FIDELITY "Build the wfc_fidelity harness" ON)
if(WFC_BUILD_FIDELITY)
    enable_testing()
    add_subdirectory(tools/fidelity)
endif()

# GFx runtime headless dump (tools/frontend/GfxDump.cpp).
file(GLOB GFX_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/ui/gfx/*.cpp")
add_executable(wfc_gfxdump ${CMAKE_SOURCE_DIR}/tools/frontend/GfxDump.cpp ${GFX_SOURCES} ${FRONTEND_SOURCES} ${PLATFORM_COMMON_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/ui/GfxHost.cpp ${CMAKE_SOURCE_DIR}/src/core/Log.cpp)
target_include_directories(wfc_gfxdump PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_options(wfc_gfxdump PRIVATE -Wall -Wextra -Wno-unused-parameter)
if(WIN32)
    target_link_options(wfc_gfxdump PRIVATE -static)
endif()
set_target_properties(wfc_gfxdump PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
# Lossless file compression (platform/FileCompression.h): compress -> decompress byte-identical, twin reads.
if(WIN32)
    add_executable(wfc_compression_tests ${CMAKE_SOURCE_DIR}/tests/CompressionTests.cpp
                   ${CMAKE_SOURCE_DIR}/src/platform/win32/Win32FileCompression.cpp)
    target_include_directories(wfc_compression_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    set_target_properties(wfc_compression_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    target_link_options(wfc_compression_tests PRIVATE -static)
    enable_testing()
    add_test(NAME compression COMMAND wfc_compression_tests "${CMAKE_BINARY_DIR}")
endif()
