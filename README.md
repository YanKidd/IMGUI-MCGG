# IMGUI-MCGG

<p align="center">
  <img alt="Android NDK" src="https://img.shields.io/badge/Android_NDK-r27d-3DDC84?style=for-the-badge&logo=android&logoColor=white">
  <img alt="C++20" src="https://img.shields.io/badge/C++-20-00599C?style=for-the-badge&logo=cplusplus&logoColor=white">
  <img alt="ABI" src="https://img.shields.io/badge/ABI-arm64--v8a-111827?style=for-the-badge">
  <img alt="Renderer" src="https://img.shields.io/badge/Renderer-OpenGL_ES_3-5586A4?style=for-the-badge">
</p>

<p align="center">
  <strong>Dear ImGui overlay scaffold for Unity IL2CPP Android games.</strong>
  <br>
  Built around Magic Chess: Go Go, Dobby hooks, XDL symbol lookup, and an Android NDK C++20 pipeline.
</p>

```
    IMGUI-MCGG
    +-- EGL frame hook
    +-- Dear ImGui Android overlay
    +-- IL2CPP method resolver
    +-- Login dialog scaffold
    +-- Info / Room / Config menu shell
```

## Overview

`IMGUI-MCGG` is a native Android shared library template for rendering a Dear ImGui interface on top of a Unity IL2CPP game. It is intentionally structured as scaffolding: rendering, hook lifecycle, process guard, and UI shell are already wired, while project-specific feature logic can be added in controlled sections.

The current build focuses on:

| Area | Details |
| --- | --- |
| Render hook | Hooks `eglSwapBuffers` and renders after the game frame |
| UI runtime | Initializes Dear ImGui with Android + OpenGL ES 3 backends |
| Process guard | Attaches only when `/proc/self/cmdline` contains `:UnityKillsMe` |
| Room info | Reads cached `SystemData.LogicGetRoomData()` data for the Room tab |
| Build target | Android `arm64-v8a`, API 21, Clang, C++20, `c++_static` |

## Interface

The overlay is split into a compact login flow and a sidebar-based menu.

| Tab | Purpose |
| --- | --- |
| `Info` | Shows runtime state, framebuffer size, target info, and initialization status |
| `Room` | Shows read-only room roster data when the game cache is available |
| `Config` | Reserved for menu configuration and future controls |

The login dialog currently accepts any non-empty key. Replace that placeholder with your own validation backend if you need real license handling.

## Quick Build

Install Android NDK first. The template was prepared for NDK r27d.

```bash
ndk-build -C jni -j8
```

Expected output:

```text
libs/arm64-v8a/libmain.so
```

## Deploy Flow

The generated `.so` is a proxy-style native library. In a local test workflow, it replaces the target APK's `libmain.so`.

1. Force-stop the game.
2. Build `libs/arm64-v8a/libmain.so`.
3. Replace `lib/arm64-v8a/libmain.so` inside the APK, or the installed library path on a rooted test device.
4. Re-launch the game.
5. Watch runtime logs with:

```bash
adb logcat -s IMGUI-MCGG
```

## Project Layout

```text
jni/
+-- Main.cpp           # Entry point, hook setup, ImGui lifecycle, menu rendering
+-- Android.mk         # Native build config
+-- Application.mk     # ABI, platform, STL, and toolchain settings
+-- DOBBY/             # Vendored hook framework
+-- XDL/               # Vendored symbol resolver
+-- IMGUI/             # Dear ImGui sources and Android/OpenGL backends
+-- UNITY/             # Unity IL2CPP wrapper headers
```

## Native Pipeline

```text
JNI_OnLoad / constructor
        |
        v
Target process guard
        |
        v
Setup thread
        |
        +--> Resolve libEGL + hook eglSwapBuffers
        |
        +--> Resolve IL2CPP APIs through XDL
        |
        v
First rendered frame
        |
        v
Initialize Dear ImGui + draw overlay
```

## Extension Points

| File | Function / Area | Use |
| --- | --- | --- |
| `jni/Main.cpp` | `RenderLoginDialog()` | Replace placeholder key validation |
| `jni/Main.cpp` | `RenderModMenu()` | Add new tabs or route tab content |
| `jni/Main.cpp` | `RenderRoomTab()` | Adjust read-only room display fields |
| `jni/Main.cpp` | `SetupThread()` | Add new hook setup after library resolution |
| `jni/Main.cpp` | `IsTargetProcess()` | Retarget the process guard for another app |

Keep hook originals grouped in `namespace Originals` and detours in `namespace Hooks` when adding native hooks.

## Build Notes

- Output artifacts belong in `libs/` and `obj/`; do not commit generated `.so` files.
- The project exports only the native loader entry points needed by Android.
- Vendored dependencies should stay untouched unless you are deliberately upgrading or patching them.
- If you adapt this for another target, document process-name and symbol changes.

## Contributors

| Contributor | Role |
| --- | --- |
| [YanKidd](https://github.com/YanKidd) | Project author |
| [Claude](https://github.com/claude) | Implementation assistance |
| [Codex](https://github.com/codex) | README and implementation assistance |

## License

This repository contains scaffolding code. Vendored libraries such as Dear ImGui, Dobby, and XDL retain their original licenses.
