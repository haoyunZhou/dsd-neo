# Crypto module library
add_library(dsd-neo_crypto)

target_sources(
    dsd-neo_crypto
    PRIVATE
        crypt-aes.c
        crypt-csi72.c
        crypt-des.c
        crypt-ecdsa.c
        crypt-etc.c
        crypt-kirisun.c
        crypt-pc4.c
        crypt-pc5.c
        crypt-rc2.c
        crypt-rc4.c
        crypt-tyt.c
        dmr_mi.c
        md2ii.c
        nxdn_keystream.c
        tetra_tea.c
        tetra_etee.c
)

set(_dsd_tetra_crypto_src ${PROJECT_SOURCE_DIR}/tools/tetra/TETRA_crypto)
set(_dsd_tetra_etee_src ${PROJECT_SOURCE_DIR}/tools/tetra/TETRA_etee)
set(_dsd_tetra_crypto_dir ${PROJECT_SOURCE_DIR}/third_party/TETRA_crypto)
set(_dsd_tetra_etee_dir ${PROJECT_SOURCE_DIR}/third_party/TETRA_etee)
set(_dsd_tetra_crypto_inc ${PROJECT_SOURCE_DIR}/third_party/TETRA_crypto/include)
set(_dsd_tetra_etee_inc ${PROJECT_SOURCE_DIR}/third_party/TETRA_etee/include)
set(_dsd_tetra_compat ${PROJECT_SOURCE_DIR}/tools/tetra/gcc_compat.h)

if(WIN32)
    set(_dsd_tetra_os windows)
elseif(APPLE)
    set(_dsd_tetra_os macos)
else()
    set(_dsd_tetra_os linux)
endif()

