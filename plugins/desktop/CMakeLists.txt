add_library(signalscope_desktop_parameters STATIC common/parameter_editor.cpp)
target_link_libraries(signalscope_desktop_parameters PUBLIC signalscope_iq Qt6::Widgets)
target_include_directories(signalscope_desktop_parameters PUBLIC "${PROJECT_SOURCE_DIR}")

add_library(signalscope_desktop_display STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/display/signal_display.cpp")
target_link_libraries(signalscope_desktop_display PUBLIC signalscope_dsp Qt6::Widgets)

add_library(signalscope_desktop_research STATIC
    "${PROJECT_SOURCE_DIR}/plugins/wspr/live_panel.cpp"
    "${PROJECT_SOURCE_DIR}/plugins/adsb/live_panel.cpp"
    "${PROJECT_SOURCE_DIR}/plugins/adsb/map_view.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/waveform/trace_view.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/waveform/plane_view.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/waveform/recorded_worker.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/waveform/analysis_save_worker.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/waveform/channel_panel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/waveform/research_views.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/waveform/window_panel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/waveform/model_panel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/waveform/recovery_panel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/waveform/recovery_complex_view.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/waveform/laboratory.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/waveform/capture_controls.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/external_research.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/research_provider.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/live_registry.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/media_playback.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/research_audio.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/pcm_audio_backend.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/research_audio_playback.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/research_range.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/builtin/fm_research.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/builtin/fm_live.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/builtin/fm_playback.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/builtin/iq_overview.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/research/builtin/lora_study.cpp")
target_link_libraries(signalscope_desktop_research
    PUBLIC signalscope_iq Qt6::Gui Qt6::Widgets
    PRIVATE signalscope_modulation signalscope_dsp signalscope_desktop_parameters signalscope_distribution signalscope_media_protocol signalscope_transport kissfft::kissfft)
if(TARGET Qt6::Multimedia)
  target_sources(signalscope_desktop_research PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/research/research_pcm_source.cpp")
  target_link_libraries(signalscope_desktop_research PRIVATE Qt6::Multimedia)
  target_compile_definitions(signalscope_desktop_research PRIVATE SIGNALSCOPE_HAS_AUDIO=1)
endif()
if(TARGET signalscope-media AND TARGET signalscope_dtmb_receiver)
  target_sources(signalscope_desktop_research PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/research/builtin/dtmb_tv.cpp")
  target_link_libraries(signalscope_desktop_research PRIVATE signalscope_dtmb_receiver)
  target_compile_definitions(signalscope_desktop_research PRIVATE SIGNALSCOPE_HAS_TELEVISION=1)
  add_dependencies(signalscope_desktop_research signalscope-media)
  file(GLOB catalogs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/research/builtin/dtmb_tv/i18n/*.json")
  foreach(catalog IN LISTS catalogs)
    get_filename_component(locale "${catalog}" NAME)
    configure_file("${catalog}" "${PROJECT_BINARY_DIR}/resources/dtmb-tv/i18n/${locale}" COPYONLY)
  endforeach()
  install(DIRECTORY research/builtin/dtmb_tv/i18n/ DESTINATION share/signalscope/dtmb-tv/i18n COMPONENT SignalScope)
  if(APPLE)
    install(DIRECTORY research/builtin/dtmb_tv/i18n/ DESTINATION Foreshadow.app/Contents/Resources/signalscope/dtmb-tv/i18n COMPONENT SignalScope)
  endif()
endif()
file(GLOB media_playback_catalogs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/research/media_playback/i18n/*.json")
foreach(catalog IN LISTS media_playback_catalogs)
  get_filename_component(locale "${catalog}" NAME)
  configure_file("${catalog}" "${PROJECT_BINARY_DIR}/resources/media-playback/i18n/${locale}" COPYONLY)
endforeach()
install(DIRECTORY research/media_playback/i18n/ DESTINATION share/signalscope/media-playback/i18n COMPONENT SignalScope)
if(APPLE)
  install(DIRECTORY research/media_playback/i18n/ DESTINATION Foreshadow.app/Contents/Resources/signalscope/media-playback/i18n COMPONENT SignalScope)
endif()

file(GLOB fm_live_catalogs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/research/builtin/fm_live/i18n/*.json")
foreach(catalog IN LISTS fm_live_catalogs)
  get_filename_component(locale "${catalog}" NAME)
  configure_file("${catalog}" "${PROJECT_BINARY_DIR}/resources/fm-live/i18n/${locale}" COPYONLY)
endforeach()
install(DIRECTORY research/builtin/fm_live/i18n/ DESTINATION share/signalscope/fm-live/i18n COMPONENT SignalScope)
if(APPLE)
  install(DIRECTORY research/builtin/fm_live/i18n/ DESTINATION Foreshadow.app/Contents/Resources/signalscope/fm-live/i18n COMPONENT SignalScope)
endif()

foreach(target IN ITEMS signalscope_desktop_display signalscope_desktop_research)
  target_include_directories(${target} PUBLIC "${PROJECT_SOURCE_DIR}")
  if(NOT MSVC)
    target_compile_options(${target} PRIVATE $<$<CONFIG:Release>:-O1>)
  endif()
endforeach()

file(GLOB waveform_catalogs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/research/waveform/i18n/*.json")
foreach(catalog IN LISTS waveform_catalogs)
  get_filename_component(locale "${catalog}" NAME)
  configure_file("${catalog}" "${PROJECT_BINARY_DIR}/resources/waveform-laboratory/i18n/${locale}" COPYONLY)
endforeach()
install(DIRECTORY research/waveform/i18n/ DESTINATION share/signalscope/waveform-laboratory/i18n COMPONENT SignalScope)
if(APPLE)
  install(DIRECTORY research/waveform/i18n/ DESTINATION Foreshadow.app/Contents/Resources/signalscope/waveform-laboratory/i18n COMPONENT SignalScope)
endif()
