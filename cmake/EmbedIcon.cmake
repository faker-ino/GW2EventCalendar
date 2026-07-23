# Converts a binary file (IN_FILE) into a generated C++ byte array
# (OUT_HEADER/OUT_CPP), exposing kFallbackIconPng/kFallbackIconPngSize for
# Textures_GetOrCreateFromMemory. Run via `cmake -P` from a CMakeLists.txt
# custom command so the array regenerates whenever the source PNG changes.
#
# Deliberately not the Win32-resource route (Textures_GetOrCreateFromResource)
# - that was tried in this repo and Nexus's loader couldn't find a resource
# Win32 itself confirmed was present (see main.cpp). GetOrCreateFromMemory
# takes a raw pointer + size instead, no OS resource lookup involved.
if(NOT DEFINED IN_FILE OR NOT DEFINED OUT_HEADER OR NOT DEFINED OUT_CPP)
    message(FATAL_ERROR "EmbedIcon.cmake requires -DIN_FILE=, -DOUT_HEADER=, -DOUT_CPP=")
endif()

file(READ "${IN_FILE}" hex_content HEX)
string(LENGTH "${hex_content}" hex_length)
math(EXPR byte_count "${hex_length} / 2")
string(REGEX REPLACE "([0-9a-fA-F][0-9a-fA-F])" "0x\\1," hex_array "${hex_content}")

get_filename_component(header_name "${OUT_HEADER}" NAME)

file(WRITE "${OUT_HEADER}"
"#pragma once
#include <cstdint>

extern const unsigned char kFallbackIconPng[];
extern const uint64_t kFallbackIconPngSize;
")

file(WRITE "${OUT_CPP}"
"#include \"${header_name}\"

const unsigned char kFallbackIconPng[] = { ${hex_array} };
const uint64_t kFallbackIconPngSize = ${byte_count};
")
