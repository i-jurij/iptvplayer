# =============================================================================
# windows/cmake/platform.cmake
# Windows-специфичная конфигурация (MSYS2 / MinGW-w64, окружение UCRT64).
# =============================================================================

if(NOT WIN32)
    message(FATAL_ERROR "platform.cmake is for Windows only")
endif()

if(NOT DEFINED ENV{MSYSTEM})
    message(FATAL_ERROR
        "Сборка должна запускаться из MSYS2. Откройте 'MSYS2 UCRT64' "
        "и запустите сборку оттуда.")
endif()

set(MSYS2_ENV "$ENV{MSYSTEM}")
message(STATUS "MSYS2 environment: ${MSYS2_ENV}")

if(MSYS2_ENV STREQUAL "UCRT64")
    set(MSYS2_PREFIX "/ucrt64")
else()
    message(FATAL_ERROR
        "Поддерживается только UCRT64. MINGW64 сворачивается с 2026-03-15 "
        "и не поддерживается. Текущее окружение: ${MSYS2_ENV}")
endif()

message(STATUS "MSYS2 prefix: ${MSYS2_PREFIX}")

if(MINGW)
    set(CMAKE_EXE_LINKER_FLAGS_INIT
        "${CMAKE_EXE_LINKER_FLAGS_INIT} -static-libgcc -static-libstdc++")
    message(STATUS "MinGW runtime: статическая линковка libgcc/libstdc++")
endif()

if(MINGW)
    add_compile_options(-Wall -Wextra)
    add_compile_definitions(
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
        _WIN32_WINNT=0x0A00
        WINVER=0x0A00
    )
endif()

function(win_deploy_runtime_dlls target)
    if(NOT MSYS2_PREFIX)
        return()
    endif()
    set(bin_dir "${MSYS2_PREFIX}/bin")
    if(NOT EXISTS "${bin_dir}")
        message(WARNING "MSYS2 bin не найден: ${bin_dir}")
        return()
    endif()
    set(core_dlls
        libmpv-2.dll
        libcurl-4.dll
        libexpat-1.dll
        libzstd.dll
        libbrotlidec.dll
        libbrotlicommon.dll
        libssl-3-x64.dll
        libcrypto-3-x64.dll
        libnghttp2-14.dll
        libssh2-1.dll
        libidn2-0.dll
        libunistring-5.dll
        libintl-8.dll
        libiconv-2.dll
        libwebp-7.dll
        libwebpdemux-2.dll
    )
    foreach(dll IN LISTS core_dlls)
        if(EXISTS "${bin_dir}/${dll}")
            add_custom_command(TARGET ${target} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${bin_dir}/${dll}"
                    "$<TARGET_FILE_DIR:${target}>/${dll}"
                COMMENT "Deploy: ${dll}"
                VERBATIM)
        endif()
    endforeach()
endfunction()