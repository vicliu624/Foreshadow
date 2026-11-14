# Provider-owned legal declarations. This is a build/distribution assembly
# protocol, independent of the signed toolbox/installer protocol.
function(amc_compliance_declaration source)
  install(FILES "${source}" DESTINATION share/signalscope/compliance/providers COMPONENT SignalScope)
endfunction()

function(amc_compliance_build_facts)
  set(facts "${PROJECT_BINARY_DIR}/compliance/build-facts.json")
  file(WRITE "${facts}" "{\n  \"schema\": 1,\n  \"platform\": \"${AMC_DISTRIBUTION_PLATFORM}\",\n  \"qt\": \"${Qt6_VERSION}\",\n  \"openssl\": \"${OPENSSL_VERSION}\",\n  \"application\": \"${PROJECT_VERSION}\"\n}\n")
  install(FILES "${facts}" DESTINATION share/signalscope/compliance COMPONENT SignalScope)
endfunction()
