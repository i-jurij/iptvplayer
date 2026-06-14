# iptvplayer — Build Instructions for Linux

This project uses locally built dependencies:  

- wxWidgets 3.3.2 (from the official source archive) with buitin libwebp  

The project root is assumed to be:  

```text
${PROJECT_ROOT}/iptvplayer  
```

## 1. Requirements

Install required packages (example for Debian/Ubuntu):  

```sh
sudo apt update  
sudo apt install -y build-essential cmake pkg-config libcurl4-openssl-dev libgtk-3-dev  
```

You need:  

- A C++20‑capable compiler  
- CMake ≥ 3.20  

## 2. Installing wxWidgets 3.3.2 (local build)

### 2.1. Download wxWidgets 3.3.2

Official archive:  

<https://github.com/wxWidgets/wxWidgets/releases/download/v3.3.2/wxWidgets-3.3.2.tar.bz2>  

Assume it is saved as:  

~/Downloads/wxWidgets-3.3.2.tar.bz2  

### 2.2. Extract into third_party/wx

```sh
cd ${PROJECT_ROOT}/third_party  
rm -rf wx  
mkdir wx  
cd wx  
tar xf ~/Downloads/wxWidgets-3.3.2.tar.bz2 && echo "Extracted" || (echo "Extraction failed"; exit 1)  
mv wxWidgets-3.3.2 src  
mkdir build  
mkdir install  
cd build  
```

### 2.3. Configure wxWidgets

```sh
cmake ../src \  
  -DCMAKE_BUILD_TYPE=Release \  
  -DwxBUILD_SHARED=OFF \  
  -DwxUSE_LIBWEBP=builtin \  
  -DCMAKE_INSTALL_PREFIX=../install  
```

### 2.4. Build and install

Building wxWidgets requires ~2.5 GB of disk space and up to 10 minutes of time.  

```sh
cmake --build . --target install -j8  
```

wxWidgets will be installed into:  

```text
${PROJECT_ROOT}/third_party/wx/install  
```

## 3. Building iptvplayer

### 3.1. Project configuration

The provided CMakeLists.txt already contains:  

- wxWidgets lookup in ${PROJECT_ROOT}/third_party/wx/install with linking against static libwebp  

### 3.2. Build

```sh
cd ${PROJECT_ROOT}  
rm -rf build  
mkdir build  
cd build  
cmake .. -DCMAKE_BUILD_TYPE=Debug  
cmake --build . -j8  
```

The resulting binary will be:  

```text
${PROJECT_ROOT}/build/iptvplayer  
```

## 4. Running

```sh
cd ${PROJECT_ROOT}/build  
./iptvplayer
```

## 5. For developers: IDE support  

After the build, the 'compile_commands.json' file will be generated — it is needed for:  

- VSCode (`C/C++` extension)  
- CLion  
- `clangd`, `ccls`  
It is automatically created thanks to: `cmake set(CMAKE_EXPORT_COMPILE_COMMANDS ON)`

## 6. Developer Automation Scripts

To simplify setup and build process, the following helper scripts are provided:

### 6.1. `setup-deps.sh` — Install Dependencies Automatically

This script downloads and builds all required dependencies locally:  

- wxWidgets 3.3.2 (static build)  

**Usage:**

```bash
./setup-deps.sh
```

⚠️ Requires: cmake, wget or curl, make, gcc/g++, ninja (optional).  

The dependencies will be installed into:  

```text
third_party/wx/install/
These paths are already configured in CMakeLists.txt.
```

### 6.2. build-release.sh — Build Release Version

Builds the project in Release mode and installs it into install/ directory.  

Usage:

```Bash
./build-release.sh
```

To clean previous build:  

```Bash
./build-release.sh clean
```

After successful build, the portable app will be available at:

```text
install/bin/iptvplayer
```

✅ Includes icons and generates compile_commands.json for IDE support.

### 6.3. VSCode Development Support

For best experience with VSCode, install these extensions:  

clangd (recommended)  
Or: C/C++ by Microsoft  
Using clangd (Recommended)  
Install clangd:  

```Bash
sudo apt install clangd-16  # Ubuntu/Debian
```

Install the clangd extension in VSCode.  
Open the project — it will automatically use compile_commands.json.  

No extra configuration needed!  

Using C/C++ Extension  
If you prefer Microsoft's C/C++ extension, the file .vscode/c_cpp_properties.json is included and preconfigured.  

🔁 The compile_commands.json file is copied to the project root after each build for easy access.  

## 7. Summary of Commands

Task                    Command  
Setup dependencies     ./setup-deps.sh  
Build debug version     cmake -S . -B build && cmake --build build  
Build release version   ./build-release.sh  
Clean and rebuild       ./build-release.sh clean  
Run app                 cd install/bin && ./iptvplayer  

You're now ready to develop and distribute iptvplayer!

## App Settings

Config file location: ~/.config/iptvplayer/config.json  

The config file is created automatically on first run with default settings.
