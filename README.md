# HyperBrowse Starter Repository

This is an initial **pure native Win32 + CMake** scaffold for **HyperBrowse**, a high-performance Windows image browser and viewer.

## What is included

- CMake-based x64 desktop build
- Minimal Win32 application shell
- Main window with:
  - menu bar
  - left folder tree placeholder pane
  - draggable vertical splitter
  - right browser placeholder pane
  - bottom status bar
- Splitter position persistence in `HKCU\Software\HyperBrowse`
- Basic logging and timing scaffolding
- Source tree organized for the planned architecture

## Build

### CMake Presets

```powershell
cmake --preset vs2022-x64
cmake --build --preset debug
ctest --preset debug-tests
```

### Visual Studio 2022 / 2026 generator

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```

### Ninja + MSVC

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Run

The executable will be generated under the selected build output directory. On first run it creates the main shell and persists the splitter width on exit.

## Notes

This repo is intentionally lightweight and ready for iterative expansion using the spec pack and prompt sequence.
