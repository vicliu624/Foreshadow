file(GLOB desktop_languages CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/src/frontend/desktop/i18n/*.json"
    "${PROJECT_SOURCE_DIR}/src/frontend/desktop/i18n/*.qm")
foreach(language IN LISTS desktop_languages)
  get_filename_component(name "${language}" NAME)
  configure_file("${language}" "${PROJECT_BINARY_DIR}/resources/desktop/languages/${name}" COPYONLY)
endforeach()
install(DIRECTORY "${PROJECT_SOURCE_DIR}/src/frontend/desktop/i18n/"
    DESTINATION share/signalscope/desktop/languages COMPONENT SignalScope)
if(APPLE)
  install(DIRECTORY "${PROJECT_SOURCE_DIR}/src/frontend/desktop/i18n/"
      DESTINATION Foreshadow.app/Contents/Resources/signalscope/desktop/languages COMPONENT SignalScope)
endif()
