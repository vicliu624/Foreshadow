add_executable(signalscope-gui
    gui.cpp
    legal_notices.cpp
    workspace.cpp
    live_tools.cpp
    toolbox_center.cpp
    toolbox_services.cpp
    analysis_panel.cpp
    desktop_language.cpp
    language_preference.cpp
    desktop_tools.cpp
    desktop_error.cpp
    receiver_dialog.cpp
    receiver_probe.cpp
    search_width_control.cpp
    modulation_assessment.cpp
    desktop_menus.cpp
    event_presentation.cpp
    status_presentation.cpp
    desktop_recording_export.cpp)
set_target_properties(signalscope-gui PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
set_target_properties(signalscope-gui PROPERTIES AUTOMOC ON)
file(GLOB status_catalogs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/status/i18n/*.json")
foreach(catalog IN LISTS status_catalogs)
  get_filename_component(locale "${catalog}" NAME)
  configure_file("${catalog}" "${PROJECT_BINARY_DIR}/resources/desktop-status/i18n/${locale}" COPYONLY)
endforeach()
install(DIRECTORY status/i18n/ DESTINATION share/signalscope/desktop-status/i18n COMPONENT SignalScope)
if(APPLE)
  amc_install_mac_runtime(Foreshadow.app/Contents/Frameworks
    Foreshadow.app/Contents/Resources/licenses/toolchain SignalScope)
  amc_mac_runtime_arguments(mac_runtime_arguments)
  install(DIRECTORY status/i18n/ DESTINATION Foreshadow.app/Contents/Resources/signalscope/desktop-status/i18n COMPONENT SignalScope)
endif()
get_property(desktop_receiver_modules GLOBAL PROPERTY AMC_RECEIVER_MODULE_TARGETS)
add_dependencies(signalscope-gui ${desktop_receiver_modules})
target_include_directories(signalscope-gui PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_link_libraries(signalscope-gui PRIVATE signalscope_core signalscope_distribution signalscope_distribution_network Qt6::Widgets)

if(SIGNALSCOPE_REQUIRE_AUDIO)
  find_package(Qt6 ${desktop_qt_minimum} REQUIRED COMPONENTS Multimedia)
else()
  find_package(Qt6 ${desktop_qt_minimum} QUIET COMPONENTS Multimedia)
endif()
if(TARGET Qt6::Multimedia)
  target_link_libraries(signalscope-gui PRIVATE Qt6::Multimedia)
  target_compile_definitions(signalscope-gui PRIVATE SIGNALSCOPE_HAS_AUDIO=1)
endif()

add_subdirectory("${PROJECT_SOURCE_DIR}/plugins/desktop" "${PROJECT_BINARY_DIR}/plugins/desktop")
target_link_libraries(signalscope-gui PRIVATE
    signalscope_desktop_display signalscope_desktop_research signalscope_desktop_parameters)
add_executable(signalscope-toolboxes "${PROJECT_SOURCE_DIR}/tools/distribution/manage.cpp" toolbox_services.cpp)
set_target_properties(signalscope-toolboxes PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
target_include_directories(signalscope-toolboxes PRIVATE "${PROJECT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(signalscope-toolboxes PRIVATE signalscope_distribution_network signalscope_desktop_research Qt6::Widgets)
get_target_property(frontend_sources signalscope-gui SOURCES)
set(translation_sources "")
foreach(source IN LISTS frontend_sources)
  get_filename_component(absolute_source "${source}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
  list(APPEND translation_sources "${absolute_source}")
endforeach()
foreach(plugin IN ITEMS signalscope_desktop_display signalscope_desktop_research signalscope_desktop_parameters)
  get_target_property(plugin_sources ${plugin} SOURCES)
  list(APPEND translation_sources ${plugin_sources})
endforeach()
qt_add_translations(signalscope-gui SOURCES ${translation_sources}
    TS_FILES "${PROJECT_SOURCE_DIR}/translations/ams_zh_CN.ts")
include("${PROJECT_SOURCE_DIR}/cmake/QtWidgetTranslations.cmake")
amc_add_qt_widget_translations(signalscope-gui)
if(NOT MSVC)
  target_compile_options(signalscope-gui PRIVATE $<$<CONFIG:Release>:-O1>)
endif()

set(desktop_tools signalscope-compare-lora-audit signalscope-benchmark-lora signalscope-generate-pluto-iq signalscope-update signalscope-launcher signalscope-toolboxes)
if(TARGET signalscope-launcher-gui)
  list(APPEND desktop_tools signalscope-launcher-gui)
endif()
if(TARGET signalscope-pager-status)
  list(APPEND desktop_tools signalscope-pager-status)
endif()
if(TARGET signalscope-media)
  list(APPEND desktop_tools signalscope-media)
endif()

if(APPLE)
  set(AMC_MACOS_MINIMUM_VERSION_PLIST "")
  if(CMAKE_OSX_DEPLOYMENT_TARGET)
    set(AMC_MACOS_MINIMUM_VERSION_PLIST
        "    <key>LSMinimumSystemVersion</key>\n    <string>${CMAKE_OSX_DEPLOYMENT_TARGET}</string>")
  endif()
  configure_file(macos/Info.plist.in macos/Info.plist @ONLY)
  set_target_properties(signalscope-gui PROPERTIES
      MACOSX_BUNDLE TRUE OUTPUT_NAME Foreshadow
      INSTALL_RPATH "@loader_path/../Frameworks"
      MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/macos/Info.plist"
      MACOSX_BUNDLE_BUNDLE_NAME Foreshadow
      MACOSX_BUNDLE_GUI_IDENTIFIER org.signalscope.ams
      MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
      MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
  set(mac_deploy_receiver_modules "")
  foreach(module IN LISTS desktop_receiver_modules)
    add_custom_command(TARGET signalscope-gui POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:signalscope-gui>/receiver-plugins"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${module}>"
            "$<TARGET_FILE_DIR:signalscope-gui>/receiver-plugins/$<TARGET_FILE_NAME:${module}>"
        VERBATIM)
    get_target_property(catalogs ${module} AMC_RECEIVER_CATALOG_DIRECTORIES)
    foreach(catalog IN LISTS catalogs)
      get_filename_component(catalog_name "${catalog}" NAME)
      add_custom_command(TARGET signalscope-gui POST_BUILD
          COMMAND "${CMAKE_COMMAND}" -E rm -rf "$<TARGET_FILE_DIR:signalscope-gui>/receiver-plugins/${catalog_name}"
          COMMAND "${CMAKE_COMMAND}" -E copy_directory "${catalog}"
              "$<TARGET_BUNDLE_CONTENT_DIR:signalscope-gui>/Resources/signalscope/receivers/${catalog_name}" VERBATIM)
    endforeach()
    get_target_property(declaration ${module} AMC_RECEIVER_RESOURCE_DECLARATION)
    add_custom_command(TARGET signalscope-gui POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${declaration}"
            "$<TARGET_BUNDLE_CONTENT_DIR:signalscope-gui>/Resources/signalscope/receivers/" VERBATIM)
    string(APPEND mac_deploy_receiver_modules
        " \"Foreshadow.app/Contents/MacOS/receiver-plugins/$<TARGET_FILE_NAME:${module}>\"")
  endforeach()
  install(TARGETS signalscope-gui BUNDLE DESTINATION . COMPONENT SignalScope)
  install(TARGETS signalscope ${desktop_tools}
          RUNTIME DESTINATION Foreshadow.app/Contents/MacOS COMPONENT SignalScope)
  set(mac_deploy_executables "")
  foreach(tool IN ITEMS signalscope ${desktop_tools})
    string(APPEND mac_deploy_executables " \"Foreshadow.app/Contents/MacOS/$<TARGET_FILE_NAME:${tool}>\"")
  endforeach()
  # Providers declare private native processes and data at their assembly
  # boundary. Include every executable in relocation, including helpers that
  # live outside MacOS, and keep a runnable development bundle as well.
  get_property(private_executables GLOBAL PROPERTY AMC_BUNDLE_PRIVATE_EXECUTABLES)
  foreach(record IN LISTS private_executables)
    string(REPLACE "|" ";" fields "${record}")
    list(GET fields 0 target)
    list(GET fields 1 destination)
    add_dependencies(signalscope-gui ${target})
    add_custom_command(TARGET signalscope-gui POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:signalscope-gui>/${destination}"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:${target}>" "$<TARGET_BUNDLE_CONTENT_DIR:signalscope-gui>/${destination}/" VERBATIM)
    if(NOT target IN_LIST desktop_tools)
      string(APPEND mac_deploy_executables " \"Foreshadow.app/Contents/${destination}/$<TARGET_FILE_NAME:${target}>\"")
    endif()
  endforeach()
  get_property(private_files GLOBAL PROPERTY AMC_BUNDLE_PRIVATE_FILES)
  foreach(record IN LISTS private_files)
    string(REPLACE "|" ";" fields "${record}")
    list(GET fields 0 source)
    list(GET fields 1 destination)
    get_filename_component(directory "${destination}" DIRECTORY)
    add_custom_command(TARGET signalscope-gui POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_BUNDLE_CONTENT_DIR:signalscope-gui>/${directory}"
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${source}" "$<TARGET_BUNDLE_CONTENT_DIR:signalscope-gui>/${destination}" VERBATIM)
  endforeach()
  qt_generate_deploy_script(TARGET signalscope-gui OUTPUT_SCRIPT desktop_deploy CONTENT "
qt_deploy_runtime_dependencies(
    EXECUTABLE \"Foreshadow.app\"
    ADDITIONAL_EXECUTABLES ${mac_deploy_executables}
    ADDITIONAL_MODULES ${mac_deploy_receiver_modules}
    DEPLOY_TOOL_OPTIONS \"-libpath=\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/Foreshadow.app/Contents/Frameworks\"
)")
  install(SCRIPT "${desktop_deploy}" COMPONENT SignalScope)
  install(CODE "
    include(\"${PROJECT_SOURCE_DIR}/cmake/PcmMediaDeployment.cmake\")
    amc_finalize_pcm_bundle(\"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/Foreshadow.app\" \"${amc_ffmpeg_prefix}\")
  " COMPONENT SignalScope)
  install(CODE "
    execute_process(
      COMMAND /bin/bash \"${PROJECT_SOURCE_DIR}/cmake/RelocateMacBundle.sh\"
        \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/Foreshadow.app\" ${mac_runtime_arguments}
      COMMAND_ERROR_IS_FATAL ANY
    )" COMPONENT SignalScope)
else()
  install(TARGETS signalscope-gui ${desktop_tools} RUNTIME DESTINATION bin COMPONENT SignalScope)
endif()
if(WIN32 AND COMMAND qt_generate_deploy_app_script)
  set(pcm_deploy_options "")
  if(Qt6_VERSION VERSION_GREATER_EQUAL 6.8)
    # Research WAV playback, FM and television feed PCM directly to QAudioSink.
    # No QMediaPlayer/codec backend is part of the desktop runtime contract.
    set(pcm_deploy_options DEPLOY_TOOL_OPTIONS --skip-plugin-types multimedia --no-ffmpeg --no-opengl-sw --no-system-d3d-compiler --no-system-dxc-compiler)
  endif()
  qt_generate_deploy_app_script(TARGET signalscope-gui OUTPUT_SCRIPT desktop_deploy
                                NO_UNSUPPORTED_PLATFORM_ERROR ${pcm_deploy_options})
  install(SCRIPT "${desktop_deploy}" COMPONENT SignalScope)
endif()
if(MSVC)
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT SignalScope)
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION bin)
  include(InstallRequiredSystemLibraries)
endif()
