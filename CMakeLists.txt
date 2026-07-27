cmake_minimum_required(VERSION 3.21)
project(GW2EventCalendar LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Guild Wars 2 / Nexus are 64-bit only - fail fast instead of shipping a
# DLL Nexus can load-fail or calling-convention-mismatch on.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "GW2EventCalendar must be built x64 - select the x64 CMake preset/configuration.")
endif()

find_package(nlohmann_json CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)

# Embed the fallback icon as a C++ byte array (see cmake/EmbedIcon.cmake) so
# it ships inside the DLL via Textures_GetOrCreateFromMemory - no loose-file
# deploy step. Regenerates whenever assets/fallback_calendar_icon.png changes.
set(FALLBACK_ICON_SRC "${CMAKE_SOURCE_DIR}/assets/fallback_calendar_icon.png")
set(GENERATED_DIR "${CMAKE_BINARY_DIR}/generated")
set(FALLBACK_ICON_HEADER "${GENERATED_DIR}/fallback_icon_data.h")
set(FALLBACK_ICON_CPP "${GENERATED_DIR}/fallback_icon_data.cpp")

add_custom_command(
    OUTPUT "${FALLBACK_ICON_HEADER}" "${FALLBACK_ICON_CPP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GENERATED_DIR}"
    COMMAND ${CMAKE_COMMAND}
        -DIN_FILE=${FALLBACK_ICON_SRC}
        -DOUT_HEADER=${FALLBACK_ICON_HEADER}
        -DOUT_CPP=${FALLBACK_ICON_CPP}
        -P "${CMAKE_SOURCE_DIR}/cmake/EmbedIcon.cmake"
    DEPENDS "${FALLBACK_ICON_SRC}" "${CMAKE_SOURCE_DIR}/cmake/EmbedIcon.cmake"
    COMMENT "Embedding fallback_calendar_icon.png as a C++ byte array"
)

add_library(GW2EventCalendar SHARED
    src/main.cpp
    src/settings.cpp
    src/wikidate.cpp
    src/icsfeed.cpp
    src/wikifeed.cpp
    src/eventfetcher.cpp
    src/calendarui.cpp
    vendor/imgui/imgui.cpp
    vendor/imgui/imgui_draw.cpp
    vendor/imgui/imgui_tables.cpp
    vendor/imgui/imgui_widgets.cpp
    "${FALLBACK_ICON_CPP}"
    "${FALLBACK_ICON_HEADER}"
)

target_include_directories(GW2EventCalendar PRIVATE src vendor/nexus vendor/imgui "${GENERATED_DIR}")
target_link_libraries(GW2EventCalendar PRIVATE nlohmann_json::nlohmann_json pugixml::pugixml)
target_compile_definitions(GW2EventCalendar PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# Nexus loads addon DLLs flat from <GW2>/addons/ (e.g. addons/GW2EventCalendar.dll);
# the per-addon <GW2>/addons/GW2EventCalendar/ folder is only for the addon's own
# runtime data (settings.json, events_cache.json), via Paths_GetAddonDirectory.
# The fallback icon ships embedded in the DLL itself - see FALLBACK_ICON_CPP above.
set_target_properties(GW2EventCalendar PROPERTIES OUTPUT_NAME "GW2EventCalendar")

# DLL stays loaded inside the running GW2 process while iterating - GW2/Nexus
# can hold the matching .pdb open for symbol lookup, which makes the linker's
# Debug-config /DEBUG step fail with LNK1201 on rebuild. Skip linker PDB
# generation in Debug so rebuilds don't depend on closing the game.
target_link_options(GW2EventCalendar PRIVATE $<$<CONFIG:Debug>:/DEBUG:NONE>)
