cmake_minimum_required(VERSION 3.7)
project(rvnblock)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE)
endif()

option(BUILD_SHARED_LIBS "Set to ON to build shared libraries; OFF for static libraries." OFF)
option(WARNING_AS_ERROR "Set to ON to build with -Werror" ON)

option(BUILD_TEST_COVERAGE "Set to ON to build while generating coverage information. Will put source on the build directory." OFF)

find_package(rvnbinresource REQUIRED)
find_package(rvnmetadata REQUIRED)

add_library(rvnblock
  src/block_writer.cpp
  src/block_reader.cpp
)

target_compile_options(rvnblock PRIVATE -W -Wall -Wextra -Wmissing-include-dirs -Wunknown-pragmas -Wpointer-arith -Wmissing-field-initializers -Wno-multichar -Wreturn-type)

if(WARNING_AS_ERROR)
  target_compile_options(rvnblock PRIVATE -Werror)
endif()

if(BUILD_TEST_COVERAGE)
  target_compile_options(rvnblock PRIVATE -g -O0 --coverage -fprofile-arcs -ftest-coverage)
  target_link_libraries(rvnblock PRIVATE gcov)
endif()

target_include_directories(rvnblock
  PUBLIC
    $<INSTALL_INTERFACE:include>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_link_libraries(rvnblock
  PUBLIC
    rvnbinresource
    rvnmetadata
)

set(PUBLIC_HEADERS
  include/common.h
  include/block_writer.h
  include/block_reader.h
)

set_target_properties(rvnblock PROPERTIES
  PUBLIC_HEADER "${PUBLIC_HEADERS}"
  POSITION_INDEPENDENT_CODE ON
)

include(GNUInstallDirs)
install(TARGETS rvnblock
  EXPORT rvnblock-export
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}
)

install(EXPORT rvnblock-export
  FILE
    rvnblock-targets.cmake
  DESTINATION
    ${CMAKE_INSTALL_DATADIR}/cmake/rvnblock
)

install(FILES cmake/rvnblock-config.cmake
  DESTINATION ${CMAKE_INSTALL_DATADIR}/cmake/rvnblock
)

add_subdirectory(bin)

enable_testing()
#add_subdirectory(test)
