# =============================================================================
# windows/cmake/deps.cmake
# Поиск зависимостей под Windows (MSYS2 UCRT64 / MINGW64).
# =============================================================================

if(NOT WIN32)
    message(FATAL_ERROR "deps.cmake is for Windows only")
endif()

# -----------------------------------------------------------------------------
# 1. wxWidgets
#
# gl — обязателен: wxGLCanvas используется в VideoPanel.
# Без него собранный бинарник неработоспособен.
#
# MSYS2-сборка wxWidgets иногда не экспортирует wx::gl как CMake-таргет
# даже при наличии библиотеки. Тогда находим wxmsw32u_gl.a вручную
# и создаём IMPORTED-таргет. Если и это не удаётся — FATAL_ERROR.
# -----------------------------------------------------------------------------
find_package(wxWidgets CONFIG REQUIRED COMPONENTS core base aui adv gl)

if(NOT wxWidgets_FOUND)
    message(FATAL_ERROR
        "wxWidgets не найден с компонентами core base aui adv gl. "
        "Проверьте пакет mingw-w64-ucrt-x86_64-wxwidgets3.2-msw.")
endif()

if(NOT TARGET wx::gl)
    find_library(WX_GL_LIB
        NAMES wxmsw32u_gl wxmsw31u_gl wxmsw30u_gl
        PATHS "${MSYS2_PREFIX}/lib"
        NO_DEFAULT_PATH)
    if(WX_GL_LIB)
        add_library(wx::gl UNKNOWN IMPORTED)
        set_target_properties(wx::gl PROPERTIES
            IMPORTED_LOCATION "${WX_GL_LIB}")
        message(STATUS "wx::gl собран вручную: ${WX_GL_LIB}")
    else()
        message(FATAL_ERROR
            "wx::gl недоступен, и wxmsw32u_gl.a не найден в ${MSYS2_PREFIX}/lib. "
            "wxGLCanvas требуется для VideoPanel — без него сборка бессмысленна.")
    endif()
endif()

message(STATUS "wxWidgets version: ${wxWidgets_VERSION}")
message(STATUS "wxWidgets dir:     ${wxWidgets_DIR}")

# -----------------------------------------------------------------------------
# 2. wxSQLite3
# -----------------------------------------------------------------------------
set(WIN_WXSQLITE3_ROOT
    "${CMAKE_SOURCE_DIR}/windows/third_party/wxsqlite3/install"
    CACHE PATH "Каталог локальной сборки wxSQLite3")

set(WIN_WXSQLITE3_INCLUDE_DIR "${WIN_WXSQLITE3_ROOT}/include")

file(GLOB WIN_WXSQLITE3_LIBS "${WIN_WXSQLITE3_ROOT}/lib/libwxcode*.a")
list(LENGTH WIN_WXSQLITE3_LIBS _wxsl_count)

if(NOT _wxsl_count EQUAL 1)
    message(FATAL_ERROR
        "Ожидался ровно один файл libwxcode*.a в ${WIN_WXSQLITE3_ROOT}/lib, "
        "найдено ${_wxsl_count}:\n  ${WIN_WXSQLITE3_LIBS}\n"
        "Запустите: windows/bats/setup-deps.bat")
endif()

set(WIN_WXSQLITE3_LIBRARY "${WIN_WXSQLITE3_LIBS}")
message(STATUS "wxSQLite3 library: ${WIN_WXSQLITE3_LIBRARY}")

add_library(wxsqlite3 STATIC IMPORTED)
set_target_properties(wxsqlite3 PROPERTIES
    IMPORTED_LOCATION             "${WIN_WXSQLITE3_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${WIN_WXSQLITE3_INCLUDE_DIR}"
)

# -----------------------------------------------------------------------------
# 3. libmpv
# -----------------------------------------------------------------------------
find_path(WIN_MPV_INCLUDE_DIR
    NAMES mpv/client.h
    PATHS "${MSYS2_PREFIX}/include"
    NO_DEFAULT_PATH)
if(NOT WIN_MPV_INCLUDE_DIR)
    message(FATAL_ERROR
        "mpv/client.h не найден в ${MSYS2_PREFIX}/include. "
        "Установите пакет mingw-w64-<env>-mpv через pacman.")
endif()

find_library(WIN_MPV_LIBRARY
    NAMES mpv libmpv
    PATHS "${MSYS2_PREFIX}/lib"
    NO_DEFAULT_PATH)
if(NOT WIN_MPV_LIBRARY)
    message(FATAL_ERROR
        "libmpv не найден в ${MSYS2_PREFIX}/lib. "
        "Установите пакет mingw-w64-<env>-mpv через pacman.")
endif()

message(STATUS "libmpv include: ${WIN_MPV_INCLUDE_DIR}")
message(STATUS "libmpv library: ${WIN_MPV_LIBRARY}")

add_library(mpv_win INTERFACE)
target_include_directories(mpv_win INTERFACE "${WIN_MPV_INCLUDE_DIR}")
target_link_libraries(mpv_win INTERFACE "${WIN_MPV_LIBRARY}")

# -----------------------------------------------------------------------------
# 4. WebP (libwebp)
# На Linux он приходит транзитивно через GTK, на Windows нужен явно.
# Сначала пробуем CMake-config, потом — ручной find_library.
# -----------------------------------------------------------------------------
find_package(WebP QUIET)

if(NOT WebP_FOUND)
    find_path(WIN_WEBP_INCLUDE_DIR
        NAMES webp/decode.h
        PATHS "${MSYS2_PREFIX}/include"
        NO_DEFAULT_PATH)
    find_library(WIN_WEBP_LIBRARY
        NAMES webp libwebp
        PATHS "${MSYS2_PREFIX}/lib"
        NO_DEFAULT_PATH)

    if(WIN_WEBP_INCLUDE_DIR AND WIN_WEBP_LIBRARY)
        add_library(WebP::webp UNKNOWN IMPORTED)
        set_target_properties(WebP::webp PROPERTIES
            IMPORTED_LOCATION             "${WIN_WEBP_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${WIN_WEBP_INCLUDE_DIR}"
        )
        message(STATUS "WebP найден вручную: ${WIN_WEBP_LIBRARY}")
    else()
        message(FATAL_ERROR
            "libwebp не найден. Установите: pacman -S mingw-w64-<env>-libwebp")
    endif()
endif()