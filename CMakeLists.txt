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
# DLSS package builds (Systems' build_slim --dlss configures -DWFC_DLSS_PACKAGE=ON): the NVIDIA DLSS credit line on the
# start-up movie and the credits movie (src/frontend/PackageCredits.h). OFF: no credit line, the original frontend.
option(WFC_DLSS_PACKAGE "Show the NVIDIA DLSS credit (DLSS package builds)" OFF)
if(WFC_DLSS_PACKAGE)
    add_compile_definitions(WFC_DLSS_PACKAGE=1)
else()
    add_compile_definitions(WFC_DLSS_PACKAGE=0)
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
    "${CMAKE_SOURCE_DIR}/src/net/*.cpp"
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
# Optional FSR 3.1 (M5 B, src/render/d3d12/D3D12Upscaler.cpp): AMD FFX API headers + signed DLLs fetched by
# tools/render/fetch_ffx_sdk.py into third_party/ffx_sdk (not committed: AMD binary licence); without them the
# upscaler builds as an unavailable stub
if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/third_party/ffx_sdk/api/include/ffx_api.h")
    target_include_directories(wfc_rebuild PRIVATE "${CMAKE_SOURCE_DIR}/third_party/ffx_sdk/api/include"
        "${CMAKE_SOURCE_DIR}/third_party/ffx_sdk/upscalers/include" "${CMAKE_SOURCE_DIR}/third_party/ffx_sdk/framegeneration/include")
    target_compile_definitions(wfc_rebuild PRIVATE WFC_FFX_SDK=1 _WINDOWS)   # (_WINDOWS: ffx_api_loader.h platform check)
    add_custom_command(TARGET wfc_rebuild POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/ffx_sdk/bin/amd_fidelityfx_loader_dx12.dll"
        "${CMAKE_SOURCE_DIR}/third_party/ffx_sdk/bin/amd_fidelityfx_upscaler_dx12.dll"
        "${CMAKE_SOURCE_DIR}/third_party/ffx_sdk/bin/amd_fidelityfx_framegeneration_dx12.dll"
        "${CMAKE_SOURCE_DIR}/third_party/ffx_sdk/license.md" $<TARGET_FILE_DIR:wfc_rebuild>)
endif()
# D3D12 Agility SDK runtime (tools/render/fetch_d3d12_agility.py, not committed): FSR 3.1 needs shader model 6.6;
# the exe exports D3D12SDKVersion / D3D12SDKPath (src/render/d3d12/D3D12Agility.cpp) and D3D12Core.dll goes to bin/D3D12
if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/third_party/d3d12_agility/bin/D3D12Core.dll")
    target_compile_definitions(wfc_rebuild PRIVATE WFC_D3D12_AGILITY=614)
    add_custom_command(TARGET wfc_rebuild POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:wfc_rebuild>/D3D12"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/third_party/d3d12_agility/bin/D3D12Core.dll"
            "${CMAKE_SOURCE_DIR}/third_party/d3d12_agility/bin/d3d12SDKLayers.dll" "$<TARGET_FILE_DIR:wfc_rebuild>/D3D12")
endif()

