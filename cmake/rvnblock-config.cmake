get_filename_component(RVNBLOCK_CMAKE_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)
include(CMakeFindDependencyMacro)

find_dependency(rvnbinresource REQUIRED)
find_dependency(rvnmetadata REQUIRED)

if(NOT TARGET rvnblock)
	include("${RVNBLOCK_CMAKE_DIR}/rvnblock-targets.cmake")
endif()
