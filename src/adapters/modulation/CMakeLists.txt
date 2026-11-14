add_library(signalscope_modulation
    lora/lora.cpp
    lora/wideband_lora.cpp
    narrowband/fsk.cpp
    psk/psk.cpp
    narrowband/four_fsk.cpp
    ofdm/ofdm.cpp
    css/css.cpp
    dtmb/detector.cpp
    fm/fm_audio.cpp
    fm/live_fm_audio.cpp
    registry/snapshot_detection.cpp
    registry/snapshot_registry.cpp)
target_link_libraries(signalscope_modulation PUBLIC signalscope_iq PRIVATE kissfft::kissfft signalscope_dsp)

option(SIGNALSCOPE_FFTW "Use FFTW when installed; otherwise use KissFFT" ON)
if(SIGNALSCOPE_FFTW)
  find_path(FFTW3F_INCLUDE_DIR fftw3.h)
  find_library(FFTW3F_LIBRARY NAMES fftw3f)
endif()

file(GLOB dtmb_catalogs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/dtmb/i18n/*.json")
foreach(catalog IN LISTS dtmb_catalogs)
  get_filename_component(locale "${catalog}" NAME)
  configure_file("${catalog}" "${PROJECT_BINARY_DIR}/resources/dtmb-detection/i18n/${locale}" COPYONLY)
endforeach()
install(DIRECTORY dtmb/i18n/ DESTINATION share/signalscope/dtmb-detection/i18n COMPONENT SignalScope)
if(APPLE)
  install(DIRECTORY dtmb/i18n/ DESTINATION Foreshadow.app/Contents/Resources/signalscope/dtmb-detection/i18n COMPONENT SignalScope)
endif()
if(SIGNALSCOPE_FFTW AND FFTW3F_INCLUDE_DIR AND FFTW3F_LIBRARY)
  target_include_directories(signalscope_modulation PRIVATE ${FFTW3F_INCLUDE_DIR})
  target_link_libraries(signalscope_modulation PRIVATE ${FFTW3F_LIBRARY})
  target_compile_definitions(signalscope_modulation PRIVATE SIGNALSCOPE_FFTW=1)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(VOLK QUIET IMPORTED_TARGET volk)
  endif()
  if(TARGET PkgConfig::VOLK)
    target_link_libraries(signalscope_modulation PRIVATE PkgConfig::VOLK)
    target_compile_definitions(signalscope_modulation PRIVATE SIGNALSCOPE_VOLK=1)
    message(STATUS "Vector DSP backend: VOLK")
  endif()
  message(STATUS "FFT backend: FFTW3f")
else()
  message(STATUS "FFT backend: KissFFT")
endif()

target_sources(signalscope_modulation PRIVATE registry/descriptor.cpp)
file(GLOB modulation_catalogs CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/i18n/*.json")
foreach(catalog IN LISTS modulation_catalogs)
  get_filename_component(locale "${catalog}" NAME)
  configure_file("${catalog}" "${PROJECT_BINARY_DIR}/resources/modulation/i18n/${locale}" COPYONLY)
endforeach()
install(DIRECTORY i18n/ DESTINATION share/signalscope/modulation/i18n COMPONENT SignalScope)
if(APPLE)
  install(DIRECTORY i18n/ DESTINATION Foreshadow.app/Contents/Resources/signalscope/modulation/i18n COMPONENT SignalScope)
endif()
