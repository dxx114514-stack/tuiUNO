cmake_minimum_required(VERSION 3.16)
project(tuiUNO VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Threads REQUIRED)

add_library(uno_core STATIC
  src/card.cpp
  src/deck.cpp
  src/rules.cpp
  src/game.cpp
  src/serialize.cpp
  src/ai.cpp
  src/i18n.cpp
  src/console.cpp
  src/ui.cpp
  src/player.cpp
  src/net.cpp
  src/protocol.cpp
  src/session.cpp
)

target_include_directories(uno_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(uno_core PUBLIC Threads::Threads)

if(WIN32)
  target_compile_definitions(uno_core PUBLIC UNO_WIN=1)
  target_link_libraries(uno_core PUBLIC ws2_32)
else()
  target_compile_definitions(uno_core PUBLIC UNO_WIN=0)
endif()

if(MSVC)
  target_compile_options(uno_core PRIVATE /W4 /utf-8)
else()
  target_compile_options(uno_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

add_executable(uno src/main.cpp src/app.cpp src/online.cpp)
target_link_libraries(uno PRIVATE uno_core)

# Link the compiler runtime statically so the shipped binary depends only on
# Windows system DLLs (or nothing at all on Linux) and runs on a machine that
# has no MinGW or MSVC redistributable installed.
option(TUIUNO_STATIC_RUNTIME "Link the compiler runtime statically" OFF)
if(TUIUNO_STATIC_RUNTIME)
  if(MSVC)
    message(WARNING "TUIUNO_STATIC_RUNTIME needs MSVC: set /MT (CMAKE_MSVC_RUNTIME_LIBRARY)")
  else()
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_LINK_OPTIONS -static)
    check_cxx_source_compiles("int main() { return 0; }" TUIUNO_HAVE_STATIC_LINK)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)
    if(TUIUNO_HAVE_STATIC_LINK)
      target_link_options(uno PRIVATE -static)
    else()
      target_link_options(uno PRIVATE -static-libgcc -static-libstdc++)
    endif()
  endif()
endif()

enable_testing()
add_executable(uno_tests tests/test_uno.cpp)
target_link_libraries(uno_tests PRIVATE uno_core)
add_test(NAME uno_tests COMMAND uno_tests)

install(TARGETS uno RUNTIME DESTINATION bin)