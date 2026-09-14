# IPTV Player

> **Binary built on Ubuntu 24.04 (glibc 2.39).**  
> For local builds, use `./build-package.sh --help`.
> It can build a minimal-size package for your system.  
> Tested only on Debian 13.
>
> The bundled `.deb` from GitHub should theoretically run on most Debian-family
> systems with glibc >= 2.39 (Ubuntu 24.04+, Debian 13+), and the bundled `.rpm`
> on most RPM-based distributions of the Red Hat family (Fedora, Rocky, RHEL,
> AlmaLinux, openSUSE) with a compatible glibc. The AppImage is self-contained
> and should work on any system with glibc >= 2.39.

## Screenshots

### Playlists

<a href="screenshots/playlists.png"><img src="screenshots/playlists.png" width="720" alt="Playlists"></a>

### Channels

<a href="screenshots/channels.png"><img src="screenshots/channels.png" width="720" alt="Channels"></a>

### Favorites

<a href="screenshots/favorites.png"><img src="screenshots/favs.png" width="720" alt="Favorites"></a>

### Video Player

<a href="screenshots/video.png"><img src="screenshots/video.png" width="720" alt="Video player"></a>

### EPG Program Settings

<a href="screenshots/program.png"><img src="screenshots/program.png" width="720" alt="EPG Program"></a>

---

## 1. Building a package for your system (recommended)

Everything is driven by one script:

    ./build-package.sh

Run it without arguments and you get an interactive menu tailored to your
distribution (Debian/Ubuntu, Fedora/Rocky/RHEL/openSUSE, Arch/Manjaro).
Pick a number — the script takes care of the rest: it builds the binary if
needed, prepares a staging tree, and produces the package in `dist/`.

Typical flows:

    # Interactive menu — choose what to build
    ./build-package.sh

    # .deb from system libraries (Debian/Ubuntu)
    ./build-package.sh --native-deb

    # .rpm from system libraries (Fedora/Rocky/RHEL/openSUSE)
    ./build-package.sh --native-rpm

    # .pkg.tar.zst (Arch/Manjaro)
    ./build-package.sh --native-arch

    # AppImage — works on any distribution
    ./build-package.sh --appimage

Use `./build-package.sh --help` for the full list of options.

**Which variant should I pick?**

- **Native** (`.deb` / `.rpm` / `.pkg.tar.zst`) — smallest download, but
  depends on libraries already present in your distribution. Only offered
  when the script recognises your system.
- **AppImage** — self-contained, runs on any glibc >= 2.39 system, no
  installation required.
- **Bundled** (`.deb` / `.rpm`) — self-contained packages that install
  everything under `/opt/iptvplayer`. These are separate from the native
  variants and are **not** offered in the interactive menu; build them
  explicitly with `--bundle-deb` / `--bundle-rpm` if you need them.

---

## 2. How the scripts fit together

There are three scripts. You normally only touch the first one.

| Script | Role | When to run it |
| :--- | :--- | :--- |
| `setup-deps.sh` | Installs system packages, then builds wxWidgets and wxSQLite3 statically into `third_party/`. | Once, before the first build. `build-package.sh` does **not** run it for you. |
| `build-release.sh` | Configures CMake, builds the binary, installs into `--prefix` (default `install/`). Does not package. | Automatically called by `build-package.sh`, or manually for a plain build. |
| `build-package.sh` | Orchestrator. Calls `build-release.sh` if the binary is missing, then produces the requested artifacts in `dist/`. | Whenever you want a package. |

**Order of operations, in plain terms:**

1. `setup-deps.sh` — one-time prep, produces static dependencies.
2. `build-package.sh <flags>` —
   - reads `METAINFO_NAME`, detects architecture (`DEB_ARCH`, `RPM_ARCH`, `APPIMAGE_ARCH`) and distribution (`DISTRO`);
   - checks for required tools (`dpkg-deb`, `rpmbuild`, `wget`, `tar`, `gpg`, …);
   - if `install/bin/iptvplayer` is missing (or `--rebuild` given) — calls `build-release.sh --type release --prefix ./install --yes`;
   - reads `install/VERSION{,_FULL,_FILE}`;
   - for bundled/AppImage: populates `iptvplayer.AppDir` via `linuxdeploy` + GTK plugin (downloaded on demand);
   - builds the requested artifacts into `dist/`;
   - optionally signs them (if `GPG_KEY_ID` is set) and writes `dist/checksums.txt`.
3. Result: `dist/` contains the artifacts listed at the end of the run.

Note: `build-package.sh` cleans up `pkg-staging/`, `iptvplayer.AppDir/` and
`pkg-rpm/` on exit. `dist/` is preserved (unless you pass `--clean`).

---

## 3. Installing dependencies (manual step)

Before the first package build, run:

    ./setup-deps.sh

It will:

