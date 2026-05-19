# IMGUI-MCGG

Dear ImGui overlay scaffolding for injection into Unity IL2CPP Android games (specifically Magic Chess: Go Go).

This is a **template** — no cheat/feature code, just the rendering + hook scaffolding. Login dialog placeholder + sidebar-tab shell. Drop your own features in.

## What it does

- Hooks `eglSwapBuffers` via Dobby — runs every frame after the game finishes rendering
- Initializes Dear ImGui (Android + OpenGL ES 3 backends) on first frame
- Renders either:
  - **Login dialog** until you provide a valid key (template accepts any non-empty input — replace with your backend)
  - **Mod menu** with sidebar tabs (Autoplay / Brutal Ops / Arena / Info / Room / Config) — empty bodies for you to fill in
- Resets Unity's leftover GL state so ImGui draws correctly on top

## Build

Requires Android NDK (tested with r27d).

```bash
ndk-build -C jni -j8
```

Output: `libs/arm64-v8a/libmain.so` (~4 MB).

## Deploy

The .so is a proxy library — it replaces `libmain.so` in the game's APK and Unity calls it instead of the original. You'll need to:

1. Force-stop the game
2. Replace `lib/arm64-v8a/libmain.so` inside the APK (or `/data/app/<pkg>-*/lib/arm64/libmain.so` on a rooted device) with the built `.so`
3. Re-launch the game

See `scripts/repack_apk.py` (in the parent project) for an example APK repacker.

## Project layout

```
jni/
├── Main.cpp           # entry point, hooks, ImGui setup, render loop
├── Android.mk         # build config
├── Application.mk     # ABI + platform
├── DOBBY/             # hook framework (vendored, arm64-v8a only)
├── XDL/               # symbol resolver (source)
├── IMGUI/             # Dear ImGui v1.x with android+opengl3 backends
└── UNITY/             # Unity IL2CPP wrapper headers
```

## Stubbed parts (you fill these in)

- **Key validation** (`Main.cpp` → `RenderLoginDialog`) — currently accepts any non-empty input. Wire up your HTTP/license backend here.
- **Tab content** (`Main.cpp` → `RenderModMenu`) — six empty tabs. Add your `ImGui::Checkbox` / `ImGui::Button` / feature logic.
- **Game hooks** — only `eglSwapBuffers` is hooked. To intercept game methods, add IL2CPP method resolution (helper macros in the parent project use `xdl_sym` + `il2cpp_class_get_method_from_name`) and call `DobbyHook(addr, handler, &original)`.

## License

This is scaffolding code. The vendored libraries (Dear ImGui, Dobby, XDL) retain their original licenses.

## Notes

- Target process check: only attaches when `/proc/self/cmdline` contains `:UnityKillsMe` (Magic Chess: Go Go's child process). Change the substring in `IsTargetProcess()` for other games.
- The library exports only `JNI_OnLoad` and the constructor; everything else is hidden via `-fvisibility=hidden`.
- Default debug log tag: `IMGUI-MCGG`. Filter with `adb logcat -s IMGUI-MCGG`.
