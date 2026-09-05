target_compile_features(WorldBots PRIVATE cxx_std_20)
target_compile_definitions(WorldBots PRIVATE TRINITY_API_USE_DYNAMIC_LINKING)

option(WORLDBOTS_PARALLEL_BUILD "Enable parallel MSVC compilation for WorldBots" OFF)
set(WORLDBOTS_PARALLEL_JOBS "4" CACHE STRING
    "Maximum MSVC compiler processes for WorldBots (empty uses the host default)"
)
if(MSVC)
    # /FS keeps parallel compiler processes from racing on the shared PDB.
    # Keep this module-local and opt-in because TSWoW's generated PCH is very
    # large; parallel PCH mapping can be slower on legacy or constrained build
    # hosts. Four workers is the conservative cap when explicitly enabled.
    target_compile_options(WorldBots PRIVATE /FS)
    if(WORLDBOTS_PARALLEL_BUILD)
        if(WORLDBOTS_PARALLEL_JOBS STREQUAL "")
            target_compile_options(WorldBots PRIVATE /MP)
        elseif(WORLDBOTS_PARALLEL_JOBS MATCHES "^[1-9][0-9]*$")
            target_compile_options(WorldBots PRIVATE "/MP${WORLDBOTS_PARALLEL_JOBS}")
        else()
            message(FATAL_ERROR
                "WORLDBOTS_PARALLEL_JOBS must be empty or a positive integer"
            )
        endif()
    endif()

    # CMake disables incremental linking for RelWithDebInfo by default. It is
    # useful for the development configurations while leaving release output
    # and runtime compatibility unchanged.
    target_link_options(WorldBots PRIVATE
        "$<$<OR:$<CONFIG:Debug>,$<CONFIG:RelWithDebInfo>>:/INCREMENTAL>"
    )
endif()
target_sources(WorldBots PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}/Commands/BotCommands.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Commands/BotDiagnostics.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Sense/SelfSenseUpdater.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Sense/SpatialSenseUpdater.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Sense/PartySenseUpdater.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Sense/CombatSenseUpdater.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Sense/NavigationSenseUpdater.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Sense/InventorySenseUpdater.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Sense/QuestSenseUpdater.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Actions/GrindAction.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Actions/RevivePartyMemberAction.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Actions/TownRunAction.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Town/TownPlanning.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Testing/ScenarioRunner.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Diagnostics/BotTrace.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Diagnostics/ProgressMonitor.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Diagnostics/StructuredEventLog.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Diagnostics/SoakDigest.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Config/BotConfig.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Helper/TeleportUtils.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Helper/EquipmentUtils.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Helper/ProgressionUtils.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Helper/BotMaintenance.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Brain/GoalPolicy.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Brain/FailurePolicy.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Brain/QuestSelector.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Party/PartyCoordination.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Party/PartyCombat.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Party/PartyRecruitmentPolicy.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Travel/TravelGraph.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Travel/WorldTravel.cpp"
)

set(WORLDBOTS_MODULE_CONFIG_FILE "${CMAKE_CURRENT_LIST_DIR}/../config/worldbots.conf"
    CACHE FILEPATH "WorldBots runtime configuration file")
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
        "${CMAKE_CURRENT_LIST_DIR}/Travel/TravelGraph.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/Party/PartyCoordination.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/Party/PartyRecruitmentPolicy.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/Brain/FailurePolicy.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/Brain/GoalPolicy.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/Brain/QuestSelector.cpp"
        "${CMAKE_CURRENT_LIST_DIR}/Scheduler/Scheduler.cpp"
    )
    target_compile_features(WorldBotsLogicTests PRIVATE cxx_std_20)
    target_compile_definitions(WorldBotsLogicTests PRIVATE WORLDBOTS_LOGIC_TESTS)
    target_include_directories(WorldBotsLogicTests PRIVATE "${CMAKE_CURRENT_LIST_DIR}")
    enable_testing()
    add_test(NAME WorldBotsLogicTests COMMAND WorldBotsLogicTests)
endif()

