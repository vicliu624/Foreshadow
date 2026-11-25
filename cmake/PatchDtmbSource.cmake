include("${CMAKE_CURRENT_LIST_DIR}/PatchPinnedSource.cmake")
function(amc_dtmb_patch_source source output manifest)
  amc_patch_pinned_source("${source}" "${output}" "${manifest}")
endfunction()
