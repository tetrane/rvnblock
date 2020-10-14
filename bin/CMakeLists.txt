add_executable(rvn_block_reader
  cli_block_reader.cpp
)

target_link_libraries(rvn_block_reader
  PUBLIC
    rvnblock
)

include(GNUInstallDirs)
install(TARGETS rvn_block_reader
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

