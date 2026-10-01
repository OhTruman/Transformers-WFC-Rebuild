# wfc_fidelity: windowless fidelity regression/measurement harness.
# Links the platform-independent gameplay + asset code only (no renderer, window or audio).
add_executable(wfc_fidelity
    main.cpp
    Checks.cpp
    PlaytestChecks.cpp
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
