# macOS App Bundle Setup - Summary

## What Was Done

### 1. Icon Creation
- Converted PNG icons to macOS `.icns` format with all required sizes (16x16 to 1024x1024)
  - `EditorIcon.png` → `EditorIcon.icns` (845 KB)
  - `StandAloneIcon.png` → `AppIcon.icns` (223 KB)

### 2. App Bundle Configuration
Both executables were converted from simple binaries to proper macOS app bundles:

**DaedalusEdit.app**
- Bundle ID: `com.daedalus.editor`
- Display name: "Daedalus Edit"
- Icon: `EditorIcon.icns`
- File associations: `.emap` (Daedalus Map files)

**DaedalusApp.app**
- Bundle ID: `com.daedalus.app`
- Display name: "Daedalus"
- Icon: `AppIcon.icns`
- File associations: `.dlevel` (Daedalus Level Pack files)

### 3. CMake Build System Updates

**Both CMakeLists.txt files updated to:**
- Build as `MACOSX_BUNDLE`
- Reference custom `Info.plist` files
- Copy icons to `Contents/Resources/`
- Copy shader libraries to `Contents/MacOS/` (next to executable)
- Copy assets to `Contents/MacOS/assets/`

### 4. Critical Bug Fixes

**Issue:** Apps crashed on launch with "library not found" error

**Root Cause:**
- Shader library (`.metallib`) was being copied to wrong location
- Editor used `SDL_GetBasePath()` which returns `Contents/Resources/` for app bundles
- Shader library was in `Contents/MacOS/` where the executable is

**Solution:**
1. Updated CMake to copy `.metallib` to `$<TARGET_FILE_DIR:...>` (resolves to `Contents/MacOS/`)
2. Changed editor's `executableDir()` function to use `platform->getExecutableDir()` instead of `SDL_GetBasePath()`
3. Added missing `#include "daedalus/core/create_platform.h"` to editor

## App Bundle Structure

```
DaedalusEdit.app/
├── Contents/
│   ├── Info.plist              # Bundle metadata
│   ├── MacOS/
│   │   ├── DaedalusEdit        # Executable
│   │   ├── daedalus_shaders.metallib  # Shader library
│   │   ├── assets/             # Shared assets
│   │   └── editor_assets/      # Editor-specific assets
│   └── Resources/
│       └── EditorIcon.icns     # App icon

DaedalusApp.app/
├── Contents/
│   ├── Info.plist              # Bundle metadata
│   ├── MacOS/
│   │   ├── DaedalusApp         # Executable
│   │   ├── daedalus_shaders.metallib  # Shader library
│   │   └── assets/             # Shared assets
│   └── Resources/
│       └── AppIcon.icns        # App icon
```

## Files Modified

### CMake
- `editor/CMakeLists.txt` - app bundle configuration, shader copy fix
- `app/CMakeLists.txt` - app bundle configuration, shader copy fix

### Source Code
- `editor/src/main.mm` - fixed `executableDir()` to use platform API

### Resources Created
- `resources/DaedalusEdit-Info.plist`
- `resources/DaedalusApp-Info.plist`
- `resources/icons/EditorIcon.icns`
- `resources/icons/AppIcon.icns`
- `resources/icons/EditorIcon.iconset/` (multi-resolution sources)
- `resources/icons/AppIcon.iconset/` (multi-resolution sources)

## Testing

Both applications now:
- Launch successfully without crashes
- Display custom icons in Finder and Dock
- Are recognized as proper macOS applications
- Support file associations (double-click `.emap` or `.dlevel` files to open)

## Future Builds

All future builds automatically:
- Copy icons into app bundles
- Copy shader libraries to correct location
- Maintain proper app bundle structure
- Work correctly when launched via Finder or command line

## Notes

- Finder was restarted to refresh icon cache
- Icons appear in Dock, Finder, and Spotlight
- App bundles are located in `build/editor/` and `build/app/`
- Original executable binaries still exist alongside `.app` bundles for command-line use
