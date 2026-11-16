cmake_minimum_required(VERSION 3.24)
# Keep in sync with <Version> in ui/GScreen.App/GScreen.App.csproj (CI checks this).
project(GScreen LANGUAGES CXX RC VERSION 0.1.2)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(MSVC)
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/W4> $<$<COMPILE_LANGUAGE:CXX>:/permissive->
                      $<$<COMPILE_LANGUAGE:CXX>:/utf-8> $<$<COMPILE_LANGUAGE:CXX>:/EHsc>
                      $<$<COMPILE_LANGUAGE:CXX>:/await:strict>)
  add_compile_definitions(UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00)
endif()

# Adds a VERSIONINFO resource (product name + version) to an executable target.
function(gscreen_version_resource target description)
  set(GS_TARGET ${target})
  set(GS_FILE_DESCRIPTION ${description})
  set(rc ${CMAKE_CURRENT_BINARY_DIR}/${target}.version.rc)
  configure_file(${PROJECT_SOURCE_DIR}/common/version.rc.in ${rc} @ONLY)
  target_sources(${target} PRIVATE ${rc})
endfunction()

add_subdirectory(core)
