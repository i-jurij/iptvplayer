# =========================
# Player core (backend-agnostic)
# =========================
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

# =========================
# MPV backend (only backend)
# =========================
add_library(player_backend_mpv STATIC
    MpvBackend.h
    MpvBackend.cpp
)

# MPV
find_package(PkgConfig REQUIRED)
pkg_check_modules(MPV REQUIRED mpv)

target_include_directories(player_backend_mpv PRIVATE ${MPV_INCLUDE_DIRS})
target_link_libraries(player_backend_mpv PRIVATE ${MPV_LIBRARIES})

# OpenGL + Wayland EGL (только для mpv)
find_package(OpenGL REQUIRED COMPONENTS EGL)
find_library(WAYLAND_EGL_LIB wayland-egl REQUIRED)

target_link_libraries(player_backend_mpv
    PUBLIC
        player_core
        OpenGL::EGL
        ${WAYLAND_EGL_LIB}
)
