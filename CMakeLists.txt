cmake_minimum_required(VERSION 3.10)
project(fcitx5-custom-input)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_definitions(-DQT_NO_KEYWORDS)

find_package(ECM REQUIRED 1.0.0)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake")


find_package(Fcitx5Core REQUIRED)
find_package(Qt5 COMPONENTS Core Widgets Gui Concurrent REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED sqlite3)

include_directories(${Fcitx5Core_INCLUDE_DIRS} ${SQLITE3_INCLUDE_DIRS})

add_library(custom_input MODULE
    src/addon.cpp
    src/CustomEngine.cpp
    src/CustomEngine.h
    src/Database.cpp
    src/Database.h
    src/ConfigLoader.cpp
    src/ConfigLoader.h
    src/ui/FloatingWindow.cpp
    src/ui/FloatingWindow.h
    src/ui/CustomButton.cpp
    src/ui/CustomButton.h
)

target_link_libraries(custom_input
    Fcitx5::Core
    Qt5::Core
    Qt5::Widgets
    Qt5::Gui
    Qt5::Concurrent
    ${SQLITE3_LIBRARIES}
)

install(TARGETS custom_input DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
install(FILES data/custom-input.conf DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon")
install(FILES data/config.json DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/custom-input")
