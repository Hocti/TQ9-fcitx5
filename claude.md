# Q9 Linux UI

## Overview
A Qt-based floating window UI for the Q9 input method engine on Linux (supporting X11 and Wayland via LayerShell).

## Source Structure
- `src/`: Core logic and UI
  - `ConfigLoader.cpp` / `.h`: Handles JSON configuration loading and saving. Maintains a distinction between default window settings (`window`) and runtime state (`storage`).
  - `ui/`: UI components
    - `FloatingWindow.cpp` / `.h`: The main frameless, transparent overlay window.
    - `CustomButton.cpp` / `.h`: Stylized buttons for the input interface.
    - `main.cpp`: Entry point, handles IPC via stdin/stdout.
- `data/`: Configuration and assets
  - `config.json`: Main configuration file.
  - `img/`: Button assets.
  - `dataset.db`: SQLite database for the engine.

## Configuration (config.json)
- `window`: Default settings (width, height, constraints). Should not be modified at runtime.
- `storage`: Runtime persistent state (last position, current size). Updated by `ConfigLoader::save`.
- `system`: Runtime settings (numpad mode, output options).
- `buttons`: Layout definitions for the interface.
- `key` / `altkey`: Keycode mappings.

## Build & Scripts
- `build_package.sh`: Compiles the release version and packages it into a `.tar.gz`.
- `build_install.sh`: Compiles the release version and executes the installer(執行安裝) without creating a tarball.
- `debug_run.sh`: Orchestration script for debug builds and testing.

## Recent Changes
- Created `build_install.sh` to streamline the build-and-install(建置與安裝) workflow.
- Fixed `ConfigLoader::save` to properly perform a read-modify-write operation, preserving all top-level JSON keys (like `buttons` and `key`) while updating `storage` and `system`.
- Added robust error handling in `ConfigLoader::save` to prevent file truncation on read failure.
