# ImGui Builder

A visual ImGui menu designer for Android (portrait). Design game-style ImGui menus without writing layout code: add widgets from a palette, arrange them in a live preview, and export the result as ready-to-use C++ ImGui source.

## Features

- Widget palette: add ImGui widgets (text, buttons, sliders, checkboxes, etc.) to your design
- Live preview rendered with real ImGui
- Generated C++ ImGui code for the designed menu
- Copy generated code to clipboard
- Save and load designs

## Downloads

APKs are built automatically by GitHub Actions on every push to `main`. See the **Actions** tab and download the `imgui-builder-debug-apk` artifact.

## Build requirements

| Component    | Version       |
|--------------|---------------|
| AGP          | 7.4.2         |
| Gradle       | 7.5.1         |
| NDK          | 25.2.9519653  |
| compileSdk   | 33            |
| minSdk       | 26            |

Build locally with:

```
./gradlew assembleDebug
```
