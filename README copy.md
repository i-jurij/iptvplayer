# IPTV Player — Build Instructions for Linux

This project uses locally built dependencies:

- wxWidgets 3.3.2 (from the official source archive) with builtin libwebp
- libcurl, Expat, rapidjson

The project root is assumed to be:

    ${PROJECT_ROOT}/iptvplayer

## 1. Requirements

Install required packages (example for Debian/Ubuntu):

    sudo apt update
    sudo apt install -y build-essential cmake pkg-config libcurl4-openssl-dev libgtk-3-dev autoconf automake libtool  

You need:

- A C++20‑capable compiler
- CMake ≥ 3.20

## 2. Installing wxWidgets 3.3.2 (local build)

> **Note:** This section describes what the `setup-deps.sh` script does automatically.  
> Most users should simply run `./setup-deps.sh` instead of following these manual steps.

### 2.1. Download wxWidgets 3.3.2

Official archive:

    https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.2/wxWidgets-3.3.2.tar.bz2

Assume it is saved as:

    ~/Downloads/wxWidgets-3.3.2.tar.bz2

### 2.2. Extract into third_party/wx

    cd ${PROJECT_ROOT}/third_party
    rm -rf wx
    mkdir wx
    cd wx
    tar xf ~/Downloads/wxWidgets-3.3.2.tar.bz2 && echo "Extracted" || (echo "Extraction failed"; exit 1)
    mv wxWidgets-3.3.2 src
    mkdir build
    mkdir install
    cd build

### 2.3. Configure wxWidgets

    cmake ../src \
      -DCMAKE_BUILD_TYPE=Release \
      -DwxBUILD_SHARED=OFF \
      -DwxUSE_LIBWEBP=builtin \
      -DCMAKE_INSTALL_PREFIX=../install

### 2.4. Build and install

Building wxWidgets requires ~2.5 GB of disk space and up to 10 minutes of time.

    cmake --build . --target install -j8

wxWidgets will be installed into:

    ${PROJECT_ROOT}/third_party/wx/install

After this step, the dependencies are ready for building the main application.  
The same result can be achieved by running `./setup-deps.sh` from the project root.

## 3. Building iptvplayer

### 3.1. Project configuration

The provided CMakeLists.txt already contains:

- wxWidgets lookup in ${PROJECT_ROOT}/third_party/wx/install with linking against static libwebp

### 3.2. Build

    cd ${PROJECT_ROOT}
    rm -rf build
    mkdir build
    cd build
    cmake .. -DCMAKE_BUILD_TYPE=Debug
    cmake --build . -j8

The resulting binary will be:

    ${PROJECT_ROOT}/build/iptvplayer

## 4. Running

    cd ${PROJECT_ROOT}/build
    ./iptvplayer

## 5. For developers: IDE support

After the build, the `compile_commands.json` file will be generated — it is needed for:

- VSCode (`C/C++` extension)
- CLion
- `clangd`, `ccls`

It is automatically created thanks to:

    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

## 6. Developer Automation Scripts

To simplify setup and build process, the following helper scripts are provided:

### 6.1. setup-deps.sh — Install Dependencies Automatically

This script downloads and builds all required dependencies locally:

- wxWidgets 3.3.2 (static build)

**Usage:**

    ./setup-deps.sh

⚠️ Requires: cmake, wget or curl, make, gcc/g++, ninja (optional).

The dependencies will be installed into:

    third_party/wx/install/

These paths are already configured in CMakeLists.txt.

### 6.2. build-release.sh — Build Release Version

Builds the project in Release mode and installs it into `install/` directory.

Usage:

    ./build-release.sh

To clean previous build:

    ./build-release.sh clean

After successful build, the portable app will be available at:

    install/bin/iptvplayer

✅ Includes icons and generates `compile_commands.json` for IDE support.

### 6.3. VSCode Development Support

For best experience with VSCode, install these extensions:

