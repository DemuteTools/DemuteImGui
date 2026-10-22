# demute-imgui

Shared UI library for the Demute tools (RAV, RAW): Dear ImGui theme, widgets and icons.
MIT licence. Provisional name.

## CMake targets

- `theme`: theme, widgets, adaptive layout, vector icons. No REAPER or `windows.h` include.
  Provides Dear ImGui (core, no backend), pinned to 1.92.9b through `FetchContent`.
- `host` (Windows only): docked panel (`rav::host::DockedPanel`), floating windows (`rav::FloatUiWindow`),
  OpenGL context and ImGui context of a window, Windows fonts. Depends on `theme` and on the ImGui Win32 and
  OpenGL3 backends (`demute_imgui_backends`). No REAPER SDK include: REAPER is reached through injected
  services. Outside Windows the target is not configured; its pure parts (`host_prefs.h`, `float_geometry.h`,
  `float_key_route.h`) and the services are tested everywhere (`host_pure_test`).
  Every GL context switch goes through `rav::GlMakeCurrent` (`gl_switch.h`, RAV's freeze fix of
  2026-10-21): one lock per binary, a switch that waits more than 500 ms fails and its frame is skipped.
  A tool that switches contexts itself (on a video thread, say) calls it too.

Aliases: `demute_imgui::theme`, `demute_imgui::host`.

## Tests (no window)

    cmake -S . -B build && cmake --build build && ctest --test-dir build --output-on-failure

The default build is Debug: ImGui assertions are active. `theme_headless_test` creates an ImGui context
without a backend, applies the theme and draws frames with every widget and every icon.

## `host` target

### Services to inject (once, at the plugin entry point)

```cpp
#include "host_services.h"

rav::host::HostServices s;
s.get_ext_state = GetExtState;      // or s.load_pref_float / s.save_pref_float (the tool's own functions)
s.set_ext_state = SetExtState;      // writes "%.1f", persist = true
s.ext_section   = "MyTool";         // copied
s.dock_add_ex   = DockWindowAddEx;
s.dock_activate = DockWindowActivate;
s.dock_remove   = DockWindowRemove;
s.log = [](rav::host::LogLevel level, const char* text) { /* the tool's console */ };
rav::host::SetHostServices(s);
```

The signatures are those of the REAPER function pointers: assign them directly. Without services (or with a
null service), a preference reads as its default value, and a write, a docker call or a log line does
nothing. The services are global: `FloatUiWindow` objects built as global statics keep their constructor.

### Docked panel

`DockedPanel(config, callbacks)`: window class, title, `dock_name` (tab), `dock_ident` (docker position key,
never change it), initial size, 15 ms timer. The tool plugs its steps in through callbacks, in RAV's order:
GL context → `on_gl_ready` (false = failure) → vsync 0 → timer → ImGui (not fatal) → `on_imgui_ready` → one
tick. When the docker hides it, the panel pauses (`on_parked`, then `on_hidden_tick`) without being
destroyed; `on_resumed` when it comes back. `Open` / `Close` / `Toggle` never throw.
`SetDocked(false)`: outside the docker (full screen), `Open` brings it to the front and `Close` does not call
`DockWindowRemove`.

### Destruction order

1. `on_before_shutdown` (GL current, ImGui context still alive): destroy every `FloatUiWindow`
   (`Destroy`); they share the panel's font atlas and GL context.
2. OpenGL3 then Win32 backends, then the ImGui context (the atlas owner is destroyed last; ImGui 1.92 only
   destroys a texture once a single context still uses it).
3. `on_gl_shutdown` (GL still current): the tool's GL objects.

4. GL context and DC.

`on_before_shutdown` and `on_gl_shutdown` are also called after a failed open (no GL context,
`on_gl_ready` false, ImGui not created yet), like RAV's `StopRendering`: they must tolerate a half-built
state (`Rc()` / `ImGuiCtx()` may be null).

Once every window is closed: `FloatUiWindow::UnregisterWindowClass()` (when the DLL is unloaded).

### Keyboard in floating windows

In the tool's accelerator hook (`accelerator_register_t`, created by the tool):

```cpp
bool is_float = false;
const int route = rav::FloatUiWindow::RouteKey(msg, &is_float);
if (is_float) return route;  // 0 REAPER, -1 the window, -20 the window (system key)
```

### Using the library from a local folder

    FetchContent_Declare(demute_imgui SOURCE_DIR /path/to/demute-imgui)
    FetchContent_MakeAvailable(demute_imgui)
    target_link_libraries(my_tool PRIVATE demute_imgui::host)

RAW: `-DRAW_WITH_UI=ON -DRAW_DEMUTE_IMGUI_SOURCE=/path/to/demute-imgui` (see RAW's `cmake/raw_dependencies.cmake`). The
consuming project sets its `CMAKE_MSVC_RUNTIME_LIBRARY`; built on its own, the library uses `/MT` (`/MTd` in Debug).

### Windows build (MSVC)

    cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64
    cmake --build build-msvc --config Release
    ctest --test-dir build-msvc -C Release --output-on-failure

`host_compile_check` references the whole `host` API, opens no window and returns 0.

### Cross-compile check (MinGW, on Linux / WSL)

    cmake -S . -B build-mingw -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-mingw-w64.cmake -DMINGW_ROOT=<prefix>
    cmake --build build-mingw -j8

Checks that `host` and `host_compile_check.exe` compile and link (the executables are not run).

## Origin

Files taken from FbxAnimationViewer (RAV) at commit 283dbee, imported verbatim in the first commit
(see `git log`), then adapted to ImGui 1.92 and decoupled from RAV.
The `host` target takes `float_ui_window` (imported verbatim, RAV 47558f9) and the docking / GL / ImGui part of
`viewer_window.cpp`.

## Host contract (ImGui 1.92)

- Each font role (`ui::FontSet`) is an `ImFont*` added at its own size (`SizePixels` > 0), because the theme
  reads the size from `ImFont::LegacySize`. Use `ui::FontScope`, not `ImGui::PushFont(font)`.
- The render backend must support 1.92 textures (`ImGuiBackendFlags_RendererHasTextures`) and vertex
  offsets (`RendererHasVtxOffset`).
- `SoundNodeType` and `FoldScopeButtons` are copied from RAV; RAV will remove its duplicates along the way
  (story 1.3).
