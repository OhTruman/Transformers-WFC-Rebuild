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
    "${CMAKE_SOURCE_DIR}/src/frontend/*.cpp"
    "${CMAKE_SOURCE_DIR}/src/ui/*.cpp"
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

target_compile_options(wfc_rebuild PRIVATE -Wall -Wextra -Wno-unused-parameter)

if(WIN32)
    target_compile_definitions(wfc_rebuild PRIVATE UNICODE _UNICODE)
    # Static-link the C++/unwind/pthread runtimes so the exe is self-contained
    # (no llvm-mingw DLLs required beside it).
    target_link_options(wfc_rebuild PRIVATE -static)
    target_link_libraries(wfc_rebuild PRIVATE opengl32 gdi32 gdiplus user32 xinput winmm)
endif()

set_target_properties(wfc_rebuild PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# Headless frontend / game-flow tests (docs/FRONTEND.md): catalog, URLs, UI controller, boot -> match -> return.
file(GLOB FRONTEND_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/frontend/*.cpp")
add_executable(wfc_frontend_tests ${CMAKE_SOURCE_DIR}/tests/FrontendTests.cpp ${FRONTEND_SOURCES}
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
add_executable(wfc_gfxdump ${CMAKE_SOURCE_DIR}/tools/frontend/GfxDump.cpp ${GFX_SOURCES} ${FRONTEND_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/ui/GfxHost.cpp ${CMAKE_SOURCE_DIR}/src/core/Log.cpp)
target_include_directories(wfc_gfxdump PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_options(wfc_gfxdump PRIVATE -Wall -Wextra -Wno-unused-parameter)
if(WIN32)
    target_link_options(wfc_gfxdump PRIVATE -static)
endif()
set_target_properties(wfc_gfxdump PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