- **clangd** (recommended)
- Or: **C/C++ by Microsoft**

Using clangd (Recommended):

Install clangd:

    sudo apt install clangd-16  # Ubuntu/Debian

Install the clangd extension in VSCode.
Open the project — it will automatically use `compile_commands.json`.
No extra configuration needed!

Using C/C++ Extension:

If you prefer Microsoft's C/C++ extension, the file `.vscode/c_cpp_properties.json` is included and preconfigured.

🔁 The `compile_commands.json` file is copied to the project root after each build for easy access.

## 7. Summary of Commands

| Task                    | Command |
| :---------------------- | :------ |
| Setup dependencies      | `./setup-deps.sh` |
| Build debug version     | `cmake -S . -B build && cmake --build build` |
| Build release version   | `./build-release.sh` |
| Clean and rebuild       | `./build-release.sh clean` |
| Run app                 | `cd install/bin && ./iptvplayer` |

---

## App Settings

Config file location: `~/.config/iptvplayer/config.json`

The config file is created automatically on first run with default settings.

## Directory structure

All user data is stored in the user's home directory:

| Path | Purpose |
| :--- | :--- |
| `~/.config/iptvplayer/config.json` | Main application settings |
| `~/.config/iptvplayer/playlists/*.json` | Playlist metadata (JSON) |
| `~/.config/iptvplayer/favorites.json` | List of favorite channels |
| `~/.cache/iptvplayer/icons/` | Cached channel logos (disk) |
| `~/.cache/iptvplayer/epg/` | Cached EPG data (JSON) |

---

## Features (for end users)

- **Playlist management**: add local or remote M3U playlists; edit, update, remove.
- **Channel view**: list or grid (cards), with sorting and search.
- **Favorites**: mark channels as favorites, view separately.
- **EPG (Electronic Program Guide)**:
  - Configure multiple XMLTV sources in Settings → EPG.
  - Auto‑update interval and cache expiration.
  - View current program in channel list / tooltip.
  - Dedicated **Program** tab with day navigation and program details.
  - Quick jump to EPG from channel context menu.
- **Video playback**: fullscreen, volume, mute, audio/subtitle tracks, speed control.
- **Recording**: record current stream (method configurable) to a user‑defined directory.
- **IPTV‑Org integration**: add playlists from the public IPTV‑Org repository, filtered by country, language, or category.

## Keyboard shortcuts (quick reference)

| Key | Action |
| :--- | :--- |
| `Space` | Play / Pause |
| `Left` / `Right` | Seek backward / forward (5s) |
| `Shift+Left/Right` | Seek backward / forward (30s) |
| `Ctrl+Left/Right` | Seek backward / forward (1s) |
| `Home` / `End` | Go to start / end |
| `Up` / `Down` | Volume up / down (5 steps) |
| `Ctrl+Up/Down` | Volume up / down (1 step) |
| `m` / `M` | Toggle mute |
| `[` / `]` | Decrease / increase playback speed (0.1) |
| `{` / `}` | Decrease / increase playback speed (0.5) |
| `Backspace` | Reset speed to 1.0 |
| `+` / `_` | Next / previous audio track |
| `v` / `V` | Toggle subtitles |
| `j` / `J` | Next subtitle track |
| `h` / `H` | Previous subtitle track |
| `f` / `F` | Toggle fullscreen (on Video tab) |
| `ESC` | Exit fullscreen |
| `q` / `Q` | Stop playback |

Full documentation is available in the **About** dialog.

---

## Additional notes

- The application uses **wxWidgets 3.3.2**; ensure you have the correct version.
- For Windows and macOS, the same directory structure applies but paths differ (e.g., `%APPDATA%\iptvplayer\`, `~/Library/Application Support/iptvplayer/`).
- EPG sources are managed in Settings → EPG; add at least one source for the EPG features to work.

---

**You're now ready to develop, use, and distribute iptvplayer!**
