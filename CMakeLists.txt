cmake_minimum_required(VERSION 3.28)

project(nowplaylisting VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "nowplaylisting currently supports Windows only.")
endif()

find_package(libobs REQUIRED)

add_library(${PROJECT_NAME} MODULE
  src/plugin-main.cpp
  src/nowplaylisting-source.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
  OBS::libobs
  gdiplus
  ole32
  propsys
  shell32
)

target_compile_features(${PROJECT_NAME} PRIVATE cxx_std_20)

if(MSVC)
  target_compile_options(${PROJECT_NAME} PRIVATE /utf-8 /permissive-)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES
  OUTPUT_NAME "${PROJECT_NAME}"
  PREFIX ""
)

install(TARGETS ${PROJECT_NAME}
  RUNTIME DESTINATION obs-plugins/64bit
  LIBRARY DESTINATION obs-plugins/64bit
)

install(DIRECTORY data/
  DESTINATION data/obs-plugins/${PROJECT_NAME}
)
