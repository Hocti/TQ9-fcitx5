cmake_minimum_required(VERSION 3.16)
project(fcitx5-tq9)


set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_definitions(-DQT_NO_KEYWORDS)

find_package(ECM REQUIRED 1.0.0)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Prevent Qt5 and Qt6 from conflicting on versionless targets like Qt::Core
set(QT_NO_CREATE_VERSIONLESS_TARGETS ON)

find_package(Fcitx5Core REQUIRED)
find_package(Qt5 COMPONENTS Core Concurrent REQUIRED)
find_package(Qt6 COMPONENTS Core Widgets Gui Network Multimedia REQUIRED)
# LayerShellQt is optional - only available on systems with Qt6-compatible version
find_package(LayerShellQt QUIET)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

include_directories(${Fcitx5Core_INCLUDE_DIRS} ${SQLITE3_INCLUDE_DIRS})

include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_INCLUDES ${Fcitx5Core_INCLUDE_DIRS})
# Force C++17 check to see if headers break
set(CMAKE_REQUIRED_FLAGS "-std=c++17") 

check_cxx_source_compiles("
#include <fcitx-utils/log.h>
#include <fcitx-utils/standardpath.h>
int main() { return 0; }
" FCITX_COMPATIBLE_CXX17)

unset(CMAKE_REQUIRED_FLAGS)
unset(CMAKE_REQUIRED_INCLUDES)

if(FCITX_COMPATIBLE_CXX17)
    message(STATUS "Fcitx headers are compatible with C++17. Setting C++17 for wider compatibility.")
    set(CMAKE_CXX_STANDARD 17)
else()
    message(STATUS "Fcitx headers require C++20 (likely std::span). Setting C++20.")
    set(CMAKE_CXX_STANDARD 20)
endif()

add_library(tq9 MODULE
    src/addon.cpp
    src/CustomEngine.cpp
    src/CustomEngine.h
    src/Database.cpp
    src/Q9Logic.cpp
    src/Database.h
    src/UserDb.cpp
    src/UserDb.h
    src/ConfigLoader.cpp
    src/ConfigLoader.h
)

target_link_libraries(tq9
    Fcitx5::Core
    Qt5::Core
    Qt5::Concurrent
    ${SQLITE3_LIBRARIES}
)

# UI executable uses Qt6
add_executable(fcitx5-tq9-ui
    src/ui/main.cpp
    src/ui/FloatingWindow.cpp
    src/ui/FloatingWindow.h
    src/ui/CustomButton.cpp
    src/ui/CustomButton.h
    src/ui/stt/SttSettings.cpp
    src/ui/stt/SttSettings.h
    src/ui/stt/SttUsageLog.cpp
    src/ui/stt/SttUsageLog.h
    src/ui/stt/SttRawLog.cpp
    src/ui/stt/SttRawLog.h
    src/ui/stt/AudioRecorder.cpp
    src/ui/stt/AudioRecorder.h
    src/ui/stt/GeminiClient.cpp
    src/ui/stt/GeminiClient.h
    src/ui/stt/SttController.cpp
    src/ui/stt/SttController.h
    src/ui/stt/SettingsDialog.cpp
    src/ui/stt/SettingsDialog.h
    src/ui/stt/DialogSupport.cpp
    src/ui/stt/DialogSupport.h
    src/ConfigLoader.cpp
    src/ConfigLoader.h
)

# Base libraries for UI
target_link_libraries(fcitx5-tq9-ui
    Qt6::Core
    Qt6::Widgets
    Qt6::Gui
    Qt6::Network
    Qt6::Multimedia
    ${SQLITE3_LIBRARIES}
)

# Conditionally link LayerShellQt if found
if(LayerShellQt_FOUND)
    message(STATUS "LayerShellQt found - Wayland layer shell support enabled")
    target_link_libraries(fcitx5-tq9-ui LayerShellQt::Interface)
    target_compile_definitions(fcitx5-tq9-ui PRIVATE HAVE_LAYERSHELLQT)
else()
    message(STATUS "LayerShellQt not found - building without Wayland layer shell support")
endif()

target_include_directories(fcitx5-tq9-ui PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ui"
)

# Distros that build Qt6 with -mno-direct-extern-access (Arch among them) mark
# libQt6Core.so.6 with GNU_PROPERTY_1_NEEDED_INDIRECT_EXTERN_ACCESS and export
# their data symbols as protected. Every new-style connect() takes the address
# of a staticMetaObject, and a compiler that is free to assume direct access to
# external data makes the linker answer that with an R_X86_64_COPY relocation -
# a copy of the object in our own .bss. glibc refuses to resolve a copy
# relocation against a protected symbol, so the UI dies before main() with:
#
#   warning: copy relocation against non-copyable protected symbol
#     `_ZN15QSocketNotifier16staticMetaObjectE' in `/usr/lib/libQt6Core.so.6'
#   fcitx5-tq9-ui: _ZN15QSocketNotifier16staticMetaObjectE: /usr/lib/libQt6Core.so.6:
#     error due to GNU_PROPERTY_1_NEEDED_INDIRECT_EXTERN_ACCESS
#
# Ubuntu's Qt6 carries no such property, so a build made here - or the binary
# tarball build_package.sh ships - looks perfectly healthy until it is run on
# Arch, and a from-source build on Arch is broken in the same way. Reading
# external data through the GOT costs an indirection and removes every copy
# relocation, which makes one binary correct on both. Only the executable needs
# it; the tq9 addon is a shared object and never gets copy relocations.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-mno-direct-extern-access HAVE_NO_DIRECT_EXTERN_ACCESS)
if(HAVE_NO_DIRECT_EXTERN_ACCESS)
    target_compile_options(fcitx5-tq9-ui PRIVATE -mno-direct-extern-access)
else()
    # Clang spells the same thing differently.
    check_cxx_compiler_flag(-fno-direct-access-external-data
                            HAVE_NO_DIRECT_ACCESS_EXTERNAL_DATA)
    if(HAVE_NO_DIRECT_ACCESS_EXTERNAL_DATA)
        target_compile_options(fcitx5-tq9-ui PRIVATE -fno-direct-access-external-data)
    else()
        message(WARNING
            "No -mno-direct-extern-access support: the UI may fail to start on "
            "distros whose Qt6 is built with indirect external access (Arch).")
    endif()
endif()

install(TARGETS tq9 DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
install(TARGETS fcitx5-tq9-ui DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(FILES data/tq9.conf DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon")
install(FILES data/config.json DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/tq9")
# One speech prompt per translate mode, plus the one for translating a selection.
install(FILES data/stt_prompt_off.txt
              data/stt_prompt_only.txt
              data/stt_prompt_both.txt
              data/translate_prompt.txt
        DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/tq9")
install(FILES data/dataset.db DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/tq9")
install(FILES data/icons/16x16/tq9.png DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/16x16/apps")
install(FILES data/icons/24x24/tq9.png DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/24x24/apps")
install(FILES data/icons/32x32/tq9.png DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/32x32/apps")
install(FILES data/icons/48x48/tq9.png DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/48x48/apps")
install(FILES data/icons/64x64/tq9.png DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/64x64/apps")
install(FILES data/icons/128x128/tq9.png DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/128x128/apps")
install(FILES data/icons/256x256/tq9.png DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps")
install(FILES data/icons/48x48/tq9.png DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod")
install(DIRECTORY data/img DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/tq9")
