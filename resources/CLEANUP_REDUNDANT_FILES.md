# Cleanup: Redundant Files After App Bundle Conversion

## Overview
After converting to macOS app bundles, CMake still creates some redundant files alongside the `.app` bundles. These can be safely removed.

## Redundant Files in `build/editor/`

### Files to Remove
- **`DaedalusEdit`** (14 MB) - Standalone executable (redundant, use `DaedalusEdit.app` instead)
- **`daedalus_shaders.metallib`** (340 KB) - Old shader library copy (now inside `.app`)
- **`assets/`** - Old asset directory copy (now inside `.app/Contents/MacOS/`)
- **`editor_assets/`** - Old editor asset copy (now inside `.app/Contents/MacOS/`)

### Keep These
- **`DaedalusEdit.app/`** - The actual app bundle ✓
- **`daedalusedit.ini`** - Editor settings file (still needed) ✓
- **CMake build files** - cmake_install.cmake, Makefile, CMakeFiles/ ✓

## Redundant Files in `build/app/`

### Files to Remove
- **`DaedalusApp`** (23 MB) - Standalone executable (redundant, use `DaedalusApp.app` instead)
- **`daedalus_shaders.metallib`** (340 KB) - Old shader library copy (now inside `.app`)
- **`assets/`** - Old asset directory copy (now inside `.app/Contents/MacOS/`)

### Keep These
- **`DaedalusApp.app/`** - The actual app bundle ✓
- **CMake build files** - cmake_install.cmake, Makefile, CMakeFiles/ ✓

## Manual Cleanup Commands

To remove the redundant files:

```bash
cd ~/Documents/Daedalus/build

# Remove redundant editor files
rm -f editor/DaedalusEdit
rm -f editor/daedalus_shaders.metallib
rm -rf editor/assets
rm -rf editor/editor_assets

# Remove redundant app files
rm -f app/DaedalusApp
rm -f app/daedalus_shaders.metallib
rm -rf app/assets
```

**Note:** The `.DS_Store` file in `build/editor/` is a macOS Finder metadata file and can also be removed (it will be recreated by Finder if needed).

## Why These Files Exist

CMake creates both:
1. A standalone executable (the old behavior)
2. An app bundle (the new behavior)

This happens because `MACOSX_BUNDLE` in `add_executable()` creates the bundle, but the executable is also built as a standalone binary by default. The POST_BUILD commands that copy assets and shaders also run before the bundle is created, leaving copies in the build directory.

## Will They Come Back?

**Yes** - These files will be recreated on every build unless we modify the CMakeLists.txt files.

### To Prevent Future Redundant Files

We could modify the CMakeLists.txt to:
1. Only build app bundles (no standalone executables)
2. Remove the standalone asset copy commands

However, keeping the standalone executables can be useful for:
- Command-line debugging with tools like `lldb`
- Running from terminal with direct arguments
- Profiling with Instruments

**Recommendation:** Keep the current setup but add a `.gitignore` to ignore these build artifacts. The app bundles are what you actually distribute and use.

## Total Space Savings

Removing redundant files frees up approximately:
- Editor: ~15 MB (14 MB executable + shader lib + assets)
- App: ~24 MB (23 MB executable + shader lib + assets)
- **Total: ~39 MB per build**

## Summary

- **Safe to delete:** All files listed above under "Files to Remove"
- **Do NOT delete:** `.app` bundles, `.ini` files, CMake build files
- **Will regenerate:** These files will come back on next build
- **Consider:** Adding them to `.gitignore` if committing build artifacts