# Ray-traced D3D12 passes (M7, PC ADAPTATION, off by default): HLSL -> DXIL at build time with the pinned DXC
# (tools/render/fetch_dxc.py, not committed), embedded as headers; nothing of DXC ships. Without DXC: no RT passes.
if(WIN32 AND EXISTS "${CMAKE_SOURCE_DIR}/third_party/dxc/bin/dxc.exe")
    set(WFC_RT_SHADER_DIR "${CMAKE_BINARY_DIR}/generated/rt")
    set(WFC_RT_SHADER_HEADERS "")
    foreach(sh rt_depthcheck rt_shadow_ao rt_ao_filter rt_reflect rt_shadow_soft rt_volumetric)
        set(out "${WFC_RT_SHADER_DIR}/${sh}.h")
        add_custom_command(OUTPUT "${out}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${WFC_RT_SHADER_DIR}"
            COMMAND "${CMAKE_SOURCE_DIR}/third_party/dxc/bin/dxc.exe" -nologo -T cs_6_5 -E main -O3 -Fh "${out}" -Vn g_${sh}
                "${CMAKE_SOURCE_DIR}/src/render/d3d12/shaders/${sh}.hlsl"
            DEPENDS "${CMAKE_SOURCE_DIR}/src/render/d3d12/shaders/${sh}.hlsl" "${CMAKE_SOURCE_DIR}/src/render/d3d12/shaders/rt_common.hlsli"
            COMMENT "DXC ${sh}.hlsl")
        list(APPEND WFC_RT_SHADER_HEADERS "${out}")
    endforeach()
    # R4: the runtime ray-traced lightmap - the texel tracer and its gutter copy (one source, GI_DILATE)
    foreach(pair "rt_gi:" "rt_gi_dilate:-DGI_DILATE" "rt_gi_probe:-DGI_PROBE")
        string(REPLACE ":" ";" kv "${pair}")
        list(GET kv 0 sh)
        list(LENGTH kv kvn)
        set(def "")
        if(kvn GREATER 1)
            list(GET kv 1 def)
        endif()
        set(out "${WFC_RT_SHADER_DIR}/${sh}.h")
        add_custom_command(OUTPUT "${out}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${WFC_RT_SHADER_DIR}"
            COMMAND "${CMAKE_SOURCE_DIR}/third_party/dxc/bin/dxc.exe" -nologo -T cs_6_5 -E main -O3 ${def} -Fh "${out}" -Vn g_${sh}
                "${CMAKE_SOURCE_DIR}/src/render/d3d12/shaders/rt_gi.hlsl"
            DEPENDS "${CMAKE_SOURCE_DIR}/src/render/d3d12/shaders/rt_gi.hlsl" "${CMAKE_SOURCE_DIR}/src/render/d3d12/shaders/rt_common.hlsli"
            COMMENT "DXC rt_gi.hlsl ${def}")
        list(APPEND WFC_RT_SHADER_HEADERS "${out}")
    endforeach()
    target_sources(wfc_rebuild PRIVATE ${WFC_RT_SHADER_HEADERS})
    target_include_directories(wfc_rebuild PRIVATE "${WFC_RT_SHADER_DIR}")
    target_compile_definitions(wfc_rebuild PRIVATE WFC_RT_SHADERS=1)
    # the FidelityFX shadow denoiser's five passes (tools/render/fetch_ffx_denoiser.py, SDK v1.1.4, MIT), cs_6_6 with
    # their embedded root signatures (render/d3d12/D3D12RtDenoise.cpp)
    set(FFXDN "${CMAKE_SOURCE_DIR}/third_party/ffx_denoiser")
    if(EXISTS "${FFXDN}/VERSION.txt")
        set(WFC_DN_HEADERS "")
        foreach(pair "prepare:ffx_denoiser_prepare_shadow_mask_pass" "tile:ffx_denoiser_shadows_tile_classification_pass"
                     "filter0:ffx_denoiser_filter_soft_shadows_0_pass" "filter1:ffx_denoiser_filter_soft_shadows_1_pass"
                     "filter2:ffx_denoiser_filter_soft_shadows_2_pass")
            string(REPLACE ":" ";" kv "${pair}")
            list(GET kv 0 short)
            list(GET kv 1 src)
            set(out "${WFC_RT_SHADER_DIR}/ffx_dn_${short}.h")
            add_custom_command(OUTPUT "${out}"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${WFC_RT_SHADER_DIR}"
                COMMAND "${CMAKE_SOURCE_DIR}/third_party/dxc/bin/dxc.exe" -nologo -T cs_6_6 -E CS -O3 -DFFX_GPU=1 -DFFX_HLSL=1 -DFFX_HALF=1 -enable-16bit-types -DFFX_DENOISER_SHADOWS_EMBED_ROOTSIG=1
                    -I "${FFXDN}/gpu" -Fh "${out}" -Vn g_ffx_dn_${short} "${FFXDN}/passes/${src}.hlsl"
                DEPENDS "${FFXDN}/passes/${src}.hlsl"
                COMMENT "DXC ${src}.hlsl")
            list(APPEND WFC_DN_HEADERS "${out}")
        endforeach()
        target_sources(wfc_rebuild PRIVATE ${WFC_DN_HEADERS})
        target_compile_definitions(wfc_rebuild PRIVATE WFC_RT_DENOISER=1)
    endif()
