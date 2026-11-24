add_library(signalscope_core
    recordings/event_store.cpp
    recordings/recording_export.cpp
    recordings/recording_scan.cpp
    sessions/monitor.cpp
    sessions/reception.cpp
    sessions/receiver_configuration.cpp
    sessions/wideband_narrowband.cpp
    sessions/process_metrics.cpp
    sessions/replay_session.cpp
    sessions/snapshot_analysis.cpp)
target_link_libraries(signalscope_core PUBLIC signalscope_receivers signalscope_modulation signalscope_dsp)
add_library(signalscope_dsp dsp/spectrum.cpp dsp/channelizer.cpp dsp/waveform.cpp dsp/signal_processing.cpp dsp/window_research.cpp
    dsp/reference_model.cpp dsp/recovery.cpp dsp/recovery_export.cpp)
add_subdirectory("${PROJECT_SOURCE_DIR}/plugins/adsb" "${PROJECT_BINARY_DIR}/adsb_module")
add_subdirectory("${PROJECT_SOURCE_DIR}/plugins/wspr" "${PROJECT_BINARY_DIR}/wspr_module")
add_subdirectory("${PROJECT_SOURCE_DIR}/plugins/research/models" "${PROJECT_BINARY_DIR}/reference_models")
add_subdirectory("${PROJECT_SOURCE_DIR}/plugins/research/recovery" "${PROJECT_BINARY_DIR}/recovery_providers")
target_link_libraries(signalscope_dsp PUBLIC signalscope_iq PRIVATE kissfft::kissfft)
if(SIGNALSCOPE_FFTW AND FFTW3F_INCLUDE_DIR AND FFTW3F_LIBRARY)
  target_include_directories(signalscope_dsp PRIVATE ${FFTW3F_INCLUDE_DIR})
  target_link_libraries(signalscope_dsp PRIVATE ${FFTW3F_LIBRARY})
  target_compile_definitions(signalscope_dsp PRIVATE SIGNALSCOPE_DSP_FFTW=1)
endif()
