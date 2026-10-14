
add_library(dsd-neo_proto_tetra)

target_sources(dsd-neo_proto_tetra PRIVATE
  tetra.c
  tetra_uplink.c
  tetra_fec.c
  tetra_acelp.c
  tetra_mac.c
  tetra_mle.c
  tetra_mm.c
  tetra_trunk_sm.c
  tetra_tch_slot_gate.c
  tetra_bsch.c
  tetra_dmo.c
  tetra_air.c
  tetra_bsch_fmt.c
  tetra_channel_info.c
  osmo_sources/tetra_conv_enc.c

)

target_include_directories(dsd-neo_proto_tetra
  PUBLIC ${PROJECT_SOURCE_DIR}/include
)

target_link_libraries(dsd-neo_proto_tetra PUBLIC dsd-neo_core dsd-neo_crypto)

# Local ETSI EN 300 395-2 C-CODE is optional and is never fetched by the build.
# When it is present, speech is decoded in-process. TETRA_VOCODER_CMD remains
# an explicit override for tests and alternate codecs.
set(_DSD_TETRA_C_CODE_DEFAULT "")
if(EXISTS "${PROJECT_SOURCE_DIR}/third_party/tetra_acelp/reference/C-CODE/SOURCE.H")
    set(_DSD_TETRA_C_CODE_DEFAULT
        "${PROJECT_SOURCE_DIR}/third_party/tetra_acelp/reference/C-CODE"
    )
endif()
set(ETSI_TETRA_C_CODE
    "${_DSD_TETRA_C_CODE_DEFAULT}"
    CACHE PATH
    "Local ETSI EN 300 395-2 C-CODE directory. Empty leaves speech on TETRA_VOCODER_CMD."
)
if(EXISTS "${ETSI_TETRA_C_CODE}/SOURCE.H")
    set(_DSD_TETRA_ACELP_SOURCES
        tetra_acelp_builtin.c
        "${ETSI_TETRA_C_CODE}/SDEC_TET.C"
        "${ETSI_TETRA_C_CODE}/SUB_SC_D.C"
        "${ETSI_TETRA_C_CODE}/SUB_DSP.C"
        "${ETSI_TETRA_C_CODE}/FBAS_TET.C"
        "${ETSI_TETRA_C_CODE}/FEXP_TET.C"
        "${ETSI_TETRA_C_CODE}/FMAT_TET.C"
        "${ETSI_TETRA_C_CODE}/TETRA_OP.C"
    )
    # Shared library so the codec's generic symbols (add, round, ...) stay
    # inside this module instead of colliding with the rest of the link.
    add_library(dsd-neo_tetra_acelp SHARED ${_DSD_TETRA_ACELP_SOURCES})
    set_source_files_properties(
        ${_DSD_TETRA_ACELP_SOURCES} PROPERTIES LANGUAGE C
    )
    set_target_properties(dsd-neo_tetra_acelp PROPERTIES OUTPUT_NAME tetra_acelp)
    target_include_directories(
        dsd-neo_tetra_acelp PRIVATE "${ETSI_TETRA_C_CODE}"
    )
    if(MSVC)
        target_compile_definitions(
            dsd-neo_tetra_acelp PRIVATE _CRT_SECURE_NO_WARNINGS
        )
    else()
        target_compile_options(dsd-neo_tetra_acelp PRIVATE -fvisibility=hidden)
    endif()
    target_compile_definitions(
        dsd-neo_proto_tetra PRIVATE DSD_HAVE_TETRA_ACELP_BUILTIN=1
    )
    target_link_libraries(dsd-neo_proto_tetra PUBLIC dsd-neo_tetra_acelp)
    message(STATUS "TETRA ACELP: in-process decoder from ${ETSI_TETRA_C_CODE}")
else()
    message(STATUS "TETRA ACELP: no local C-CODE; speech uses TETRA_VOCODER_CMD")
endif()

function(dsd_neo_copy_tetra_acelp_runtime target)
    if(NOT TARGET dsd-neo_tetra_acelp)
        return()
    endif()
    add_custom_command(
        TARGET ${target}
        POST_BUILD
        COMMAND
            ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:dsd-neo_tetra_acelp>
            $<TARGET_FILE_DIR:${target}>
        VERBATIM
    )
endfunction()

# Small test executable to validate RCPC depuncture mapping (build with CMake)
add_executable(dsd-neo_tetra_depunct_test tetra_fec.c)
target_compile_definitions(dsd-neo_tetra_depunct_test PRIVATE TETRA_FEC_TEST_MAIN)
target_include_directories(dsd-neo_tetra_depunct_test PRIVATE ${PROJECT_SOURCE_DIR}/include)
add_executable(dsd-neo_tetra_depunct_test_real tetra_deep_diag.c
  osmo_sources/osmo_viterbi_shim.c osmo_sources/osmo_shim_map.c)
target_include_directories(dsd-neo_tetra_depunct_test_real PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(dsd-neo_tetra_depunct_test_real PRIVATE dsd-neo_core dsd-neo_proto_tetra dsd-neo_fec)

# Ensure the test is built into the same configuration output dir and provide
# a convenience target to run it via the build system.
add_custom_target(run-tetra-depunct-test
  COMMAND $<TARGET_FILE:dsd-neo_tetra_depunct_test>
  DEPENDS dsd-neo_tetra_depunct_test
  COMMENT "Run TETRA depuncture mapping test"
)
add_custom_target(run-tetra-depunct-test-real
  COMMAND $<TARGET_FILE:dsd-neo_tetra_depunct_test_real>
  DEPENDS dsd-neo_tetra_depunct_test_real
  COMMENT "Run TETRA depuncture mapping test (real viterbi)"
)
