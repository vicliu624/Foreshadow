# Only the permissively licensed remote client is linked into receiver modules.
# Do not register/load local hardware modules through SoapySDR's global factory.
FetchContent_Declare(soapyremote
  URL https://codeload.github.com/pothosware/SoapyRemote/tar.gz/bd10a2e4a916bf66f0faa36dca7f043d4268138c
  URL_HASH SHA256=c225d0608ad299053f850fbe8e4ac227f4f920054fde8c422b31eb6d084289bd
  SOURCE_SUBDIR foreshadow-client-assembly)
FetchContent_MakeAvailable(soapyremote)
include("${PROJECT_SOURCE_DIR}/cmake/PatchPinnedSource.cmake")
set(amc_remote_patched "${PROJECT_BINARY_DIR}/remote-client-patched")
foreach(unit Settings LogAcceptor)
  amc_patch_pinned_source("${soapyremote_SOURCE_DIR}/client/${unit}.cpp"
    "${amc_remote_patched}/${unit}.cpp"
    "${PROJECT_SOURCE_DIR}/third_party/soapyremote-patches/${unit}.json")
endforeach()
set(SOAPY_REMOTE_VERSION "0.5.4-bd10a2e4")
set(ENABLE_AVAHI OFF)
add_subdirectory("${soapyremote_SOURCE_DIR}/common" "${soapyremote_BINARY_DIR}/common" EXCLUDE_FROM_ALL)
add_library(amc_remote_client OBJECT
  "${soapyremote_SOURCE_DIR}/client/Registration.cpp"
  "${amc_remote_patched}/Settings.cpp"
  "${soapyremote_SOURCE_DIR}/client/Streaming.cpp"
  "${amc_remote_patched}/LogAcceptor.cpp"
  "${soapyremote_SOURCE_DIR}/client/ClientStreamData.cpp"
  "${soapyremote_SOURCE_DIR}/client/DiscoverServers.cpp")
target_link_libraries(amc_remote_client PRIVATE SoapySDR SoapySDRRemoteCommon)
target_include_directories(amc_remote_client PRIVATE "${soapyremote_SOURCE_DIR}/common" "${soapyremote_SOURCE_DIR}/client")
target_link_libraries(amc-receiver-sdrplay PRIVATE amc_remote_client SoapySDRRemoteCommon)
if(MSVC)
  target_compile_definitions(SoapySDRRemoteCommon PRIVATE NOMINMAX)
  target_compile_definitions(amc_remote_client PRIVATE NOMINMAX)
endif()
# Native contracts use an independent, unmodified upstream service. It is not
# installed in the application or client toolbox. Operators install it separately.
include(CTest)
if(BUILD_TESTING)
  add_executable(amc-test-receiver-service
    "${soapyremote_SOURCE_DIR}/server/SoapyServer.cpp"
    "${soapyremote_SOURCE_DIR}/server/ServerListener.cpp"
    "${soapyremote_SOURCE_DIR}/server/ClientHandler.cpp"
    "${soapyremote_SOURCE_DIR}/server/LogForwarding.cpp"
    "${soapyremote_SOURCE_DIR}/server/ServerStreamData.cpp")
  target_link_libraries(amc-test-receiver-service PRIVATE SoapySDR SoapySDRRemoteCommon)
  set_property(TARGET amc-test-receiver-service PROPERTY CXX_STANDARD 11)
  target_include_directories(amc-test-receiver-service PRIVATE "${soapyremote_SOURCE_DIR}/server")
  if(WIN32)
    target_sources(amc-test-receiver-service PRIVATE "${soapyremote_SOURCE_DIR}/server/ThreadPrioWindows.cpp")
    target_include_directories(amc-test-receiver-service PRIVATE "${soapyremote_SOURCE_DIR}/server/msvc")
    target_compile_definitions(amc-test-receiver-service PRIVATE NOMINMAX)
  else()
    target_sources(amc-test-receiver-service PRIVATE "${soapyremote_SOURCE_DIR}/server/ThreadPrioUnix.cpp")
  endif()
endif()
