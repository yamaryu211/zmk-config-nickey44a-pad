# Build a private copy for the selected shield. Other keyboards can use the same west checkout.
set(tps43_upstream "${ZEPHYR_ZMK_DRIVER_AZOTEQ_MODULE_DIR}/drivers/input")
set(tps43_build "${CMAKE_CURRENT_BINARY_DIR}/tps43")
file(MAKE_DIRECTORY "${tps43_build}")
foreach(source tps43.c tps43.h)
  configure_file("${tps43_upstream}/${source}" "${tps43_build}/${source}" COPYONLY)
endforeach()
set(patches tps43-multitouch)
if(CONFIG_SHIELD_TPS43_EDGE)
  list(APPEND patches tps43-edge-origin)
endif()
find_package(Git REQUIRED)
foreach(name IN LISTS patches)
  set(tps43_patch "${CMAKE_CURRENT_LIST_DIR}/../patches/${name}.patch")
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${tps43_patch}")
  execute_process(
    # Ignore any enclosing worktree so git applies paths relative to the build copy.
    COMMAND "${CMAKE_COMMAND}" -E env "GIT_CEILING_DIRECTORIES=${CMAKE_CURRENT_BINARY_DIR}"
            "${GIT_EXECUTABLE}" apply "${tps43_patch}"
    WORKING_DIRECTORY "${tps43_build}"
    COMMAND_ERROR_IS_FATAL ANY
  )
endforeach()
set_source_files_properties("${tps43_upstream}/tps43.c" TARGET_DIRECTORY drivers__input
                            PROPERTIES HEADER_FILE_ONLY TRUE)
target_sources(drivers__input PRIVATE "${tps43_build}/tps43.c")
