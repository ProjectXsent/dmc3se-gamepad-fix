cmake_minimum_required(VERSION 3.20)
project(DMC3Input CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT so the DLL does not need the VC++ redistributable next to the game.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(WARNING "The game is a 32-bit process: configure with -A Win32.")
endif()

find_package(SDL2 CONFIG REQUIRED)

add_library(dinput8 SHARED
  src/proxy.cpp
  src/pad.cpp
  src/dinput8.def)

target_compile_definitions(dinput8 PRIVATE
  DIRECTINPUT_VERSION=0x0800 WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_link_libraries(dinput8 PRIVATE SDL2::SDL2 dxguid)
set_target_properties(dinput8 PROPERTIES PREFIX "" OUTPUT_NAME "dinput8")

# Drop SDL2.dll and the sample config next to dinput8.dll for easy packaging.
add_custom_command(TARGET dinput8 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL2::SDL2> $<TARGET_FILE_DIR:dinput8>
  COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/DMC3Input.ini $<TARGET_FILE_DIR:dinput8>)
