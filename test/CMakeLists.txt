cmake_minimum_required(VERSION 3.7)
project(test)

find_package(Boost 1.49 COMPONENTS
    unit_test_framework
)

if(NOT Boost_FOUND)
  message(WARNING "Boost not found, don't build tests")
  return()
endif(NOT Boost_FOUND)

add_executable(test_rvnblock
  test_rvnblock.cpp
)

target_link_libraries(test_rvnblock
  PUBLIC
    Boost::boost

  PRIVATE
    rvnblock
    rvnmetadata::common
    rvnmetadata::sql
    Boost::unit_test_framework
)

target_compile_definitions(test_rvnblock PRIVATE "BOOST_TEST_DYN_LINK")

add_test(rvnblock::rvnblock test_rvnblock)
