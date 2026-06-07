# Minimal bootstrap shim. The full obs-plugintemplate ships an elaborate cmake
# helpers tree under cmake/common; what the plugin actually needs from it is:
#   - set_target_properties_plugin(): install + bundle layout helpers
#
# To keep this repo self-contained without copying the entire upstream tree,
# we define a stub that does the bare minimum a typical Windows / macOS /
# Linux install needs. CI on a fresh checkout should clone the upstream helpers
# instead via:
#   git submodule add https://github.com/obsproject/obs-plugintemplate cmake/common
#
# When that submodule is present, this file is overwritten by the upstream one.

if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()
if(POLICY CMP0077)
  cmake_policy(SET CMP0077 NEW)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Windows")
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
endif()

function(set_target_properties_plugin tgt)
  cmake_parse_arguments(PARSE_ARGV 1 P "" "OUTPUT_NAME" "PROPERTIES")
  if(NOT P_OUTPUT_NAME)
    set(P_OUTPUT_NAME ${tgt})
  endif()
  set_target_properties(${tgt} PROPERTIES
    PREFIX        ""
    OUTPUT_NAME   ${P_OUTPUT_NAME}
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
  if(APPLE)
    set_target_properties(${tgt} PROPERTIES
      BUNDLE TRUE
      BUNDLE_EXTENSION "plugin"
      MACOSX_BUNDLE_BUNDLE_NAME "${PLUGIN_DISPLAY_NAME}"
      MACOSX_BUNDLE_GUI_IDENTIFIER "${PLUGIN_BUNDLE_ID}"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
  endif()
endfunction()
