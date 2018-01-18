cmake_minimum_required(VERSION 3.1)

project(libevent-examples)

option(USE_GCOV "Create a GCov-enabled build." OFF)
if (USE_GCOV)
  set(GCC_COVERAGE_COMPILE_FLAGS "-fprofile-arcs -ftest-coverage")
  set(GCC_COVERAGE_LINK_FLAGS    "-lgcov")
endif()

# for FreeBSD
set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -I/usr/local/include" )
set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -I/usr/local/include" )

set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} ${GCC_COVERAGE_COMPILE_FLAGS}" )
set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} ${GCC_COVERAGE_COMPILE_FLAGS}" )
set(CMAKE_EXE_LINKER_FLAGS  "${CMAKE_EXE_LINKER_FLAGS} ${GCC_COVERAGE_LINK_FLAGS}" )

find_library(LIBEVENT2_CORE event_core)

include_directories(include)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED on)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Werror")

add_executable(chat src/chat.c)
target_link_libraries(chat ${LIBEVENT2_CORE})

file(GLOB_RECURSE ALL_SOURCE_FILES *.c *.h)
add_custom_target(format
    COMMAND clang-format --style=file -i ${ALL_SOURCE_FILES} )
