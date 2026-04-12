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

The optional nvJPEG backend is compiled by default. To disable it explicitly, configure with `-DHYPERBROWSE_ENABLE_NVJPEG=OFF`.

### Ninja + MSVC

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

## Run

The executable will be generated under the selected build output directory. On first run it creates the main shell and persists the splitter width on exit.

## Packaging

Create the portable layout after building:

```powershell
cmake --install build --config Debug --component Portable --prefix build/dist/HyperBrowse-0.1.0-portable
```

Create the installer-friendly staging layout after building:

```powershell
cmake --install build --config Debug --component Runtime --prefix build/dist/HyperBrowse-0.1.0-installer-layout
```

The portable layout stages `HyperBrowse.exe`, the required VC runtime DLLs for the active MSVC toolchain when needed, and a short `README.txt`. The installer-friendly layout stages the executable under `bin/` and documentation under `docs/` so an external installer step can consume a predictable layout.

When nvJPEG support is enabled, CMake now downloads the official NVIDIA `cuda_cudart` and `libnvjpeg` redistributable archives, verifies their SHA256 hashes, copies the runtime DLLs beside the built executables, and installs those DLLs and license files into the portable and installer layouts. The application no longer relies on CUDA or nvJPEG being present on `PATH`.

## Notes

This repo is intentionally lightweight and ready for iterative expansion using the spec pack and prompt sequence.

When the nvJPEG backend is compiled in, HyperBrowse still falls back to WIC automatically if runtime activation fails, for example on machines without a compatible NVIDIA GPU.