- detect your OS and package manager (`apt`, `dnf`/`yum`, `pacman`);
- install the required development packages;
- download **wxWidgets 3.3.2** and build it statically with builtin libwebp;
- download **wxSQLite3 5.0.1** and build it statically against the local wxWidgets.

Both are installed into:

    third_party/wx/install/
    third_party/wxsqlite3/install/

These paths are already configured in `CMakeLists.txt`.

Options:

    ./setup-deps.sh                  # interactive
    ./setup-deps.sh --yes            # non-interactive
    ./setup-deps.sh --skip-system    # only rebuild third_party
    ./setup-deps.sh --yes --rebuild-deps

> On a fresh system you will also need `build-essential cmake pkg-config
> libcurl4-openssl-dev libgtk-3-dev autoconf automake libtool` (or the
> equivalents for your distribution) before `setup-deps.sh` can do its job.
> The script installs them for you when it recognises your package manager.

---

## 4. Manual build (step by step)

This is the low-level path, useful if you want to understand what the scripts
do or if you need a build without packaging.

1. **Install dependencies** (see §3):

        ./setup-deps.sh

2. **Configure and build** with CMake:

        mkdir -p build && cd build
        cmake .. -DCMAKE_BUILD_TYPE=Release \
                 -DCMAKE_INSTALL_PREFIX="$PWD/../install"
        cmake --build . -j"$(nproc)"

3. **Install** the binary, icons and resources:

        cmake --build . --target install

4. **Run**:

        cd ../install/bin && ./iptvplayer

Or use the wrapper script, which does all of the above in one shot:

    ./build-release.sh                       # Release, install to ./install
    ./build-release.sh --type debug          # Debug build
    ./build-release.sh --clean --type debug  # Clean rebuild
    ./build-release.sh --prefix /tmp/ip       # Custom install prefix
    ./build-release.sh --log                 # Save build log

### `build-release.sh` options

| Option | Description |
| :----- | :---------- |
| `--clean` | Remove previous build and install directories before building |
| `--type release\|debug` | Build type (default: `release`) |
| `--prefix PATH` | Installation directory (default: `install/` in project root) |
| `--log` | Save build log to a timestamped file |
| `--yes`, `-y` | Non-interactive mode |
| `-h`, `--help` | Show help |

**What it does:**

1. Checks that `third_party/wx/install/lib/…` and `third_party/wxsqlite3/install/lib/…` exist; warns if not.
2. Creates `build-release/` or `build-debug/`.
3. Configures CMake with the chosen type and install prefix.
4. Builds (uses `ninja` if available, otherwise `make`).
5. Installs the executable, icons, resources, `VERSION{,_FULL,_FILE}`, metainfo, license.
6. `strip`s the binary in Release/MinSizeRel builds.
7. Copies `compile_commands.json` to the project root for IDE support.

### `build-package.sh` options (full list)

| Option | Description |
| :----- | :---------- |
| `--native-deb` | Native `.deb` from system libraries (Debian/Ubuntu) |
| `--native-rpm` | Native `.rpm` from system libraries (Fedora/Rocky/RHEL/openSUSE) |
| `--native-arch` | Native `.pkg.tar.zst` (Arch/Manjaro) |
| `--appimage` | AppImage (bundled, works everywhere) |
| `--bundle-deb` | Bundled `.deb` (everything under `/opt/iptvplayer`) |
| `--bundle-rpm` | Bundled `.rpm` (everything under `/opt/iptvplayer`) |
| `--native` | All native packages available on this system |
| `--native-appimage` | Native package for the current system + AppImage (rarely needed; AppImage alone already covers other systems) |
| `--bundle` | Bundled `.deb` + bundled `.rpm` |
| `--all` | Everything possible on this system |
| `--rebuild` | Force rebuild of the binary via `build-release.sh` |
| `--clean` | Wipe `dist/` before building |
| `--clean-only` | Wipe `dist/` and exit |
| `--no-menu` | Do not show the interactive menu |
| `--yes`, `-y` | Non-interactive mode (implies `--no-menu`) |
| `-h`, `--help` | Show help |

If no flags are given and stdin is a TTY, `build-package.sh` shows a menu
tailored to your distribution. The menu offers native packages and the
AppImage. Bundled packages are built only when you ask for them explicitly
with `--bundle-deb` / `--bundle-rpm` / `--bundle` / `--all`.

> **Note on native vs. bundled `.rpm`:** they currently use the same file
> name, so building both into the same `dist/` will overwrite one with the
> other. Build them in separate runs (or clean `dist/` between them) if you
> need both.

