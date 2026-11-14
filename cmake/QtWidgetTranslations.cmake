function(amc_add_qt_widget_translations target)
  if(TARGET Qt6::qmake)
    get_target_property(qmake Qt6::qmake IMPORTED_LOCATION)
    if(NOT qmake)
      get_target_property(configs Qt6::qmake IMPORTED_CONFIGURATIONS)
      if(configs)
        list(GET configs 0 config)
        get_target_property(qmake Qt6::qmake "IMPORTED_LOCATION_${config}")
      endif()
    endif()
  endif()

  if(qmake)
    execute_process(COMMAND "${qmake}" -query QT_INSTALL_TRANSLATIONS
      RESULT_VARIABLE result OUTPUT_VARIABLE translations OUTPUT_STRIP_TRAILING_WHITESPACE
      ERROR_QUIET)
    if(result STREQUAL "0" AND EXISTS "${translations}/qtbase_zh_CN.qm")
      qt_add_resources(${target} amc_qt_widget_translations PREFIX "/i18n"
        BASE "${translations}" FILES "${translations}/qtbase_zh_CN.qm")
      return()
    endif()
  endif()

  if(SIGNALSCOPE_REQUIRE_GUI)
    message(FATAL_ERROR "The desktop build requires the Qt Widgets Simplified Chinese catalog (qtbase_zh_CN.qm). Install the Qt translations for the selected SDK (qt6-translations-l10n on Debian and Ubuntu).")
  endif()
endfunction()
