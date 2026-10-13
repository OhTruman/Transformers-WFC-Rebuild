cmake_minimum_required(VERSION 3.20)
project(WFCRebuild CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Gameplay/core/render/game sources are platform-independent.
file(GLOB_RECURSE COMMON_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/src/core/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/render/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/game/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/assets/*.cpp"
)

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
    foreach(sh rt_depthcheck rt_shadow_ao)
        set(out "${WFC_RT_SHADER_DIR}/${sh}.h")
        add_custom_command(OUTPUT "${out}"
            COMMAND ${CMAKE_COMMAND} -E make_directory "${WFC_RT_SHADER_DIR}"
            COMMAND "${CMAKE_SOURCE_DIR}/third_party/dxc/bin/dxc.exe" -nologo -T cs_6_5 -E main -O3 -Fh "${out}" -Vn g_${sh}
                "${CMAKE_SOURCE_DIR}/src/render/d3d12/shaders/${sh}.hlsl"
            DEPENDS "${CMAKE_SOURCE_DIR}/src/render/d3d12/shaders/${sh}.hlsl"
            COMMENT "DXC ${sh}.hlsl")
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
    target_link_libraries(wfc_rebuild PRIVATE opengl32 gdi32 gdiplus user32 xinput winmm)
endif()

set_target_properties(wfc_rebuild PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Fidelity regression/measurement harness (tools/fidelity/README.md).
option(WFC_BUILD_FIDELITY "Build the wfc_fidelity harness" ON)
if(WFC_BUILD_FIDELITY)
    enable_testing()
    add_subdirectory(tools/fidelity)
endif()

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
