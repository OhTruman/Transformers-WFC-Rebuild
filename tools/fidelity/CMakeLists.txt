# wfc_fidelity: windowless fidelity regression/measurement harness.
# Links the platform-independent gameplay + asset code only (no renderer, window or audio).
add_executable(wfc_fidelity
    main.cpp
    Checks.cpp
    PlaytestChecks.cpp
    M03Checks.cpp
    M03P2Checks.cpp
    NativeVehicleChecks.cpp
    Rig.cpp
    Access.cpp
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
# sources and swaps or adds instrumentation files from tools/fidelity/measure/:
#   wfc_rebuild_prof      + in-process sampling profiler (WFC_PROF=<out.txt>); real clock, real audio
#   wfc_rebuild_observe   lockstep clock (Time.cpp -> LockstepClock.cpp: exactly one fixed step per
#                         frame, deterministic) + frame grabber (WFC_GRAB/WFC_GRAB_DIR) + recording
#                         audio backend (Win32Audio.cpp -> SpyAudio.cpp, WFC_AUDIOSPY=<out.txt>)
#   wfc_rebuild_count     lockstep clock, real audio; selected subsystems compiled with -finstrument-functions
#                         and measure/CallCounter.cpp: exact per-frame call counts + inclusive time
#                         (WFC_COUNT=<out.txt>)
option(WFC_BUILD_MEASURE "Build the wfc_rebuild measurement variants" OFF)
if(WFC_BUILD_MEASURE AND WIN32)
    set(_wfc_product ${CMAKE_SOURCE_DIR}/src/main.cpp ${COMMON_SOURCES} ${PLATFORM_SOURCES})
    set(_wfc_observe ${_wfc_product})
    list(FILTER _wfc_observe EXCLUDE REGEX "(Win32Audio|/core/Time)\.cpp$")
    # Counted subsystems: collision, gameplay world/FX, character skinning, the WFC render pipeline.
    set(_wfc_counted ${_wfc_product})
    list(FILTER _wfc_counted INCLUDE REGEX "/src/(game/(Collision|World|Character|WeaponFx|VehicleFx|Particles?|SoundCues|RobotFoley)[A-Za-z]*|render/gl/[A-Za-z]+|assets/SkinnedModel)\.cpp$")
    set(_wfc_uncounted ${_wfc_product})
    list(REMOVE_ITEM _wfc_uncounted ${_wfc_counted})
    list(FILTER _wfc_uncounted EXCLUDE REGEX "/core/Time\.cpp$")
    add_library(_wfc_counted_objs OBJECT ${_wfc_counted})
    target_include_directories(_wfc_counted_objs PRIVATE "${CMAKE_SOURCE_DIR}/src")
    target_compile_definitions(_wfc_counted_objs PRIVATE UNICODE _UNICODE)
    target_compile_options(_wfc_counted_objs PRIVATE -Wno-unused-parameter -finstrument-functions-after-inlining)
    foreach(_t wfc_rebuild_prof wfc_rebuild_observe wfc_rebuild_count)
        if(_t STREQUAL "wfc_rebuild_prof")
            add_executable(${_t} ${_wfc_product} measure/Profiler.cpp)
        elseif(_t STREQUAL "wfc_rebuild_observe")
            add_executable(${_t} ${_wfc_observe} measure/SpyAudio.cpp measure/LockstepClock.cpp)
        else()
            add_executable(${_t} ${_wfc_uncounted} $<TARGET_OBJECTS:_wfc_counted_objs> measure/CallCounter.cpp measure/LockstepClock.cpp)
        endif()
        target_include_directories(${_t} PRIVATE "${CMAKE_SOURCE_DIR}/src")
        target_compile_options(${_t} PRIVATE -Wall -Wextra -Wno-unused-parameter)
        target_compile_definitions(${_t} PRIVATE UNICODE _UNICODE)
        target_link_options(${_t} PRIVATE -static)
        target_link_libraries(${_t} PRIVATE opengl32 gdi32 gdiplus user32 xinput winmm)
        set_target_properties(${_t} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    endforeach()
endif()
