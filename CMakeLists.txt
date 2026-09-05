cmake_minimum_required(VERSION 3.28)
cmake_policy(SET CMP0091 NEW)
project(potplayer-dlss5-nr VERSION 0.1.3 LANGUAGES CXX)

if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()

if(NOT WIN32)
  message(FATAL_ERROR "Windows x64 + NVIDIA RTX only")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)

FetchContent_Declare(
  avisynthplus
  GIT_REPOSITORY https://github.com/AviSynth/AviSynthPlus.git
  GIT_TAG v3.7.5
  GIT_SHALLOW TRUE)
FetchContent_Declare(
  vapoursynth
  GIT_REPOSITORY https://github.com/vapoursynth/vapoursynth.git
  GIT_TAG R70
  GIT_SHALLOW TRUE)

# Headers only — do not add those projects.
FetchContent_GetProperties(avisynthplus)
if(NOT avisynthplus_POPULATED)
  FetchContent_Populate(avisynthplus)
endif()
FetchContent_GetProperties(vapoursynth)
if(NOT vapoursynth_POPULATED)
  FetchContent_Populate(vapoursynth)
endif()

set(AVS_INCLUDE "${avisynthplus_SOURCE_DIR}/avs_core/include")
set(VS_INCLUDE "${vapoursynth_SOURCE_DIR}/include")

add_library(nr-caller-shim SHARED src/caller_shim.cpp)
target_link_libraries(nr-caller-shim PRIVATE d3d12)
set_target_properties(nr-caller-shim PROPERTIES OUTPUT_NAME "nvngx.dll_pot" PREFIX "" SUFFIX ".dll")

add_library(DLSS5NR SHARED src/avs_plugin.cpp src/nr_bridge.cpp src/seh_wrap.cpp)
target_include_directories(DLSS5NR PRIVATE "${AVS_INCLUDE}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(DLSS5NR PRIVATE d3d12 dxgi)
target_compile_definitions(DLSS5NR PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(DLSS5NR PROPERTIES PREFIX "" SUFFIX ".dll")

add_library(vsdlss5nr SHARED src/vs_plugin.cpp src/nr_bridge.cpp src/seh_wrap.cpp)
target_include_directories(vsdlss5nr PRIVATE "${VS_INCLUDE}" "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(vsdlss5nr PRIVATE d3d12 dxgi)
target_compile_definitions(vsdlss5nr PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
set_target_properties(vsdlss5nr PROPERTIES PREFIX "" OUTPUT_NAME "vsdlss5nr" SUFFIX ".dll")

install(TARGETS nr-caller-shim DLSS5NR vsdlss5nr RUNTIME DESTINATION bin)
install(FILES scripts/after_svp.avs scripts/after_svp.vpy tools/install-portable.ps1 DESTINATION .)
