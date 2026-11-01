# Resolume Browser Source (FFGL 2.x Plugin for Windows & macOS)

A native **FreeFrameGL (FFGL 2.x) Source Plugin** (`FF_SOURCE`) for **Resolume Arena & Avenue 7+** that embeds a hardware-accelerated web browser directly into Resolume's **Sources** panel, complete with a **URL** input section and full clip controls in the Clip Properties panel.

## Features

- **Native Clip Options UI (Grouped Parameters)**:
  - **Browser Source**:
    - `URL` (`FF_TYPE_TEXT`): Enter any `https://`, `http://`, `file://`, or `data:` URL directly in the clip options.
    - `Reload Page`, `Navigate Back`, `Navigate Fwd` (`FF_TYPE_EVENT`): Trigger buttons for page navigation.
    - `Transparent BG` (`FF_TYPE_BOOLEAN`): Enables full alpha-channel transparency (`rgba(0,0,0,0)`) for web overlays, lower thirds, and shaders.
    - `Page Zoom` (`FF_TYPE_STANDARD`): Scale page zoom from `25%` to `300%`.
    - `Custom CSS` (`FF_TYPE_TEXT`): Live CSS injection on every page load or parameter edit.
  - **Resolution & FPS**:
    - `Resolution` (`FF_TYPE_OPTION`): Choose `Auto (Match Comp)`, `1920 x 1080 (1080p)`, `1280 x 720 (720p)`, `3840 x 2160 (4K)`, or `Custom Size`.
    - `Custom Width` / `Custom Height` (`FF_TYPE_INTEGER`): Dynamically shown when `Custom Size` is selected (`128` to `4096` px).
    - `Target FPS` (`FF_TYPE_INTEGER`): Configurable capture rate (`1` to `120` FPS).
  - **Audio**:
    - `Mute Audio` (`FF_TYPE_BOOLEAN`): Mute/unmute browser audio playback.
    - `Audio Volume` (`FF_TYPE_STANDARD`): Adjust HTML5 `<video>` / `<audio>` element gain (`0%` to `100%`).
  - **Interaction**:
    - `Pop-out Window` (`FF_TYPE_BOOLEAN`): Opens an interactive desktop browser window so you can log in, dismiss cookie banners, or interact directly with the page using your keyboard and mouse.
    - `Mouse X`, `Mouse Y`, `Left Click`, `Right Click`, `Scroll Wheel`: Parameter-driven mouse & scroll automation that can be keyframed or mapped to MIDI/OSC inside Resolume.

## Cross-Platform Architecture

| Platform | Browser Engine | Output Binary |
|----------|----------------|---------------|
| **Windows (x64)** | Microsoft Edge WebView2 (`WebView2LoaderStatic.lib` statically linked) | `BrowserSource.dll` |
| **macOS (Universal / Apple Silicon / Intel)** | Apple `WKWebView` (`WebKit.framework` + `Cocoa.framework`) | `BrowserSource.bundle` |

## Installation in Resolume

### Windows
Copy `build/Release/BrowserSource.dll` to your Resolume Extra Effects folder:
```text
C:\Users\<YourUsername>\Documents\Resolume Arena\Extra Effects\BrowserSource.dll
```
*(or `Resolume Avenue\Extra Effects\`)*

### macOS
Build on macOS using CMake and copy `BrowserSource.bundle` to:
```text
~/Documents/Resolume Arena/Extra Effects/BrowserSource.bundle
```
*(or `/Library/Application Support/Resolume Arena/plugins/vfx/`)*

Then open Resolume, look under the **Sources** tab for **Browser Source**, drag it onto a clip slot, and enter your desired URL in the **Browser Source -> URL** field in the Clip panel.

## Building from Source

### Windows (Visual Studio 2022 + CMake)
```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### macOS (Xcode Command Line Tools + CMake)
Run the included one-step build, sign, test, and install script:
```bash
chmod +x build_mac.sh
./build_mac.sh
```
Or build manually via CMake:
```bash
cmake -B build_mac -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET="11.0"
cmake --build build_mac --config Release
codesign --force --deep --sign - build_mac/BrowserSource.bundle
cp -R build_mac/BrowserSource.bundle "$HOME/Documents/Resolume Arena/Extra Effects/"
```
*(You can also push this repository to GitHub to have [`.github/workflows/build.yml`](.github/workflows/build.yml) automatically compile and package `BrowserSource-macOS-Universal.zip` on a macOS runner.)*
