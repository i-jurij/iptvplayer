cmake_minimum_required(VERSION 3.16)

# ------------------------
# System mpv (static)
# ------------------------
find_library(MPV_STATIC_LIB
    NAMES mpv libmpv
    PATHS
        /usr/lib
        /usr/lib64
        /usr/local/lib
        /usr/local/lib64
)

if(NOT MPV_STATIC_LIB)
    message(FATAL_ERROR "System libmpv not found (libmpv.a). Install libmpv-dev / mpv-devel.")
endif()

message(STATUS "Using system libmpv: ${MPV_STATIC_LIB}")

find_path(MPV_INCLUDE_DIRS
    NAMES mpv/client.h
    PATHS
        /usr/include
        /usr/local/include
)

if(NOT MPV_INCLUDE_DIRS)
    message(FATAL_ERROR "mpv include dir not found (mpv/client.h). Install mpv headers.")
endif()

message(STATUS "mpv include dir: ${MPV_INCLUDE_DIRS}")

# ------------------------
# OpenGL for mpv GL renderer
# Кросс-платформенно: на Windows find_package находит opengl32,
# на Linux — GL/GLX. EGL/Wayland специфичны для Linux.
# ------------------------
find_package(OpenGL REQUIRED)

set(_MPV_SYS_LIBS "")

if(NOT WIN32)
    # ------------------------
    # EGL / Wayland (Linux only)
    # ------------------------
    if(TARGET OpenGL::EGL)
        set(EGL_TARGET OpenGL::EGL)
    else()
        find_library(EGL_LIB EGL)
        if(EGL_LIB)
            set(EGL_TARGET ${EGL_LIB})
        else()
            message(WARNING "EGL not found; some mpv features may fail to link")
        endif()
    endif()

    find_library(WAYLAND_EGL_LIB NAMES wayland-egl libwayland-egl)
    if(NOT WAYLAND_EGL_LIB)
        message(WARNING "wayland-egl not found; Wayland EGL support may be unavailable")
    endif()

    # ------------------------
    # Static mpv dependencies (Linux only)
    #
    # На Windows libmpv — это import library (libmpv.dll.a) для libmpv-2.dll.
    # Все зависимости (ffmpeg, curl, …) вшиты в DLL, и линковать dl/m/pthread/
    # gbm/drm/wayland-* к нашему бинарнику не нужно — их там просто нет.
    # ------------------------
    if(MPV_STATIC_LIB MATCHES "\\.a$")
        message(STATUS "Linking additional system libraries for static libmpv")

        set(_GL_TARGET OpenGL::GL)

        if(DEFINED EGL_TARGET)
            set(_EGL_LIB ${EGL_TARGET})
        endif()

        find_library(DL_LIB dl)
        find_library(M_LIB m)
        find_library(PTHREAD_LIB pthread)
        find_library(GBM_LIB gbm)
        find_library(DRM_LIB drm)
        find_library(WAYLAND_CLIENT_LIB wayland-client)
        find_library(PLACEBO_LIB NAMES placebo libplacebo)

        list(APPEND _MPV_SYS_LIBS
            ${_EGL_LIB}
            ${_GL_TARGET}
            ${DL_LIB}
            ${M_LIB}
            ${PTHREAD_LIB}
            ${GBM_LIB}
            ${DRM_LIB}
            ${WAYLAND_CLIENT_LIB}
            ${WAYLAND_EGL_LIB}
            ${PLACEBO_LIB}
        )
        # Фильтр должен убирать не только пустые строки, но и значения
        # вида PLACEBO_LIB-NOTFOUND. Иначе они попадают в
        # target_link_libraries как -lPLACEBO_LIB-NOTFOUND и линковка
        # падает с «cannot find -lPLACEBO_LIB-NOTFOUND».
        list(FILTER _MPV_SYS_LIBS EXCLUDE REGEX "(^$|-NOTFOUND$)")
    endif()
endif()

# ------------------------
# player_core
# ------------------------
add_library(player_core STATIC
    IPlayerBackend.h
    PlayerController.h
    PlayerController.cpp
)

target_include_directories(player_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(player_core
    PUBLIC
        wx_all
)

# ------------------------
# MPV backend (unified: MpvBackend + MpvGLCanvas)
# ------------------------
add_library(player_backend_mpv STATIC
    MpvBackend.h
    MpvBackend.cpp
    MpvGLCanvas.h
    MpvGLCanvas.cpp
)

target_include_directories(player_backend_mpv
    PRIVATE
        ${MPV_INCLUDE_DIRS}
)

get_filename_component(MPV_LIB_FILENAME "${MPV_STATIC_LIB}" NAME)
message(STATUS "MPV library file: ${MPV_LIB_FILENAME}")

target_link_libraries(player_backend_mpv
    PUBLIC
        player_core
        wx_all
    PRIVATE
        ${MPV_STATIC_LIB}
        ${_MPV_SYS_LIBS}
)

# ------------------------
# LTO для целей плеера (опционально)
# ------------------------
option(IPTVPLAYER_ENABLE_LTO "Enable LTO for player targets" ON)

include(CheckIPOSupported)
check_ipo_supported(RESULT IPO_OK OUTPUT IPO_MSG)

if(IPO_OK AND IPTVPLAYER_ENABLE_LTO)
    foreach(_tgt player_core player_backend_mpv)
        if(TARGET ${_tgt})
            set_property(TARGET ${_tgt} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
            message(STATUS "LTO включён для ${_tgt}")
        endif()
    endforeach()
elseif(NOT IPTVPLAYER_ENABLE_LTO)
    message(STATUS "LTO отключён (IPTVPLAYER_ENABLE_LTO=OFF)")
else()
    message(WARNING "LTO не поддерживается: ${IPO_MSG}")
endif()