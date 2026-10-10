cmake_minimum_required(VERSION 3.20)
project(rubiks C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(RUBIKS_BUILD_GUI "Build the raylib GUI (downloads raylib if not installed)" ON)

set(RUBIKS_WARNINGS -Wall -Wextra -Wshadow -Wno-missing-field-initializers)

# ---- core library: cube model + all solvers ----
add_library(cubecore STATIC
  src/cube.c
  src/cubie.c
  src/solution.c
  src/lessons.c
  src/solvers/common.c
  src/solvers/beginner.c
  src/solvers/cfop.c
  src/solvers/cfop_algs.c
  src/solvers/kociemba.c
  src/solvers/thistlethwaite.c
)
target_include_directories(cubecore PUBLIC include PRIVATE src)
target_compile_options(cubecore PRIVATE ${RUBIKS_WARNINGS})
target_link_libraries(cubecore PUBLIC m)

# ---- command line ----
add_executable(rubik apps/cli.c)
target_compile_options(rubik PRIVATE ${RUBIKS_WARNINGS})
target_link_libraries(rubik PRIVATE cubecore)

# ---- tests ----
enable_testing()
add_executable(rubik_tests tests/test_main.c)
target_include_directories(rubik_tests PRIVATE src)
target_compile_options(rubik_tests PRIVATE ${RUBIKS_WARNINGS})
target_link_libraries(rubik_tests PRIVATE cubecore)
add_test(NAME rubik_tests COMMAND rubik_tests)

# ---- GUI ----
if(RUBIKS_BUILD_GUI)
  find_package(raylib 5.0 QUIET)
  if(NOT raylib_FOUND)
    message(STATUS "raylib not found on the system; fetching raylib 5.5")
    include(FetchContent)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(raylib
      URL https://github.com/raysan5/raylib/archive/refs/tags/5.5.tar.gz
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(raylib)
  endif()
  find_package(Threads REQUIRED)
  add_executable(rubik_gui apps/gui/main.c apps/gui/render.c apps/gui/ui.c)
  target_compile_options(rubik_gui PRIVATE ${RUBIKS_WARNINGS})
  target_link_libraries(rubik_gui PRIVATE cubecore raylib Threads::Threads)
  # the UI loads IBM Plex from fonts/ next to the executable
  add_custom_command(TARGET rubik_gui POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${CMAKE_CURRENT_SOURCE_DIR}/apps/gui/fonts $<TARGET_FILE_DIR:rubik_gui>/fonts)
endif()
