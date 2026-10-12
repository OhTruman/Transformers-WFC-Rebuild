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
        "${CMAKE_SOURCE_DIR}/third_party/ffx_sdk/upscalers/include")
    target_compile_definitions(wfc_rebuild PRIVATE WFC_FFX_SDK=1 _WINDOWS)   # (_WINDOWS: ffx_api_loader.h platform check)
    add_custom_command(TARGET wfc_rebuild POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/third_party/ffx_sdk/bin/amd_fidelityfx_loader_dx12.dll"
        "${CMAKE_SOURCE_DIR}/third_party/ffx_sdk/bin/amd_fidelityfx_upscaler_dx12.dll"
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
