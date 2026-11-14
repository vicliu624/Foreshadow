set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS catalog.json)
install(FILES licensing.json DESTINATION share/signalscope/compliance/providers RENAME research-power-report.json COMPONENT SignalScope)
file(READ catalog.json power_report_catalog)
configure_file(catalog.hpp.in "${PROJECT_BINARY_DIR}/power_report/catalog.hpp" @ONLY)
configure_file(catalog.json "${PROJECT_BINARY_DIR}/power-report-catalog.json" COPYONLY)

add_executable(amc-power-report main.cpp)
set_target_properties(amc-power-report PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
add_custom_command(TARGET amc-power-report POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/catalog.json"
            "$<TARGET_FILE_DIR:amc-power-report>/power-report-catalog.json"
    VERBATIM)
target_include_directories(amc-power-report PRIVATE "${PROJECT_BINARY_DIR}/power_report")
target_link_libraries(amc-power-report PRIVATE signalscope_iq)
if(MINGW)
  target_link_options(amc-power-report PRIVATE -municode)
endif()

set(power_report_resources "[\"power-report-catalog.json\"]")
function(power_report_runtime_asset source relative)
  string(REPLACE "\\" "\\\\" escaped "${relative}")
  string(REPLACE "\"" "\\\"" escaped "${escaped}")
  string(JSON count LENGTH "${power_report_resources}")
  string(JSON resources SET "${power_report_resources}" ${count} "\"${escaped}\"")
  set(power_report_resources "${resources}" PARENT_SCOPE)
  get_filename_component(directory "${relative}" DIRECTORY)
  get_filename_component(real "${source}" REALPATH)
  add_custom_command(TARGET amc-power-report POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:amc-power-report>/${directory}"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${real}" "$<TARGET_FILE_DIR:amc-power-report>/${relative}"
    VERBATIM)
endfunction()
if(APPLE)
  foreach(library IN LISTS AMC_MACOS_EXTRA_RUNTIME_LIBRARIES)
    get_filename_component(name "${library}" NAME)
    power_report_runtime_asset("${library}" "runtime/${name}")
  endforeach()
  foreach(notice IN LISTS AMC_MACOS_EXTRA_RUNTIME_LICENSE_FILES)
    get_filename_component(name "${notice}" NAME)
    power_report_runtime_asset("${notice}" "licenses/toolchain/${name}")
  endforeach()
endif()
configure_file(plugin.json.in "${PROJECT_BINARY_DIR}/power_report/power-report.json" @ONLY)

install(TARGETS amc-power-report RUNTIME DESTINATION research-plugins/power-report COMPONENT ResearchExamples)
install(FILES "${PROJECT_BINARY_DIR}/power_report/power-report.json"
        DESTINATION research-plugins/power-report COMPONENT ResearchExamples)
install(FILES catalog.json DESTINATION research-plugins/power-report RENAME power-report-catalog.json COMPONENT ResearchExamples)
if(APPLE)
  amc_install_mac_runtime(research-plugins/power-report/runtime
    research-plugins/power-report/licenses/toolchain ResearchExamples)
  amc_mac_runtime_arguments(runtime_arguments)
  install(CODE "
    execute_process(
      COMMAND /bin/bash \"${PROJECT_SOURCE_DIR}/cmake/RelocateMacRuntime.sh\"
        \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/research-plugins/power-report\"
        \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/research-plugins/power-report/runtime\" --sign-leaves ${runtime_arguments}
      COMMAND_ERROR_IS_FATAL ANY
    )" COMPONENT ResearchExamples)
endif()
