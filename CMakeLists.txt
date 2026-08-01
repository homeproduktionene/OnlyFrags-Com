cmake_minimum_required(VERSION 3.22)

project(OnlyFragsCom
    VERSION 1.0.0
    DESCRIPTION "OnlyFrags Com – Lightweight Voice Chat Client"
    LANGUAGES CXX)

# ── C++ standard ──────────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Find dependencies (provided by vcpkg) ─────────────────────────────────────
find_package(OpenSSL REQUIRED)
find_package(Opus    CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# ── Sources ───────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/Config.cpp
    src/Theme.cpp
    src/Network.cpp
    src/VoiceEngine.cpp
    src/LoginWindow.cpp
    src/MainWindow.cpp
    src/SettingsWindow.cpp
    src/OnlyFrags.rc
)

# ── Executable (WIN32 = no console window) ────────────────────────────────────
add_executable(OnlyFrags WIN32 ${SOURCES})

# ── Include paths ─────────────────────────────────────────────────────────────
target_include_directories(OnlyFrags PRIVATE src)

# ── Link libraries ────────────────────────────────────────────────────────────
target_link_libraries(OnlyFrags PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    Opus::opus
    nlohmann_json::nlohmann_json
    # Windows SDK libraries
    ws2_32        # Winsock
    winmm         # Windows Multimedia
    avrt          # Audio Video Real-time (WASAPI priority)
    ole32         # COM
    oleaut32      # COM automation
    comctl32      # Common Controls
    dwmapi        # Desktop Window Manager (dark title bar)
    uxtheme       # Visual styles
    shlwapi       # Shell utilities
    shcore        # DPI
    bcrypt        # BCrypt (SHA-256 in VoiceEngine)
    setupapi      # Device enumeration
)

# ── Compile definitions ───────────────────────────────────────────────────────
target_compile_definitions(OnlyFrags PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    UNICODE
    _UNICODE
    _WIN32_WINNT=0x0A00   # Windows 10
    WINVER=0x0A00
    _CRT_SECURE_NO_WARNINGS
)

# ─────────────────────────────────────────────────────────────────────────────
# MSVC-specific settings
# ─────────────────────────────────────────────────────────────────────────────
if(MSVC)
    # Warning level 4; treat specific warnings as errors
    target_compile_options(OnlyFrags PRIVATE
        /W3
        /utf-8
        /MP           # parallel compilation
        /Zi           # debug info even in Release (for crash reports)
    )
    # Embed manifest directly into the EXE (no external .manifest file needed)
    set_target_properties(OnlyFrags PROPERTIES
        LINK_FLAGS
        "/MANIFEST:EMBED /MANIFESTINPUT:${CMAKE_SOURCE_DIR}/manifest/app.manifest"
    )
    # Release: optimise for speed
    target_compile_options(OnlyFrags PRIVATE
        $<$<CONFIG:Release>:/O2 /GL>
    )
    target_link_options(OnlyFrags PRIVATE
        $<$<CONFIG:Release>:/LTCG>
    )
endif()

# ─────────────────────────────────────────────────────────────────────────────
# MinGW-specific settings  (for cross-compilation or GitHub Actions fallback)
# ─────────────────────────────────────────────────────────────────────────────
if(MINGW)
    target_compile_options(OnlyFrags PRIVATE -Wall -Wextra -Wno-unused-parameter)
    target_link_options(OnlyFrags PRIVATE
        -mwindows             # no console
        -static-libgcc
        -static-libstdc++
        -Wl,--subsystem,windows
    )
    # Embed manifest via windres (already handled through the .rc file)
endif()

# ─────────────────────────────────────────────────────────────────────────────
# Output name
# ─────────────────────────────────────────────────────────────────────────────
set_target_properties(OnlyFrags PROPERTIES
    OUTPUT_NAME "OnlyFrags"
)

# ─────────────────────────────────────────────────────────────────────────────
# Install rule  (optional – copies EXE to a flat "dist" folder)
# ─────────────────────────────────────────────────────────────────────────────
install(TARGETS OnlyFrags
    RUNTIME DESTINATION dist)
