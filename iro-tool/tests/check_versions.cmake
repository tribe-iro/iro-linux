cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED IRO_TOOL_ROOT)
  message(FATAL_ERROR "IRO_TOOL_ROOT is required")
endif()

set(IRO_CORE_ROOT "${IRO_TOOL_ROOT}/../iro-core")

file(READ "${IRO_CORE_ROOT}/VERSION" CORE_VERSION_TEXT)
string(REGEX MATCH "([0-9]+)\\.([0-9]+)" _ "${CORE_VERSION_TEXT}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR "Failed to parse iro-core/VERSION")
endif()
set(CORE_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(CORE_VERSION_MINOR "${CMAKE_MATCH_2}")

file(READ "${IRO_CORE_ROOT}/overlay/iro/include/iro/version.hpp" CORE_VERSION_HPP)
string(REGEX MATCH "IRO_CORE_SPEC_MAJOR[ \t]*=[ \t]*([0-9]+)" _ "${CORE_VERSION_HPP}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR "Failed to parse IRO_CORE_SPEC_MAJOR from version.hpp")
endif()
set(CORE_HPP_MAJOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "IRO_CORE_SPEC_MINOR[ \t]*=[ \t]*([0-9]+)" _ "${CORE_VERSION_HPP}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR "Failed to parse IRO_CORE_SPEC_MINOR from version.hpp")
endif()
set(CORE_HPP_MINOR "${CMAKE_MATCH_1}")

if(NOT CORE_VERSION_MAJOR STREQUAL CORE_HPP_MAJOR OR
   NOT CORE_VERSION_MINOR STREQUAL CORE_HPP_MINOR)
  message(FATAL_ERROR
    "Core version mismatch: VERSION=${CORE_VERSION_MAJOR}.${CORE_VERSION_MINOR}, "
    "version.hpp=${CORE_HPP_MAJOR}.${CORE_HPP_MINOR}")
endif()

file(READ "${IRO_TOOL_ROOT}/overlay/scripts/iro/iro_common.hpp" IRO_COMMON_HPP)
string(REGEX MATCH "kLayoutSchemaMajor[ \t]*=[ \t]*([0-9]+)" _ "${IRO_COMMON_HPP}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR "Failed to parse kLayoutSchemaMajor from iro_common.hpp")
endif()
set(LAYOUT_SCHEMA_MAJOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "kLayoutSchemaMinor[ \t]*=[ \t]*([0-9]+)" _ "${IRO_COMMON_HPP}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR "Failed to parse kLayoutSchemaMinor from iro_common.hpp")
endif()
set(LAYOUT_SCHEMA_MINOR "${CMAKE_MATCH_1}")

file(READ "${IRO_TOOL_ROOT}/docs/ENHANCEMENT_SPEC.md" ENHANCEMENT_SPEC_TEXT)
string(REGEX MATCH "IRO-TOOL-SPEC version:[ \t]*([0-9]+\\.[0-9]+)" _ "${ENHANCEMENT_SPEC_TEXT}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR
    "Failed to parse IRO-TOOL-SPEC version tag from ENHANCEMENT_SPEC.md")
endif()
set(TOOL_SPEC_TAG "${CMAKE_MATCH_1}")
set(EXPECTED_TOOL_SPEC_TAG "${LAYOUT_SCHEMA_MAJOR}.${LAYOUT_SCHEMA_MINOR}")
if(NOT TOOL_SPEC_TAG STREQUAL EXPECTED_TOOL_SPEC_TAG)
  message(FATAL_ERROR
    "Tool spec tag mismatch: ENHANCEMENT_SPEC=${TOOL_SPEC_TAG}, "
    "iro_common layout schema=${EXPECTED_TOOL_SPEC_TAG}")
endif()

file(READ "${IRO_CORE_ROOT}/docs/PRIMITIVES_SPEC.md" PRIMITIVES_SPEC_TEXT)
string(REGEX MATCH "IRO-CORE-SPEC version:[ \t]*([0-9]+\\.[0-9]+)" _ "${PRIMITIVES_SPEC_TEXT}")
if(NOT CMAKE_MATCH_1)
  message(FATAL_ERROR
    "Failed to parse IRO-CORE-SPEC version tag from PRIMITIVES_SPEC.md")
endif()
set(CORE_SPEC_TAG "${CMAKE_MATCH_1}")
set(EXPECTED_CORE_SPEC_TAG "${CORE_VERSION_MAJOR}.${CORE_VERSION_MINOR}")
if(NOT CORE_SPEC_TAG STREQUAL EXPECTED_CORE_SPEC_TAG)
  message(FATAL_ERROR
    "Core spec tag mismatch: PRIMITIVES_SPEC=${CORE_SPEC_TAG}, "
    "core version=${EXPECTED_CORE_SPEC_TAG}")
endif()

message(STATUS "Version consistency checks passed")
