// SPDX-License-Identifier: MIT
//
// See host_services.h.

#include "host_services.h"

#include <cstdarg>
#include <cstdio>
#include <string>

#include "host_prefs.h"

namespace rav {
namespace host {

namespace {

HostServices g_services;
std::string  g_section;  // the copy of ext_section

}  // namespace

void SetHostServices(const HostServices& services)
{
    g_services = services;
    g_section = services.ext_section ? services.ext_section : "";
    g_services.ext_section = services.ext_section ? g_section.c_str() : nullptr;
}

const HostServices& GetHostServices()
{
    return g_services;
}

float LoadPrefFloat(const char* key, float def)
{
    if (!key) return def;
    if (g_services.load_pref_float) return g_services.load_pref_float(key, def);
    if (!g_services.get_ext_state || !g_services.ext_section) return def;
    return ParsePrefFloat(g_services.get_ext_state(g_services.ext_section, key), def);
}

void SavePrefFloat(const char* key, float value)
{
    if (!key) return;
    if (g_services.save_pref_float) {
        g_services.save_pref_float(key, value);
        return;
    }
    if (!g_services.set_ext_state || !g_services.ext_section) return;
    g_services.set_ext_state(g_services.ext_section, key, FormatPrefFloat(value).c_str(), true);
}

void DockAddEx(HWND hwnd, const char* name, const char* identstr, bool allow_show)
{
    if (g_services.dock_add_ex) g_services.dock_add_ex(hwnd, name, identstr, allow_show);
}

void DockActivate(HWND hwnd)
{
    if (g_services.dock_activate) g_services.dock_activate(hwnd);
}

void DockRemove(HWND hwnd)
{
    if (g_services.dock_remove) g_services.dock_remove(hwnd);
}

void Log(LogLevel level, const char* fmt, ...)
{
    if (!g_services.log || !fmt) return;
    char    buf[1024];
    va_list ap;
    va_start(ap, fmt);
    const int n = std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return;
    g_services.log(level, buf);
}

}  // namespace host
}  // namespace rav
