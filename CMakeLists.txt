cmake_minimum_required(VERSION 3.24)

project(LimeForge VERSION 1.1.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(LimeForge WIN32
  src/main.cpp
  src/app.cpp
  src/engine.cpp
  src/catalog_data.cpp
  src/app.rc
)

target_include_directories(LimeForge PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(LimeForge PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)

if (MSVC)
  target_compile_options(LimeForge PRIVATE /utf-8 /W4 /permissive- /Zc:__cplusplus /EHsc)
  target_link_options(LimeForge PRIVATE /MANIFEST:NO)
endif()

target_link_libraries(LimeForge PRIVATE
  d2d1
  dwrite
  dwmapi
  shell32
  ole32
  user32
  gdi32
  advapi32
  psapi
)

set_target_properties(LimeForge PROPERTIES
  OUTPUT_NAME "LimeForge"
  VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:LimeForge>"
)
