# The DTMB protocol implementation is a private, pinned dependency. Its
# command line is only used by the owning receiver adapter, never by the GUI.
FetchContent_Declare(dtmb_decoder
  URL https://github.com/ningzichun/dtmb-sdr/archive/2cc94c012128d6fa3e8d6de456b3bb57e51990a9.tar.gz
  URL_HASH SHA256=87ecbf94ae4873bcc65c3a284ab4e2bfeecf26f2f8135a2f0ff2d98af8338c41)
# The archive has no root CMakeLists; populate it without running its optional
# native project's install rules or changing this project's build options.
FetchContent_MakeAvailable(dtmb_decoder)
set(dtmb_native "${dtmb_decoder_SOURCE_DIR}/core/cpp")
include("${CMAKE_CURRENT_LIST_DIR}/PatchDtmbSource.cmake")
set(dtmb_patched "${PROJECT_BINARY_DIR}/dtmb-native-patched")
amc_dtmb_patch_source("${dtmb_native}/include/dtmb/core.hpp" "${dtmb_patched}/include/dtmb/core.hpp"
  "${PROJECT_SOURCE_DIR}/third_party/dtmb-patches/pn595.json")
amc_dtmb_patch_source("${dtmb_native}/tools/ldpc_bch_decode.cpp" "${dtmb_patched}/ldpc_bch_decode.cpp"
  "${PROJECT_SOURCE_DIR}/third_party/dtmb-patches/fec-eof.json")
set(dtmb_helper_dir "${PROJECT_BINARY_DIR}/libexec/signalscope/dtmb")
add_library(amc_dtmb_native STATIC
  "${dtmb_native}/src/c1.cpp" "${dtmb_native}/src/c3780.cpp"
  "${dtmb_native}/src/core.cpp" "${dtmb_native}/src/nr.cpp"
  "${dtmb_native}/src/fec.cpp" "${dtmb_native}/src/frame_sync.cpp"
  "${dtmb_native}/src/pn_channel.cpp" "${dtmb_native}/src/resampler.cpp"
  "${dtmb_native}/src/iq.cpp" "${dtmb_native}/src/transport.cpp")
target_include_directories(amc_dtmb_native PUBLIC "${dtmb_patched}/include" "${dtmb_native}/include")
target_link_libraries(amc_dtmb_native PUBLIC Threads::Threads)
target_compile_features(amc_dtmb_native PUBLIC cxx_std_20)
find_path(DTMB_FFTW_INCLUDE_DIR fftw3.h)
find_library(DTMB_FFTW_LIBRARY NAMES fftw3f libfftw3f-3)
if(DTMB_FFTW_INCLUDE_DIR AND DTMB_FFTW_LIBRARY)
  target_include_directories(amc_dtmb_native PRIVATE "${DTMB_FFTW_INCLUDE_DIR}")
  target_link_libraries(amc_dtmb_native PUBLIC "${DTMB_FFTW_LIBRARY}")
  target_compile_definitions(amc_dtmb_native PRIVATE DTMB_CORE_HAVE_FFTW3F=1)
endif()
add_library(amc_dtmb_fec_support STATIC
  "${dtmb_native}/tools/ldpc_h_gate.cpp" "${dtmb_native}/tools/ldpc_cuda_backend.cpp")
