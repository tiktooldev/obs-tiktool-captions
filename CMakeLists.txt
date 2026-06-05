cmake_minimum_required(VERSION 3.28...3.30)

# Plugin metadata is also surfaced through buildspec.json on the OBS plugintemplate path.
project(obs-tiktool-captions VERSION 1.0.0)

set(PLUGIN_AUTHOR        "TikTool")
set(PLUGIN_DISPLAY_NAME  "TikTool Live Captions")
set(PLUGIN_WEBSITE       "https://tik.tools/captions")
set(PLUGIN_BUNDLE_ID     "tools.tik.captions")

option(ENABLE_QT "Build Qt UI" ON)

include(cmake/common/bootstrap.cmake NO_POLICY_SCOPE)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
find_package(obs-frontend-api REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs OBS::obs-frontend-api)

if(ENABLE_QT)
  find_package(Qt6 REQUIRED COMPONENTS Core Widgets Gui Network)
  set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
    AUTOMOC ON
    AUTOUIC ON
    AUTORCC ON)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
    Qt6::Core Qt6::Widgets Qt6::Gui Qt6::Network)
  target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE TIKTOOL_QT)
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
  data/tiktool.qrc
  src/plugin-main.cpp
  src/audio-capture.cpp
  src/audio-capture.hpp
  src/ws-client.cpp
  src/ws-client.hpp
  src/settings.cpp
  src/settings.hpp
  src/trial-tracker.cpp
  src/trial-tracker.hpp
  src/overlay-installer.cpp
  src/overlay-installer.hpp
  src/captions-dock.cpp
  src/captions-dock.hpp
  src/captions-wizard.cpp
  src/captions-wizard.hpp
  src/api-client.cpp
  src/api-client.hpp
  src/version.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE src)
target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_20)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES
  OUTPUT_NAME ${CMAKE_PROJECT_NAME})
