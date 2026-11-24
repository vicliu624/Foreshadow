# Shared module rules have no dependency on any of their provider adapters.
target_sources(signalscope_dsp PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/live_input.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/recovery.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/codec.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/tracker.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/core/demod.cpp")
file(GLOB catalogs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/i18n/*.json")
foreach(catalog IN LISTS catalogs)
    get_filename_component(locale "${catalog}" NAME)
    configure_file("${catalog}" "${PROJECT_BINARY_DIR}/resources/adsb/i18n/${locale}" COPYONLY)
endforeach()
install(DIRECTORY i18n/ DESTINATION share/signalscope/adsb/i18n COMPONENT SignalScope)
foreach(asset IN ITEMS map.json map-source.json)
    configure_file("${CMAKE_CURRENT_SOURCE_DIR}/${asset}" "${PROJECT_BINARY_DIR}/resources/adsb/${asset}" COPYONLY)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${asset}" DESTINATION share/signalscope/adsb COMPONENT SignalScope)
    if(APPLE)
        install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/${asset}" DESTINATION Foreshadow.app/Contents/Resources/signalscope/adsb COMPONENT SignalScope)
    endif()
endforeach()
if(APPLE)
    install(DIRECTORY i18n/ DESTINATION Foreshadow.app/Contents/Resources/signalscope/adsb/i18n COMPONENT SignalScope)
endif()