target_include_directories(amc_dtmb_fec_support PUBLIC "${dtmb_native}/tools")
target_link_libraries(amc_dtmb_fec_support PUBLIC amc_dtmb_native ${CMAKE_DL_LIBS})
if(MSVC)
  set_target_properties(amc_dtmb_native amc_dtmb_fec_support PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
foreach(pair IN ITEMS "resample|${PROJECT_SOURCE_DIR}/src/adapters/television/dtmb/resample.cpp"
    "frontend|${dtmb_native}/tools/c3780_extract.cpp" "demap|${dtmb_native}/tools/deinterleave_qam64.cpp" "fec|${dtmb_patched}/ldpc_bch_decode.cpp")
  string(REPLACE "|" ";" parts "${pair}")
  list(GET parts 0 name)
  list(GET parts 1 source)
  add_executable(amc-dtmb-${name} "${source}")
  target_link_libraries(amc-dtmb-${name} PRIVATE amc_dtmb_fec_support)
  set_target_properties(amc-dtmb-${name} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${dtmb_helper_dir}"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${dtmb_helper_dir}"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${dtmb_helper_dir}")
  if(MSVC)
    set_target_properties(amc-dtmb-${name} PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  set_property(GLOBAL APPEND PROPERTY AMC_BUNDLE_PRIVATE_EXECUTABLES "amc-dtmb-${name}|libexec/signalscope/dtmb")
  install(TARGETS amc-dtmb-${name} RUNTIME DESTINATION libexec/signalscope/dtmb COMPONENT SignalScope)
  if(APPLE)
    install(TARGETS amc-dtmb-${name} RUNTIME DESTINATION Foreshadow.app/Contents/libexec/signalscope/dtmb COMPONENT SignalScope)
  endif()
endforeach()
foreach(rate IN ITEMS 1 2 3)
  set(alist "${dtmb_decoder_SOURCE_DIR}/python/dtmb/data/dtmb_ldpc_rate${rate}.alist")
  configure_file("${alist}" "${dtmb_helper_dir}/data/dtmb_ldpc_rate${rate}.alist" COPYONLY)
  set_property(GLOBAL APPEND PROPERTY AMC_BUNDLE_PRIVATE_FILES "${alist}|libexec/signalscope/dtmb/data/dtmb_ldpc_rate${rate}.alist")
  install(FILES "${alist}" DESTINATION libexec/signalscope/dtmb/data COMPONENT SignalScope)
  if(APPLE)
    install(FILES "${alist}" DESTINATION Foreshadow.app/Contents/libexec/signalscope/dtmb/data COMPONENT SignalScope)
  endif()
endforeach()
install(FILES "${dtmb_decoder_SOURCE_DIR}/LICENSE"
  DESTINATION share/licenses/ams/dtmb-sdr COMPONENT SignalScope)
install(FILES "${PROJECT_SOURCE_DIR}/third_party/dtmb-sampler-LICENSE" DESTINATION share/licenses/ams/dtmb-sdr COMPONENT SignalScope)
install(DIRECTORY "${PROJECT_SOURCE_DIR}/third_party/dtmb-patches/" DESTINATION share/licenses/ams/dtmb-sdr/patches COMPONENT SignalScope)
if(APPLE)
  install(FILES "${dtmb_decoder_SOURCE_DIR}/LICENSE"
    DESTINATION Foreshadow.app/Contents/Resources/licenses/dtmb-sdr COMPONENT SignalScope)
  install(FILES "${PROJECT_SOURCE_DIR}/third_party/dtmb-sampler-LICENSE" DESTINATION Foreshadow.app/Contents/Resources/licenses/dtmb-sdr COMPONENT SignalScope)
  install(DIRECTORY "${PROJECT_SOURCE_DIR}/third_party/dtmb-patches/" DESTINATION Foreshadow.app/Contents/Resources/licenses/dtmb-sdr/patches COMPONENT SignalScope)
endif()

if(BUILD_TESTING)
  add_executable(dtmb_symbol_sampler_test "${PROJECT_SOURCE_DIR}/tests/dtmb_symbol_sampler_test.cpp")
  target_include_directories(dtmb_symbol_sampler_test PRIVATE "${PROJECT_SOURCE_DIR}")
  target_link_libraries(dtmb_symbol_sampler_test PRIVATE amc_dtmb_native)
  if(MSVC)
    set_target_properties(dtmb_symbol_sampler_test PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
  add_test(NAME dtmb_symbol_sampler_contract COMMAND dtmb_symbol_sampler_test)
  set_tests_properties(dtmb_symbol_sampler_contract PROPERTIES TIMEOUT 15)
  foreach(test IN ITEMS c1 nr pn qam ldpc_h_gate)
    set(test_source "${dtmb_native}/tests/test_${test}.cpp")
    if(test STREQUAL "pn")
      set(test_source "${dtmb_patched}/test_pn.cpp")
      amc_dtmb_patch_source("${dtmb_native}/tests/test_pn.cpp" "${test_source}"
        "${PROJECT_SOURCE_DIR}/third_party/dtmb-patches/pn595-test.json")
    endif()
    add_executable(amc_dtmb_native_${test}_test "${test_source}")
    target_link_libraries(amc_dtmb_native_${test}_test PRIVATE amc_dtmb_fec_support)
    if(MSVC)
      target_compile_options(amc_dtmb_native_${test}_test PRIVATE /UNDEBUG /utf-8)
      set_target_properties(amc_dtmb_native_${test}_test PROPERTIES MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    else()
      target_compile_options(amc_dtmb_native_${test}_test PRIVATE -UNDEBUG)
    endif()
    target_compile_definitions(amc_dtmb_native_${test}_test PRIVATE
      DTMB_LDPC_RATE2_ALIST="${dtmb_decoder_SOURCE_DIR}/python/dtmb/data/dtmb_ldpc_rate2.alist")
    if(test STREQUAL "nr")
      add_test(NAME dtmb_native_${test} COMMAND amc_dtmb_native_${test}_test "${dtmb_native}/tests/nr_appendix_c.txt")
    else()
      add_test(NAME dtmb_native_${test} COMMAND amc_dtmb_native_${test}_test)
    endif()
  endforeach()
endif()
add_subdirectory("${PROJECT_SOURCE_DIR}/src/adapters/television/dtmb" "${PROJECT_BINARY_DIR}/src/adapters/television/dtmb")