# The generated MSBuild project does not reliably invalidate every action
# translation unit when this polymorphic base class changes. Mixing old and
# new object files produces incompatible vtables and can turn a valid virtual
# call into a null function-pointer call at runtime. Make the header a CMake
# configure dependency and place its content hash only on translation units
# that consume the action ABI. Keeping the definition source-scoped prevents
# an action-interface edit from rebuilding the module PCH and unrelated code.
set(WORLDBOTS_ACTION_ABI_HEADER "${CMAKE_CURRENT_LIST_DIR}/Actions/BotAction.h")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${WORLDBOTS_ACTION_ABI_HEADER}")
file(SHA256 "${WORLDBOTS_ACTION_ABI_HEADER}" WORLDBOTS_ACTION_ABI_HASH)
file(GLOB WORLDBOTS_ACTION_ABI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_LIST_DIR}/Actions/*.cpp"
)
list(APPEND WORLDBOTS_ACTION_ABI_SOURCES
    "${CMAKE_CURRENT_LIST_DIR}/Commands/BotCommands.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Commands/BotDiagnostics.cpp"
    "${CMAKE_CURRENT_LIST_DIR}/Core/CoreLogic.cpp"
)
set_property(SOURCE ${WORLDBOTS_ACTION_ABI_SOURCES} APPEND PROPERTY
    COMPILE_DEFINITIONS "WORLDBOTS_ACTION_ABI_${WORLDBOTS_ACTION_ABI_HASH}=1"
)

if(NOT DEFINED TSWOW_ROOT)
    if(DEFINED ENV{TSWOW_ROOT})
        set(TSWOW_ROOT "$ENV{TSWOW_ROOT}")
    else()
        get_filename_component(TSWOW_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
    endif()
endif()

set(WORLDBOTS_BOOST_INCLUDE_DIR "" CACHE PATH
    "Boost include directory used by the WorldBots native module"
)
if(NOT WORLDBOTS_BOOST_INCLUDE_DIR)
    if(DEFINED Boost_INCLUDE_DIR AND IS_DIRECTORY "${Boost_INCLUDE_DIR}")
        set(WORLDBOTS_BOOST_INCLUDE_DIR "${Boost_INCLUDE_DIR}")
    elseif(DEFINED BOOST_ROOT AND IS_DIRECTORY "${BOOST_ROOT}")
        set(WORLDBOTS_BOOST_INCLUDE_DIR "${BOOST_ROOT}")
    elseif(DEFINED ENV{BOOST_ROOT} AND IS_DIRECTORY "$ENV{BOOST_ROOT}")
        set(WORLDBOTS_BOOST_INCLUDE_DIR "$ENV{BOOST_ROOT}")
    else()
        file(GLOB WORLDBOTS_BOOST_CANDIDATES LIST_DIRECTORIES true
            "${TSWOW_ROOT}/tswow-build/boost/boost_*"
        )
        list(SORT WORLDBOTS_BOOST_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
        foreach(WORLDBOTS_BOOST_CANDIDATE IN LISTS WORLDBOTS_BOOST_CANDIDATES)
            if(IS_DIRECTORY "${WORLDBOTS_BOOST_CANDIDATE}" AND
                EXISTS "${WORLDBOTS_BOOST_CANDIDATE}/boost/version.hpp")
                set(WORLDBOTS_BOOST_INCLUDE_DIR "${WORLDBOTS_BOOST_CANDIDATE}")
                break()
            endif()
        endforeach()
    endif()
endif()
if(NOT IS_DIRECTORY "${WORLDBOTS_BOOST_INCLUDE_DIR}")
    message(FATAL_ERROR
        "WorldBots could not locate Boost. Set WORLDBOTS_BOOST_INCLUDE_DIR to its include directory."
    )
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
    "${WORLDBOTS_BOOST_INCLUDE_DIR}"
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
    "${CMAKE_CURRENT_SOURCE_DIR}/Sense"
    "${CMAKE_CURRENT_SOURCE_DIR}/Blackboard"
    "${CMAKE_CURRENT_SOURCE_DIR}/Commands"
    "${CMAKE_CURRENT_SOURCE_DIR}/Cache"
    "${CMAKE_CURRENT_SOURCE_DIR}/Factory"
    "${CMAKE_CURRENT_SOURCE_DIR}/Config"
    "${CMAKE_CURRENT_SOURCE_DIR}/Town"
    "${CMAKE_CURRENT_SOURCE_DIR}/Testing"
    "${CMAKE_CURRENT_SOURCE_DIR}/Diagnostics"
    "${CMAKE_CURRENT_SOURCE_DIR}/Travel"
    "${CMAKE_CURRENT_SOURCE_DIR}/Party"
    "${CMAKE_CURRENT_SOURCE_DIR}/Combat"
    "${CMAKE_CURRENT_SOURCE_DIR}/Combat/ClassStrategies"
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
