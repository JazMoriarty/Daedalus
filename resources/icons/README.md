# Daedalus App Icons

This directory contains the macOS application icons for Daedalus Engine executables.

## Icon Files

- **EditorIcon.icns** - Icon for DaedalusEdit (the map editor)
  - Source: `~/Desktop/EditorIcon.png`
  - Bundle: `DaedalusEdit.app`
  - Identifier: `com.daedalus.editor`

- **AppIcon.icns** - Icon for DaedalusApp (the standalone game runtime)
  - Source: `~/Desktop/StandAloneIcon.png`
  - Bundle: `DaedalusApp.app`
  - Identifier: `com.daedalus.app`

## Structure

```
resources/icons/
├── EditorIcon.icns           # Final editor icon
├── EditorIcon.iconset/       # Multi-resolution source (16x16 to 1024x1024)
├── EditorIcon_1024.png       # 1024x1024 source image
├── AppIcon.icns              # Final app icon
├── AppIcon.iconset/          # Multi-resolution source (16x16 to 1024x1024)
└── AppIcon_1024.png          # 1024x1024 source image
```

## Rebuilding Icons

If you need to update the icons from new PNG files:

1. Replace the source PNG files on the desktop
2. Run the icon generation script (or follow these steps manually):

```bash
cd ~/Documents/Daedalus/resources/icons

# Resize to 1024x1024
sips -s format png ~/Desktop/EditorIcon.png --out EditorIcon_1024.png -Z 1024
sips -s format png ~/Desktop/StandAloneIcon.png --out AppIcon_1024.png -Z 1024

# Generate iconsets (all required sizes)
mkdir -p EditorIcon.iconset AppIcon.iconset

# Editor icon - all sizes
sips -z 16 16     EditorIcon_1024.png --out EditorIcon.iconset/icon_16x16.png
sips -z 32 32     EditorIcon_1024.png --out EditorIcon.iconset/icon_16x16@2x.png
sips -z 32 32     EditorIcon_1024.png --out EditorIcon.iconset/icon_32x32.png
sips -z 64 64     EditorIcon_1024.png --out EditorIcon.iconset/icon_32x32@2x.png
sips -z 128 128   EditorIcon_1024.png --out EditorIcon.iconset/icon_128x128.png
sips -z 256 256   EditorIcon_1024.png --out EditorIcon.iconset/icon_128x128@2x.png
sips -z 256 256   EditorIcon_1024.png --out EditorIcon.iconset/icon_256x256.png
sips -z 512 512   EditorIcon_1024.png --out EditorIcon.iconset/icon_256x256@2x.png
sips -z 512 512   EditorIcon_1024.png --out EditorIcon.iconset/icon_512x512.png
sips -z 1024 1024 EditorIcon_1024.png --out EditorIcon.iconset/icon_512x512@2x.png

# App icon - all sizes (repeat for AppIcon)
sips -z 16 16     AppIcon_1024.png --out AppIcon.iconset/icon_16x16.png
sips -z 32 32     AppIcon_1024.png --out AppIcon.iconset/icon_16x16@2x.png
sips -z 32 32     AppIcon_1024.png --out AppIcon.iconset/icon_32x32.png
sips -z 64 64     AppIcon_1024.png --out AppIcon.iconset/icon_32x32@2x.png
sips -z 128 128   AppIcon_1024.png --out AppIcon.iconset/icon_128x128.png
sips -z 256 256   AppIcon_1024.png --out AppIcon.iconset/icon_128x128@2x.png
sips -z 256 256   AppIcon_1024.png --out AppIcon.iconset/icon_256x256.png
sips -z 512 512   AppIcon_1024.png --out AppIcon.iconset/icon_256x256@2x.png
sips -z 512 512   AppIcon_1024.png --out AppIcon.iconset/icon_512x512.png
sips -z 1024 1024 AppIcon_1024.png --out AppIcon.iconset/icon_512x512@2x.png

# Convert to .icns
iconutil -c icns EditorIcon.iconset -o EditorIcon.icns
iconutil -c icns AppIcon.iconset -o AppIcon.icns

# Rebuild the project
cd ~/Documents/Daedalus
cmake --build build --target DaedalusEdit DaedalusApp
```

## CMake Integration

The icons are automatically copied into the app bundles during build:

- **DaedalusEdit.app/Contents/Resources/EditorIcon.icns**
- **DaedalusApp.app/Contents/Resources/AppIcon.icns**

The `Info.plist` files reference these icons via the `CFBundleIconFile` key.

## File Associations

- **DaedalusEdit** (.emap files) - Daedalus Map editor format
- **DaedalusApp** (.dlevel files) - Daedalus Level Pack runtime format

These associations are defined in the respective Info.plist files.
