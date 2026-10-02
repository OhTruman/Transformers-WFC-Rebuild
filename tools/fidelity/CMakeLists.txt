# wfc_fidelity: windowless fidelity regression/measurement harness.
# Links the platform-independent gameplay + asset code only (no renderer, window or audio).
add_executable(wfc_fidelity
    main.cpp
    Checks.cpp
    PlaytestChecks.cpp
    M03Checks.cpp
    Rig.cpp
    WorldStub.cpp
    ${CMAKE_SOURCE_DIR}/src/game/Character.cpp
    ${CMAKE_SOURCE_DIR}/src/game/CharacterMovement.cpp
    ${CMAKE_SOURCE_DIR}/src/game/Collision.cpp
    ${CMAKE_SOURCE_DIR}/src/game/PlayerController.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/SkinnedModel.cpp
    ${CMAKE_SOURCE_DIR}/src/assets/Gltf.cpp
    ${CMAKE_SOURCE_DIR}/src/core/Log.cpp
)
target_include_directories(wfc_fidelity PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_compile_options(wfc_fidelity PRIVATE -Wall -Wextra -Wno-unused-parameter)
if(WIN32)
    target_link_options(wfc_fidelity PRIVATE -static)
endif()
set_target_properties(wfc_fidelity PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

add_test(NAME fidelity COMMAND wfc_fidelity WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")

# Measurement builds (opt-in: -DWFC_BUILD_MEASURE=ON). Each compiles the UNMODIFIED product
# sources plus one instrumentation file; see tools/fidelity/measure/.
#   wfc_rebuild_prof      + in-process sampling profiler (WFC_PROF=<out.txt>)
#   wfc_rebuild_audiospy  audio backend replaced by a recorder (WFC_AUDIOSPY=<out.txt>)
option(WFC_BUILD_MEASURE "Build the wfc_rebuild measurement variants" OFF)
if(WFC_BUILD_MEASURE AND WIN32)
    set(_wfc_product ${CMAKE_SOURCE_DIR}/src/main.cpp ${COMMON_SOURCES} ${PLATFORM_SOURCES})
    set(_wfc_noaudio ${_wfc_product})
    list(FILTER _wfc_noaudio EXCLUDE REGEX "Win32Audio\.cpp$")
    foreach(_t wfc_rebuild_prof wfc_rebuild_audiospy)
        if(_t STREQUAL "wfc_rebuild_prof")
            add_executable(${_t} ${_wfc_product} measure/Profiler.cpp)
        else()
            add_executable(${_t} ${_wfc_noaudio} measure/SpyAudio.cpp)
        endif()
        target_include_directories(${_t} PRIVATE "${CMAKE_SOURCE_DIR}/src")
        target_compile_options(${_t} PRIVATE -Wall -Wextra -Wno-unused-parameter)
        target_compile_definitions(${_t} PRIVATE UNICODE _UNICODE)
        target_link_options(${_t} PRIVATE -static)
        target_link_libraries(${_t} PRIVATE opengl32 gdi32 gdiplus user32 xinput winmm)
        set_target_properties(${_t} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    endforeach()
endif()