# lib/<os>/<config> so Windows .lib and Unix .a, and Debug and Release, stay apart.
function(_dsd_tetra_archive_dirs target dest_root)
    if(CMAKE_CONFIGURATION_TYPES)
        foreach(_cfg DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
            string(TOLOWER "${_cfg}" _cfg_dir)
            set_property(
                TARGET ${target}
                PROPERTY "ARCHIVE_OUTPUT_DIRECTORY_${_cfg}"
                "${dest_root}/lib/${_dsd_tetra_os}/${_cfg_dir}"
            )
        endforeach()
    else()
        if(CMAKE_BUILD_TYPE)
            string(TOLOWER "${CMAKE_BUILD_TYPE}" _cfg_dir)
        else()
            set(_cfg_dir release)
        endif()
        set_target_properties(
            ${target}
            PROPERTIES
                ARCHIVE_OUTPUT_DIRECTORY "${dest_root}/lib/${_dsd_tetra_os}/${_cfg_dir}"
        )
    endif()
endfunction()

add_library(
    tetracrypto STATIC
    ${_dsd_tetra_crypto_src}/hurdle.c
    ${_dsd_tetra_crypto_src}/tea1.c
    ${_dsd_tetra_crypto_src}/tea2.c
    ${_dsd_tetra_crypto_src}/tea3.c
    ${_dsd_tetra_crypto_src}/taa1.c
    ${_dsd_tetra_crypto_src}/common.c
)
target_include_directories(tetracrypto PUBLIC ${_dsd_tetra_crypto_inc})
_dsd_tetra_archive_dirs(tetracrypto "${_dsd_tetra_crypto_dir}")
if(MSVC)
    target_compile_options(
        tetracrypto
        PRIVATE
            "/FI${_dsd_tetra_compat}"
            /wd4018
            /wd4098
            /wd4244
    )
else()
    target_compile_options(tetracrypto PRIVATE "-include" "${_dsd_tetra_compat}")
endif()

if(MSVC)
    find_program(
        DSD_TETRA_CLANG
        NAMES clang clang.exe
        HINTS
            "$ENV{ProgramFiles}/Microsoft Visual Studio/18/Enterprise/VC/Tools/Llvm/x64/bin"
            "$ENV{ProgramFiles}/Microsoft Visual Studio/18/Community/VC/Tools/Llvm/x64/bin"
            "$ENV{ProgramFiles}/LLVM/bin"
    )
    if(NOT DSD_TETRA_CLANG)
        message(FATAL_ERROR "TETRA_etee needs Clang on Windows. Install the VS LLVM tools or LLVM.")
    endif()
    set(_dsd_etee_obj "${CMAKE_CURRENT_BINARY_DIR}/tetraetee-obj")
    set(_dsd_etee_debug_lib "${_dsd_tetra_etee_dir}/lib/windows/debug/tetraetee.lib")
    set(_dsd_etee_release_lib "${_dsd_tetra_etee_dir}/lib/windows/release/tetraetee.lib")
    set(_dsd_etee_sources
        ${_dsd_tetra_etee_src}/etee_efuncs.c
        ${_dsd_tetra_etee_src}/etee_efuncs_aes.c
        ${_dsd_tetra_etee_src}/etee.c
        ${_dsd_tetra_etee_src}/common.c
    )
    add_custom_command(
        OUTPUT "${_dsd_etee_debug_lib}" "${_dsd_etee_release_lib}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_dsd_etee_obj}/debug" "${_dsd_etee_obj}/release"
            "${_dsd_tetra_etee_dir}/lib/windows/debug" "${_dsd_tetra_etee_dir}/lib/windows/release"
        COMMAND "${DSD_TETRA_CLANG}" -target x86_64-pc-windows-msvc -std=gnu11 -O2 -D_DEBUG
            -fms-runtime-lib=dll_dbg -c
            -I "${_dsd_tetra_etee_src}" -I "${_dsd_tetra_etee_inc}" -I "${OPENSSL_INCLUDE_DIR}"
            -include "${_dsd_tetra_compat}"
            -o "${_dsd_etee_obj}/debug/etee_efuncs.obj" "${_dsd_tetra_etee_src}/etee_efuncs.c"
        COMMAND "${DSD_TETRA_CLANG}" -target x86_64-pc-windows-msvc -std=gnu11 -O2 -D_DEBUG
            -fms-runtime-lib=dll_dbg -c
            -I "${_dsd_tetra_etee_src}" -I "${_dsd_tetra_etee_inc}" -I "${OPENSSL_INCLUDE_DIR}"
            -include "${_dsd_tetra_compat}"
            -o "${_dsd_etee_obj}/debug/etee_efuncs_aes.obj" "${_dsd_tetra_etee_src}/etee_efuncs_aes.c"
        COMMAND "${DSD_TETRA_CLANG}" -target x86_64-pc-windows-msvc -std=gnu11 -O2 -D_DEBUG
            -fms-runtime-lib=dll_dbg -c
            -I "${_dsd_tetra_etee_src}" -I "${_dsd_tetra_etee_inc}" -I "${OPENSSL_INCLUDE_DIR}"
            -include "${_dsd_tetra_compat}"
            -o "${_dsd_etee_obj}/debug/etee.obj" "${_dsd_tetra_etee_src}/etee.c"
        COMMAND "${DSD_TETRA_CLANG}" -target x86_64-pc-windows-msvc -std=gnu11 -O2 -D_DEBUG
            -fms-runtime-lib=dll_dbg -c
            -I "${_dsd_tetra_etee_src}" -I "${_dsd_tetra_etee_inc}" -I "${OPENSSL_INCLUDE_DIR}"
            -include "${_dsd_tetra_compat}"
            -o "${_dsd_etee_obj}/debug/common.obj" "${_dsd_tetra_etee_src}/common.c"
        COMMAND "${CMAKE_AR}" /nologo "/OUT:${_dsd_etee_debug_lib}"
            "${_dsd_etee_obj}/debug/etee_efuncs.obj"
            "${_dsd_etee_obj}/debug/etee_efuncs_aes.obj"
            "${_dsd_etee_obj}/debug/etee.obj"
            "${_dsd_etee_obj}/debug/common.obj"
        COMMAND "${DSD_TETRA_CLANG}" -target x86_64-pc-windows-msvc -std=gnu11 -O2
            -fms-runtime-lib=dll -c
            -I "${_dsd_tetra_etee_src}" -I "${_dsd_tetra_etee_inc}" -I "${OPENSSL_INCLUDE_DIR}"
            -include "${_dsd_tetra_compat}"
            -o "${_dsd_etee_obj}/release/etee_efuncs.obj" "${_dsd_tetra_etee_src}/etee_efuncs.c"
        COMMAND "${DSD_TETRA_CLANG}" -target x86_64-pc-windows-msvc -std=gnu11 -O2
            -fms-runtime-lib=dll -c
            -I "${_dsd_tetra_etee_src}" -I "${_dsd_tetra_etee_inc}" -I "${OPENSSL_INCLUDE_DIR}"
            -include "${_dsd_tetra_compat}"
            -o "${_dsd_etee_obj}/release/etee_efuncs_aes.obj" "${_dsd_tetra_etee_src}/etee_efuncs_aes.c"
        COMMAND "${DSD_TETRA_CLANG}" -target x86_64-pc-windows-msvc -std=gnu11 -O2
            -fms-runtime-lib=dll -c
            -I "${_dsd_tetra_etee_src}" -I "${_dsd_tetra_etee_inc}" -I "${OPENSSL_INCLUDE_DIR}"
            -include "${_dsd_tetra_compat}"
            -o "${_dsd_etee_obj}/release/etee.obj" "${_dsd_tetra_etee_src}/etee.c"
        COMMAND "${DSD_TETRA_CLANG}" -target x86_64-pc-windows-msvc -std=gnu11 -O2
            -fms-runtime-lib=dll -c
            -I "${_dsd_tetra_etee_src}" -I "${_dsd_tetra_etee_inc}" -I "${OPENSSL_INCLUDE_DIR}"
            -include "${_dsd_tetra_compat}"
            -o "${_dsd_etee_obj}/release/common.obj" "${_dsd_tetra_etee_src}/common.c"
        COMMAND "${CMAKE_AR}" /nologo "/OUT:${_dsd_etee_release_lib}"
            "${_dsd_etee_obj}/release/etee_efuncs.obj"
            "${_dsd_etee_obj}/release/etee_efuncs_aes.obj"
            "${_dsd_etee_obj}/release/etee.obj"
            "${_dsd_etee_obj}/release/common.obj"
        DEPENDS ${_dsd_etee_sources} "${_dsd_tetra_compat}"
        COMMENT "Build TETRA_etee static libraries with Clang"
        VERBATIM
    )
    add_custom_target(tetraetee_build DEPENDS "${_dsd_etee_debug_lib}" "${_dsd_etee_release_lib}")
    add_library(tetraetee STATIC IMPORTED GLOBAL)
    set_target_properties(
        tetraetee
        PROPERTIES
            IMPORTED_LOCATION "${_dsd_etee_release_lib}"
            IMPORTED_LOCATION_DEBUG "${_dsd_etee_debug_lib}"
            IMPORTED_LOCATION_RELEASE "${_dsd_etee_release_lib}"
            IMPORTED_LOCATION_RELWITHDEBINFO "${_dsd_etee_release_lib}"
            IMPORTED_LOCATION_MINSIZEREL "${_dsd_etee_release_lib}"
            INTERFACE_INCLUDE_DIRECTORIES "${_dsd_tetra_etee_inc}"
            INTERFACE_LINK_LIBRARIES OpenSSL::Crypto
    )
    add_dependencies(tetraetee tetraetee_build)
else()
    add_library(
        tetraetee STATIC
        ${_dsd_tetra_etee_src}/etee_efuncs.c
        ${_dsd_tetra_etee_src}/etee_efuncs_aes.c
        ${_dsd_tetra_etee_src}/etee.c
        ${_dsd_tetra_etee_src}/common.c
    )
    target_include_directories(tetraetee PUBLIC ${_dsd_tetra_etee_inc})
    target_compile_options(
        tetraetee
        PRIVATE
            -std=gnu11
            "-include" "${_dsd_tetra_compat}"
    )
    target_link_libraries(tetraetee PUBLIC OpenSSL::Crypto)
    _dsd_tetra_archive_dirs(tetraetee "${_dsd_tetra_etee_dir}")
endif()

target_include_directories(
    dsd-neo_crypto
    PUBLIC ${PROJECT_SOURCE_DIR}/include
    PRIVATE ${_PUBLIC_INCLUDES}
)

target_link_libraries(dsd-neo_crypto PUBLIC tetracrypto tetraetee)
target_link_libraries(dsd-neo_crypto PRIVATE dsd-neo_warnings OpenSSL::Crypto)
if(MSVC)
    add_dependencies(dsd-neo_crypto tetraetee_build)
endif()

install(
    TARGETS dsd-neo_crypto
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
