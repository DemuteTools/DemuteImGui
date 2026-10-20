// SPDX-License-Identifier: MIT
//
// What the host target needs of REAPER, injected once by the tool at its plugin entry (SetHostServices):
// the docker, the ExtState that keeps a floating window's rect, and the tool's log. The library never
// includes the REAPER SDK: the tool assigns REAPER's own function pointers (their signatures match:
// `s.get_ext_state = GetExtState;`, `s.dock_add_ex = DockWindowAddEx;`...).
//
// Services are global (not a parameter) because a tool builds its FloatUiWindow objects as static globals,
// before REAPER loads it: their constructors stay as they are. Before SetHostServices (or with a service
// left null) a preference reads as its default, and a write, a dock call or a log line does nothing.
// Main thread only.

#pragma once

#ifdef _WIN32
#include <windows.h>
#else
// Only so the services (and their tests) compile on the host; the target itself is Windows only.
struct HWND__;
typedef HWND__* HWND;
#endif

namespace rav {
namespace host {

enum class LogLevel { Info, Warn, Error };

struct HostServices {
    // A preference float. Either the tool's own pair (RAV: LoadPrefFloat / SavePrefFloat)...
    float (*load_pref_float)(const char* key, float def) = nullptr;
    void (*save_pref_float)(const char* key, float value) = nullptr;
    // ...or REAPER's ExtState and a section: then read with ParsePrefFloat, written "%.1f", persist = true.
    const char* (*get_ext_state)(const char* section, const char* key) = nullptr;
    void (*set_ext_state)(const char* section, const char* key, const char* value, bool persist) = nullptr;
    const char* ext_section = nullptr;  // copied by SetHostServices

    // REAPER's docker (DockWindowAddEx, DockWindowActivate, DockWindowRemove).
    void (*dock_add_ex)(HWND hwnd, const char* name, const char* identstr, bool allow_show) = nullptr;
    void (*dock_activate)(HWND hwnd) = nullptr;
    void (*dock_remove)(HWND hwnd) = nullptr;

    // The tool's log (one line, no trailing newline).
    void (*log)(LogLevel level, const char* text) = nullptr;
};

// Replaces the services (a copy is kept; `ext_section` too).
void SetHostServices(const HostServices& services);
// The services in use (all null before SetHostServices).
const HostServices& GetHostServices();

// The library's own way to the services (each one does nothing, or gives `def`, when absent).
float LoadPrefFloat(const char* key, float def);
void  SavePrefFloat(const char* key, float value);
void  DockAddEx(HWND hwnd, const char* name, const char* identstr, bool allow_show);
void  DockActivate(HWND hwnd);
void  DockRemove(HWND hwnd);
void  Log(LogLevel level, const char* fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

}  // namespace host
}  // namespace rav
