# App Bundle Resource Path Fixes

## Issues Fixed

### Issue 1: Splash Screen Not Displaying
**Problem:** Splash screen image (`splash.png`) was not loading when the editor launched from the app bundle.

**Root Cause:** Missing leading slash in path concatenation:
- Code was: `exeDir + "editor_assets/splash.png"`  
- Should be: `exeDir + "/editor_assets/splash.png"`

**Fix:** `editor/src/panels/splash_screen.mm` line 99
```cpp
const std::string path = exeDir + "/editor_assets/splash.png";
```

### Issue 2: File Open Dialog Not Working
**Problem:** File→Open and File→Save As dialogs were not opening to the correct `assets/maps` directory.

**Root Cause:** Missing leading slash in path concatenation:
- Code was: `executableDir() + "assets/maps"`
- Should be: `executableDir() + "/assets/maps"`

**Fix:** `editor/src/main.mm` lines 246 and 270
```cpp
const std::string mapsDir = executableDir() + "/assets/maps";
```

## Why This Happened

When converting to macOS app bundles, the `executableDir()` function was changed to use `platform->getExecutableDir()` instead of `SDL_GetBasePath()`. This returns:

**Before (SDL_GetBasePath):**  
`/path/to/DaedalusEdit.app/Contents/Resources/`  
(includes trailing slash)

**After (platform->getExecutableDir):**  
`/path/to/DaedalusEdit.app/Contents/MacOS`  
(no trailing slash)

Without the trailing slash, concatenating paths like `exeDir + "assets"` produces:
- **Incorrect:** `/path/to/MacOSassets` ❌
- **Correct:** `/path/to/MacOS/assets` ✓

## Files Modified

### Code Changes
1. `editor/src/panels/splash_screen.mm` - splash screen image path
2. `editor/src/main.mm` - file dialog default directories (2 locations)

### No CMake Changes Required
The asset copying in CMakeLists.txt already uses proper path separators via CMake's `$<TARGET_FILE_DIR:...>` generator expression.

## Testing

Both issues are now resolved:
- ✅ Splash screen displays on editor launch
- ✅ File→Open dialog opens to correct maps directory
- ✅ File→Save As dialog opens to correct maps directory with proper default filename

## Related Resources

For more context on the app bundle setup, see:
- `APP_BUNDLE_SETUP.md` - Complete app bundle configuration
- `icons/README.md` - Icon generation and management