Output goes to `dist/`:

    dist/
    ├── iptvplayer_<version>_<deb-arch>.deb              # bundled .deb
    ├── iptvplayer-<version>-1.<rpm-arch>.rpm            # bundled .rpm
    ├── iptvplayer-linux-<arch>-<version>.AppImage       # AppImage
    ├── iptvplayer-linux-<arch>-<version>.AppImage.asc   # detached GPG signature
    ├── iptvplayer-linux-<arch>-<version>.zsync          # if zsyncmake is present
    ├── checksums.txt
    ├── checksums.txt.asc
    └── public-key.asc
    # native variants additionally use the `_native_` marker:
    #   iptvplayer_<version>_native_<deb-arch>.deb
    #   iptvplayer-<version>-1.<rpm-arch>.rpm (native .rpm — same name as bundled)
    #   iptvplayer-<version>-1-<arch>.pkg.tar.zst

### Signing (optional)

If `GPG_KEY_ID` is set in the environment when `build-package.sh` runs, it
signs:

- `.deb` via `debsigs` (falls back to a detached `.asc`);
- `.rpm` via `rpm --addsign` (falls back to a detached `.asc`);
- `*.AppImage` via a detached `.asc`;
- `checksums.txt` via a detached `checksums.txt.asc`.

`public-key.asc` is not produced by the script; export it manually if you
need to distribute the key:

    gpg --armor --export "$GPG_KEY_ID" > dist/public-key.asc

---

## 5. IDE support

After any build, `compile_commands.json` is generated and copied to the
project root. This enables:

- **VSCode** with `clangd` or Microsoft C/C++ extension;
- **CLion**;
- **ccls**.

Install `clangd` (recommended):

    sudo apt install clangd-16   # Ubuntu/Debian

Then install the clangd extension in VSCode — it will use
`compile_commands.json` automatically.

---

## 6. Summary of common commands

| Task | Command |
| :--- | :--- |
| Install dependencies (once) | `./setup-deps.sh` |
| Build package for your system | `./build-package.sh` |
| Build native `.deb` | `./build-package.sh --native-deb` |
| Build native `.rpm` | `./build-package.sh --native-rpm` |
| Build native `.pkg.tar.zst` | `./build-package.sh --native-arch` |
| Build AppImage | `./build-package.sh --appimage` |
| Bundled `.deb` + `.rpm` | `./build-package.sh --bundle` |
| Just build the binary | `./build-release.sh` |
| Debug build | `./build-release.sh --type debug` |
| Clean rebuild | `./build-release.sh --clean` |
| Run the app from `install/` | `cd install/bin && ./iptvplayer` |

---

## 7. Application data directories

| Path | Purpose |
| :--- | :--- |
| `~/.config/iptvplayer/config.json` | Main settings (JSON) |
| `~/.config/iptvplayer/playlists/*.json` | Playlist metadata |
| `~/.config/iptvplayer/favorites.json` | Favorite channels |
| `~/.cache/iptvplayer/icons/` | Cached channel logos (disk) |
| `~/.config/iptvplayer/epg.db` | Cached EPG data (SQLite) |

The config file is created automatically on first run.

---

## 8. Features

- **Playlist management:** add local/remote M3U playlists, edit, update, remove.
- **Channel views:** list or grid (cards) with sorting and search.
- **Favorites:** mark channels, view separately.
- **EPG (Electronic Program Guide):**
  - XMLTV sources configuration (Settings → EPG);
  - auto-update interval and cache expiration;
  - program tab with day navigation and details;
  - quick jump from channel context menu.
- **Video playback:** fullscreen, volume, mute, audio/subtitle tracks, speed control.
- **Recording:** record current stream to a user-defined directory.
- **IPTV-Org integration:** add playlists from the public IPTV-Org repository.

---

## 9. Keyboard shortcuts (quick reference)

| Key | Action |
| :--- | :--- |
| `Space` | Play / Pause |
| `Left` / `Right` | Seek –5s / +5s |
| `Shift+Left/Right` | Seek –30s / +30s |
| `Ctrl+Left/Right` | Seek –1s / +1s |
| `Home` / `End` | Go to start / end |
| `Up` / `Down` | Volume +5 / –5 |
| `Ctrl+Up/Down` | Volume +1 / –1 |
| `m` / `M` | Toggle mute |
| `[` / `]` | Speed –0.1 / +0.1 |
| `{` / `}` | Speed –0.5 / +0.5 |
| `Backspace` | Reset speed |
| `+` / `_` | Next / previous audio track |
| `v` / `V` | Toggle subtitles |
| `j` / `J` | Next subtitle track |
| `h` / `H` | Previous subtitle track |
| `f` / `F` | Toggle fullscreen (on Video tab) |
| `ESC` | Exit fullscreen |
| `q` / `Q` | Stop playback |

Full documentation is available in the **About** dialog.

---

## 10. Notes

- The scripts are designed for Linux; Windows and macOS will have separate
  build instructions later.
- After a successful build, `install/` contains a ready-to-run tree; after a
  successful package build, `dist/` contains the artifacts.

---

**You are now ready to develop, use, and distribute iptvplayer!**