endif()
target_compile_options(wfc_rebuild PRIVATE -Wall -Wextra -Wno-unused-parameter)

if(WIN32)
    target_compile_definitions(wfc_rebuild PRIVATE UNICODE _UNICODE)
    # Static-link the C++/unwind/pthread runtimes so the exe is self-contained
    # (no llvm-mingw DLLs required beside it).
    # The linker map lands beside the exe: the crash report names its frames from it (RenderWatchdog.cpp).
    target_link_options(wfc_rebuild PRIVATE -static "-Wl,-Map=${CMAKE_BINARY_DIR}/bin/wfc_rebuild.map")
    target_link_libraries(wfc_rebuild PRIVATE opengl32 gdi32 gdiplus user32 xinput winmm mfplat mfreadwrite mfuuid ole32 ws2_32)   # ws2_32: [Systems] net
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
# [Systems] Opus (voice chat, ONLINE_MP_ARCHITECTURE.md section 7; BSD-3-Clause, third_party/opus/COPYING in the credits):
# tools/net/fetch_opus.py puts the official 1.5.2 source in third_party/opus (gitignored). Present: built static, WFC_OPUS=1;
# absent: the build continues and voice send / receive reports "voice unavailable".
if(EXISTS "${CMAKE_SOURCE_DIR}/third_party/opus/CMakeLists.txt")
    set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_PKG_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    set(OPUS_INSTALL_CMAKE_CONFIG_MODULE OFF CACHE BOOL "" FORCE)
    add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/opus" "${CMAKE_BINARY_DIR}/opus" EXCLUDE_FROM_ALL)
    target_link_libraries(wfc_rebuild PRIVATE opus)
    target_compile_definitions(wfc_rebuild PRIVATE WFC_OPUS=1)
    # BSD-3-Clause: the copyright notice + licence ship with the binary, next to the exe (as vgmstream's licences).
    add_custom_command(TARGET wfc_rebuild POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:wfc_rebuild>/licenses/opus"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_SOURCE_DIR}/third_party/opus/COPYING" "$<TARGET_FILE_DIR:wfc_rebuild>/licenses/opus")
else()
    message(STATUS "Opus not fetched (tools/net/fetch_opus.py): voice chat unavailable in this build")
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
add_executable(wfc_frontend_tests ${CMAKE_SOURCE_DIR}/tests/FrontendTests.cpp ${FRONTEND_SOURCES} ${PLATFORM_COMMON_SOURCES} ${CMAKE_SOURCE_DIR}/src/platform/win32/LongPath.cpp
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
add_executable(wfc_gfxdump ${CMAKE_SOURCE_DIR}/tools/frontend/GfxDump.cpp ${GFX_SOURCES} ${FRONTEND_SOURCES} ${PLATFORM_COMMON_SOURCES} ${CMAKE_SOURCE_DIR}/src/platform/win32/LongPath.cpp
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
                   ${CMAKE_SOURCE_DIR}/src/platform/win32/Win32FileCompression.cpp
                   ${CMAKE_SOURCE_DIR}/src/platform/win32/LongPath.cpp)
    target_include_directories(wfc_compression_tests PRIVATE ${CMAKE_SOURCE_DIR}/src)
    set_target_properties(wfc_compression_tests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    target_link_options(wfc_compression_tests PRIVATE -static)
    enable_testing()
    add_test(NAME compression COMMAND wfc_compression_tests "${CMAKE_BINARY_DIR}")
endif()
