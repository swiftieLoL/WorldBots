target_compile_features(WorldBots PRIVATE cxx_std_20)
target_compile_definitions(WorldBots PRIVATE TRINITY_API_USE_DYNAMIC_LINKING)
if(MSVC)
    # TSWoW enables parallel translation-unit compilation for this target.
    # Serialize writes to the shared compiler PDB to prevent C1041 failures.
    target_compile_options(WorldBots PRIVATE /FS)
endif()
target_sources(WorldBots PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/Actions/GrindAction.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Actions/TownRunAction.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Town/TownPlanning.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Testing/ScenarioRunner.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Diagnostics/BotTrace.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Config/BotConfig.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Helper/TeleportUtils.cpp"
)

set(WORLDBOTS_MODULE_CONFIG_FILE "${CMAKE_CURRENT_LIST_DIR}/../config/worldbots.conf")
file(TO_CMAKE_PATH "${WORLDBOTS_MODULE_CONFIG_FILE}" WORLDBOTS_MODULE_CONFIG_FILE)
target_compile_definitions(WorldBots PRIVATE
    "WORLDBOTS_MODULE_CONFIG_FILE=\"${WORLDBOTS_MODULE_CONFIG_FILE}\""
)

option(WORLDBOTS_BUILD_LOGIC_TESTS "Build WorldBots standalone logic tests" ON)
if(WORLDBOTS_BUILD_LOGIC_TESTS)
    add_executable(WorldBotsLogicTests EXCLUDE_FROM_ALL
        "${CMAKE_CURRENT_LIST_DIR}/../tests/WorldBotsLogicTests.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/Town/TownPlanning.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/Factory/BotRoster.cpp"
    )
    target_compile_features(WorldBotsLogicTests PRIVATE cxx_std_20)
    target_include_directories(WorldBotsLogicTests PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
    enable_testing()
    add_test(NAME WorldBotsLogicTests COMMAND WorldBotsLogicTests)
endif()

# The generated MSBuild project does not reliably invalidate every action
# translation unit when this polymorphic base class changes. Mixing old and
# new object files produces incompatible vtables and can turn a valid virtual
# call into a null function-pointer call at runtime. Make the header a CMake
# configure dependency and place its content hash on every compiler command;
# an ABI edit then forces one coherent rebuild of the entire module.
set(WORLDBOTS_ACTION_ABI_HEADER "${CMAKE_CURRENT_LIST_DIR}/Actions/BotAction.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${WORLDBOTS_ACTION_ABI_HEADER}")
file(SHA256 "${WORLDBOTS_ACTION_ABI_HEADER}" WORLDBOTS_ACTION_ABI_HASH)
target_compile_definitions(WorldBots PRIVATE "WORLDBOTS_ACTION_ABI_${WORLDBOTS_ACTION_ABI_HASH}=1")

if(NOT DEFINED TSWOW_ROOT)
    if(DEFINED ENV{TSWOW_ROOT})
        set(TSWOW_ROOT "$ENV{TSWOW_ROOT}")
    else()
        set(TSWOW_ROOT "E:/TSFresh")
    endif()
endif()

# Add TrinityCore source directories to include paths for WorldBots module
# This file is also the configure boundary for newly added native subsystem
# translation units discovered by TSWoW's generated source glob.
target_include_directories(WorldBots PRIVATE
    "${TSWOW_ROOT}/tswow/cores/TrinityCore/src/server/game"
    "${TSWOW_ROOT}/tswow/cores/TrinityCore/src/server/shared"
    "${TSWOW_ROOT}/tswow/cores/TrinityCore/src/common"
    "${TSWOW_ROOT}/tswow/cores/TrinityCore/src/server/database"
    "${TSWOW_ROOT}/tswow/cores/TrinityCore/dep/fmt/include"
    "${TSWOW_ROOT}/tswow-build/boost/boost_1_82_0"
    "${TSWOW_ROOT}/tswow/cores/TrinityCore/dep/recastnavigation/Detour/Include"
    "${TSWOW_ROOT}/tswow/cores/TrinityCore/dep/g3dlite/include"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/Auth"
    "${CMAKE_CURRENT_SOURCE_DIR}/Helper"
    "${CMAKE_CURRENT_SOURCE_DIR}/Core"
    "${CMAKE_CURRENT_SOURCE_DIR}/Scheduler"
    "${CMAKE_CURRENT_SOURCE_DIR}/Bindings"
    "${CMAKE_CURRENT_SOURCE_DIR}/Actions"
    "${CMAKE_CURRENT_SOURCE_DIR}/Brain"
    "${CMAKE_CURRENT_SOURCE_DIR}/Blackboard"
    "${CMAKE_CURRENT_SOURCE_DIR}/Commands"
    "${CMAKE_CURRENT_SOURCE_DIR}/Cache"
    "${CMAKE_CURRENT_SOURCE_DIR}/Factory"
    "${CMAKE_CURRENT_SOURCE_DIR}/Config"
    "${CMAKE_CURRENT_SOURCE_DIR}/Town"
    "${CMAKE_CURRENT_SOURCE_DIR}/Testing"
    "${CMAKE_CURRENT_SOURCE_DIR}/Diagnostics"
)

# Dynamically add all directories containing headers inside TrinityCore/src
file(GLOB_RECURSE TC_HEADERS "${TSWOW_ROOT}/tswow/cores/TrinityCore/src/*.h")
foreach(HEADER ${TC_HEADERS})
    get_filename_component(HEADER_DIR ${HEADER} DIRECTORY)
    list(APPEND TC_DIRS ${HEADER_DIR})
endforeach()
if(TC_DIRS)
    list(REMOVE_DUPLICATES TC_DIRS)
    target_include_directories(WorldBots PRIVATE ${TC_DIRS})
endif()
